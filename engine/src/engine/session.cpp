// seven7 — Engine layer: Session renderer implementation (see session.h)

#include "engine/session.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

#include "engine/dsp_graph.h"
#include "engine/mix_math.h"

namespace s7::engine {

// ─── SourceStore ─────────────────────────────────────────────────────────────

void SourceStore::add(domain::Id id, std::shared_ptr<const SourceAudio> audio) {
  Entry e;
  e.peaks = compute_peaks(*audio);
  e.audio = std::move(audio);
  entries_[id] = std::move(e);
}

std::shared_ptr<const SourceAudio> SourceStore::get(domain::Id id) const {
  auto it = entries_.find(id);
  return it == entries_.end() ? nullptr : it->second.audio;
}

const std::vector<float>* SourceStore::peaks(domain::Id id) const {
  auto it = entries_.find(id);
  return it == entries_.end() ? nullptr : &it->second.peaks;
}

std::vector<float> SourceStore::compute_peaks(const SourceAudio& a, std::int64_t block) {
  std::vector<float> out;
  const std::int64_t len = a.length();
  const int chs = a.channels();
  if (len <= 0 || chs <= 0 || block <= 0) return out;
  const std::int64_t blocks = (len + block - 1) / block;
  out.resize(static_cast<std::size_t>(blocks * chs * 2));
  for (std::int64_t b = 0; b < blocks; ++b) {
    const std::int64_t s0 = b * block, s1 = std::min(len, s0 + block);
    for (int c = 0; c < chs; ++c) {
      float mn = 1.f, mx = -1.f;
      const auto& ch = a.ch[static_cast<std::size_t>(c)];
      for (std::int64_t i = s0; i < s1; ++i) {
        const float v = ch[static_cast<std::size_t>(i)];
        mn = std::min(mn, v);
        mx = std::max(mx, v);
      }
      const std::size_t o = static_cast<std::size_t>((b * chs + c) * 2);
      out[o] = mn;
      out[o + 1] = mx;
    }
  }
  return out;
}

// ─── compile_snapshot ────────────────────────────────────────────────────────

std::unique_ptr<RenderSnapshot> compile_snapshot(const domain::Project& project, const SourceStore& sources,
                                                 const SchedulerConfig& sched, CompileReport* report) {
  auto snap = std::make_unique<RenderSnapshot>();
  snap->sample_rate = project.sample_rate;
  snap->spb_num = project.samples_per_beat_num();
  snap->spb_den = project.samples_per_beat_den();
  snap->sig_num = project.sig_num;

  auto warn = [&](std::string msg) { if (report) report->warnings.push_back(std::move(msg)); };

  // Routing graph over track indices (MIX-05: cycles rejected at commit).
  DspGraph graph;
  std::vector<NodeId> node_of(project.tracks.size());
  std::unordered_map<domain::Id, std::size_t> index_of;
  std::size_t master_index = project.tracks.size();
  for (std::size_t i = 0; i < project.tracks.size(); ++i) {
    const domain::Track& t = project.tracks[i];
    std::int64_t latency = 0;
    for (const auto& ins : t.inserts) if (!ins.bypassed) latency += ins.latency_samples;
    node_of[i] = graph.add_node(t.name, latency);
    index_of[t.id] = i;
    if (t.kind == domain::TrackKind::kMaster) master_index = i;
  }

  std::vector<int> effective_out(project.tracks.size(), -1);  // index of output track, -1 = master
  std::vector<std::vector<bool>> send_ok(project.tracks.size());
  for (std::size_t i = 0; i < project.tracks.size(); ++i) {
    const domain::Track& t = project.tracks[i];
    send_ok[i].assign(t.sends.size(), false);
    if (t.kind == domain::TrackKind::kMaster || t.kind == domain::TrackKind::kVca) continue;
    // main output
    std::size_t dest = master_index;
    if (t.output != domain::kNoId) {
      auto it = index_of.find(t.output);
      if (it != index_of.end()) dest = it->second;
      else warn("track '" + t.name + "': output target missing, routed to Master");
    }
    if (dest < project.tracks.size() && dest != i) {
      if (graph.add_edge(node_of[i], node_of[dest])) effective_out[i] = static_cast<int>(dest);
      else {
        warn("track '" + t.name + "': output to '" + project.tracks[dest].name + "' would create a cycle — routed to Master");
        if (master_index < project.tracks.size() && master_index != i) graph.add_edge(node_of[i], node_of[master_index]);
      }
    }
    // sends
    for (std::size_t k = 0; k < t.sends.size(); ++k) {
      const domain::Send& s = t.sends[k];
      if (!s.active || s.dest_track == domain::kNoId) continue;
      auto it = index_of.find(s.dest_track);
      if (it == index_of.end() || it->second == i) continue;
      if (graph.add_edge(node_of[i], node_of[it->second])) send_ok[i][k] = true;
      else warn("track '" + t.name + "': send " + std::to_string(k + 1) + " would create a cycle — disabled");
    }
  }

  std::vector<NodeId> order;
  if (!graph.topo_order(order)) {
    warn("routing graph is cyclic after commit — falling back to track order");
    order.clear();
    for (std::size_t i = 0; i < project.tracks.size(); ++i) order.push_back(node_of[i]);
  }

  LaneScheduler lanes(sched);
  for (NodeId n : order) {
    const std::size_t i = static_cast<std::size_t>(n);  // node ids are allocation order == track index
    const domain::Track& t = project.tracks[i];
    if (t.kind == domain::TrackKind::kVca) continue;  // control-only, no audio (MIX-02)
    if (t.rt_slot < 0 || t.rt_slot >= Session::kMaxSlots) { warn("track '" + t.name + "': no render slot"); continue; }

    RenderTrack rt;
    rt.id = t.id;
    rt.slot = t.rt_slot;
    rt.kind = t.kind;
    rt.input_channel = t.input_channel;
    rt.preset = t.instrument;
    rt.fader_pos = t.fader_pos;
    rt.pan = t.pan;
    rt.mute = t.mute;
    rt.solo = t.solo;
    rt.arm = t.arm;
    rt.monitor = t.monitor;
    rt.stereo = t.kind == domain::TrackKind::kAux || t.kind == domain::TrackKind::kBus || t.kind == domain::TrackKind::kMaster;
    rt.latency = graph.node(n).latency_samples;
    rt.out_slot = effective_out[i] >= 0 ? project.tracks[static_cast<std::size_t>(effective_out[i])].rt_slot : -1;

    TrackFlags flags;
    flags.record_armed = t.arm;
    flags.input_monitoring = t.monitor;
    flags.live_instrument = t.kind == domain::TrackKind::kInstrument && (t.arm || t.monitor);
    rt.lane = lanes.classify(flags, false);
    if (t.kind == domain::TrackKind::kAudio || t.kind == domain::TrackKind::kInstrument) {
      if (rt.lane == Lane::kRt) ++snap->rt_tracks; else ++snap->mae_tracks;
    }

    for (domain::Id rid : project.regions_on_track(t.id)) {
      const domain::Region* r = project.find_region(rid);
      if (!r || r->muted || r->length_samples <= 0) continue;
      if (r->kind == domain::RegionKind::kAudio) {
        auto src = sources.get(r->source_id);
        if (!src) { warn("region '" + r->name + "': source missing (offline)"); continue; }
        RenderRegion rr;
        rr.start = r->start_sample;
        rr.end = r->end_sample();
        rr.offset = r->offset_sample;
        rr.fade_in = r->fade_in_samples;
        rr.fade_out = r->fade_out_samples;
        rr.gain = static_cast<float>(db_to_gain(r->gain_db));
        if (src->channels() > 1) rt.stereo = true;  // stereo source ⇒ balance panner
        rr.src = std::move(src);
        rt.regions.push_back(std::move(rr));
      } else {
        const float region_gain = static_cast<float>(db_to_gain(r->gain_db));
        for (const domain::Note& nt : r->notes) {
          if (nt.sample < 0 || nt.sample >= r->length_samples) continue;  // outside clip bounds: silent
          RenderNote rn;
          rn.start = r->start_sample + nt.sample;
          rn.end = std::min(r->end_sample(), rn.start + std::max<std::int64_t>(1, nt.length_samples));
          rn.pitch = nt.pitch;
          const float v = std::clamp(static_cast<float>(nt.velocity) * region_gain, 1.f, 127.f);
          rn.vel = static_cast<std::uint8_t>(v);
          rt.notes.push_back(rn);
        }
      }
    }
    std::sort(rt.notes.begin(), rt.notes.end(), [](const RenderNote& a, const RenderNote& b) { return a.start < b.start; });

    for (std::size_t k = 0; k < t.sends.size(); ++k) {
      if (!send_ok[i][k] || t.sends[k].muted) continue;
      const domain::Track* d = project.find_track(t.sends[k].dest_track);
      if (!d || d->rt_slot < 0) continue;
      RenderSend rs;
      rs.dest_slot = d->rt_slot;
      rs.gain = static_cast<float>(db_to_gain(t.sends[k].level_db));
      rs.pre = t.sends[k].pre;
      rt.sends.push_back(rs);
    }

    if (t.kind == domain::TrackKind::kMaster) snap->master_slot = rt.slot;
    snap->tracks.push_back(std::move(rt));
  }

  if (master_index < project.tracks.size()) {
    const CompensationReport pdc = graph.compensate(node_of[master_index], sched.adc_cap);
    snap->pdc_worst = pdc.domain_delay;
    if (report) report->pdc_worst = pdc.domain_delay;
    for (NodeId over : pdc.over_cap) warn("track '" + graph.node(over).name + "': latency exceeds PDC cap (partially compensated)");
  }
  return snap;
}

// ─── Session ─────────────────────────────────────────────────────────────────

Session::Session() { prepare(48000, 512); }
Session::~Session() = default;

void Session::prepare(std::int64_t sample_rate, int max_block) {
  sample_rate_ = sample_rate > 0 ? sample_rate : 48000;
  max_block_ = std::clamp(max_block, 16, kMaxBlock);
  smooth_coef_ = 1.f - std::exp(-1.f / (0.005f * static_cast<float>(sample_rate_)));  // 5 ms dezipper (AUT-02)
  meter_decay_ = std::exp(-1.f / (0.3f * static_cast<float>(sample_rate_)));          // ~300 ms fall
  metro_inc_ = 0.f;

  slots_.assign(static_cast<std::size_t>(kMaxSlots), SlotState{});
  for (SlotState& s : slots_) { s.synth.prepare(static_cast<double>(sample_rate_)); s.synth_ready = true; }
  acc_.assign(static_cast<std::size_t>(kMaxSlots) * 2 * static_cast<std::size_t>(max_block_), 0.0);
  scratch_.assign(static_cast<std::size_t>(max_block_) * 4, 0.f);

  // Record-chunk pool: 64 × 32768 floats ≈ 8 MB → ~44 s of mono @48k in flight.
  chunk_storage_.clear();
  RecordChunk* dummy = nullptr;
  while (pool_.pop(dummy)) {}
  FilledChunk fc;
  while (filled_.pop(fc)) {}
  for (int i = 0; i < 64; ++i) {
    chunk_storage_.push_back(std::make_unique<RecordChunk>());
    pool_.push(chunk_storage_.back().get());
  }
  for (auto& p : peak_) p.store(0.f, std::memory_order_relaxed);
  for (auto& c : clip_) c.store(false, std::memory_order_relaxed);
}

void Session::publish(std::unique_ptr<RenderSnapshot> snapshot) {
  snapshot->serial = next_serial_++;
  a_rt_tracks_.store(snapshot->rt_tracks, std::memory_order_relaxed);
  a_mae_tracks_.store(snapshot->mae_tracks, std::memory_order_relaxed);
  a_pdc_.store(snapshot->pdc_worst, std::memory_order_relaxed);
  snapshot_.publish(std::move(snapshot));
}

TransportStatus Session::transport() const {
  TransportStatus t;
  t.playing = a_playing_.load(std::memory_order_relaxed);
  t.recording = a_recording_.load(std::memory_order_relaxed);
  t.loop_on = a_loop_.load(std::memory_order_relaxed);
  t.metronome = a_metro_.load(std::memory_order_relaxed);
  t.playhead = a_pos_.load(std::memory_order_relaxed);
  t.loop_start = a_loop_start_.load(std::memory_order_relaxed);
  t.loop_end = a_loop_end_.load(std::memory_order_relaxed);
  t.cpu_load = a_cpu_.load(std::memory_order_relaxed);
  t.xruns = a_xruns_.load(std::memory_order_relaxed);
  t.rt_tracks = a_rt_tracks_.load(std::memory_order_relaxed);
  t.mae_tracks = a_mae_tracks_.load(std::memory_order_relaxed);
  t.pdc_worst = a_pdc_.load(std::memory_order_relaxed);
  return t;
}

void Session::service(const std::function<void(FinishedTake)>& on_take) {
  FilledChunk fc;
  while (filled_.pop(fc)) {
    if (fc.chunk) {
      auto& take = takes_[fc.slot];
      if (take.empty()) take_start_[fc.slot] = fc.start_sample;
      take.insert(take.end(), fc.chunk->data.begin(), fc.chunk->data.begin() + fc.chunk->used);
      fc.chunk->used = 0;
      pool_.push(fc.chunk);
    }
    if (fc.last) {
      auto it = takes_.find(fc.slot);
      if (it != takes_.end() && !it->second.empty()) {
        FinishedTake ft;
        ft.slot = fc.slot;
        ft.start_sample = take_start_[fc.slot];
        ft.audio = std::make_shared<SourceAudio>();
        ft.audio->sample_rate = sample_rate_;
        ft.audio->ch.push_back(std::move(it->second));
        takes_.erase(it);
        take_start_.erase(fc.slot);
        if (on_take) on_take(std::move(ft));
      }
    }
  }
  snapshot_.reclaim();
}

// ─── RT: commands ────────────────────────────────────────────────────────────

void Session::drain_commands(const RenderSnapshot* s) {
  Command c;
  while (commands_.pop(c)) {
    switch (c.type) {
      case Command::kPlay:
        if (!playing_) { playing_ = true; metro_next_beat_ = -1; }
        break;
      case Command::kStop:
        if (recording_) { stop_recording_all(); recording_ = false; }
        playing_ = false;
        all_notes_off(false);
        break;
      case Command::kLocate:
        if (recording_) { stop_recording_all(); recording_ = false; }
        pos_ = std::max<std::int64_t>(0, c.pos);
        metro_next_beat_ = -1;
        all_notes_off(false);
        break;
      case Command::kLoop:
        loop_on_ = c.a != 0 && c.pos2 > c.pos;
        loop_start_ = std::max<std::int64_t>(0, c.pos);
        loop_end_ = std::max(loop_start_ + 1, c.pos2);
        break;
      case Command::kRecord: {
        const bool on = c.a != 0;
        if (on && !recording_) {
          recording_ = true;
          if (s) start_recording_all(*s);
          if (!playing_) { playing_ = true; metro_next_beat_ = -1; }
        } else if (!on && recording_) {
          stop_recording_all();
          recording_ = false;
        }
        break;
      }
      case Command::kMetronome: metronome_ = c.a != 0; break;
      case Command::kParam: {
        if (c.slot < 0 || c.slot >= kMaxSlots) break;
        SlotState& st = slots_[static_cast<std::size_t>(c.slot)];
        switch (c.param) {
          case Command::kFader: st.fader_pos = std::clamp(c.value, 0.0, 1.0); break;
          case Command::kPan: st.pan = std::clamp(c.value, -1.0, 1.0); break;
          case Command::kMute: st.mute = c.value != 0.0; break;
          case Command::kSolo: st.solo = c.value != 0.0; break;
          case Command::kArm: st.arm = c.value != 0.0; break;
          case Command::kMonitor: st.monitor = c.value != 0.0; break;
        }
        break;
      }
      case Command::kNoteOn:
        if (c.slot >= 0 && c.slot < kMaxSlots) slots_[static_cast<std::size_t>(c.slot)].synth.note_on(c.a, c.b);
        break;
      case Command::kNoteOff:
        if (c.slot >= 0 && c.slot < kMaxSlots) slots_[static_cast<std::size_t>(c.slot)].synth.note_off(c.a);
        break;
      case Command::kAllNotesOff: all_notes_off(true); break;
      case Command::kPreset:
        if (c.slot >= 0 && c.slot < kMaxSlots) slots_[static_cast<std::size_t>(c.slot)].synth.set_preset(c.a);
        break;
    }
  }
}

void Session::adopt_snapshot(const RenderSnapshot& s) {
  for (const RenderTrack& t : s.tracks) {
    SlotState& st = slots_[static_cast<std::size_t>(t.slot)];
    st.fader_pos = t.fader_pos;
    st.pan = t.pan;
    st.mute = t.mute;
    st.solo = t.solo;
    st.arm = t.arm;
    st.monitor = t.monitor;
    st.synth.set_preset(t.preset);
  }
  seen_serial_ = s.serial;
}

void Session::all_notes_off(bool hard) {
  for (SlotState& s : slots_) s.synth.all_notes_off(hard);
}

// ─── RT: recording ───────────────────────────────────────────────────────────

void Session::start_recording_all(const RenderSnapshot& s) {
  for (const RenderTrack& t : s.tracks) {
    if (t.kind != domain::TrackKind::kAudio) continue;
    SlotState& st = slots_[static_cast<std::size_t>(t.slot)];
    if (!st.arm || st.recording) continue;
    RecordChunk* chunk = nullptr;
    if (!pool_.pop(chunk)) { a_xruns_.fetch_add(1, std::memory_order_relaxed); continue; }
    chunk->used = 0;
    st.chunk = chunk;
    st.chunk_start = pos_;
    st.rec_pos = pos_;
    st.recording = true;
  }
}

void Session::stop_recording_all() {
  for (int i = 0; i < kMaxSlots; ++i) {
    SlotState& st = slots_[static_cast<std::size_t>(i)];
    if (!st.recording) continue;
    flush_recording(st, i);
    FilledChunk end;
    end.slot = i;
    end.start_sample = st.rec_pos;
    end.last = true;
    filled_.push(end);
    st.recording = false;
  }
}

void Session::flush_recording(SlotState& st, int slot) {
  if (!st.chunk) return;
  FilledChunk fc;
  fc.chunk = st.chunk;
  fc.slot = slot;
  fc.start_sample = st.chunk_start;
  if (!filled_.push(fc)) a_xruns_.fetch_add(1, std::memory_order_relaxed);  // consumer stalled: chunk lost
  st.chunk = nullptr;
}

void Session::capture_input(SlotState& st, const float* src, int n) {
  int done = 0;
  while (done < n) {
    if (!st.chunk) {
      RecordChunk* chunk = nullptr;
      if (!pool_.pop(chunk)) { a_xruns_.fetch_add(1, std::memory_order_relaxed); st.rec_pos += n - done; return; }
      chunk->used = 0;
      st.chunk = chunk;
      st.chunk_start = st.rec_pos;
    }
    const int room = static_cast<int>(RecordChunk::kCapacity - st.chunk->used);
    const int m = std::min(room, n - done);
    if (src) std::memcpy(st.chunk->data.data() + st.chunk->used, src + done, static_cast<std::size_t>(m) * sizeof(float));
    else std::memset(st.chunk->data.data() + st.chunk->used, 0, static_cast<std::size_t>(m) * sizeof(float));
    st.chunk->used += m;
    st.rec_pos += m;
    done += m;
    if (st.chunk->used >= RecordChunk::kCapacity) flush_recording(st, static_cast<int>(&st - slots_.data()));
  }
}

// ─── RT: audio regions ───────────────────────────────────────────────────────

void Session::read_audio_regions(const RenderTrack& t, float* l, float* r, int n) {
  const std::int64_t p0 = pos_, p1 = pos_ + n;
  for (const RenderRegion& rg : t.regions) {
    if (rg.end <= p0 || rg.start >= p1) continue;
    const std::int64_t a = std::max(rg.start, p0), b = std::min(rg.end, p1);
    const SourceAudio& src = *rg.src;
    const std::int64_t len = src.length();
    const float* c0 = src.ch[0].data();
    const float* c1 = src.channels() > 1 ? src.ch[1].data() : c0;
    const bool resample = src.sample_rate != sample_rate_ && src.sample_rate > 0;
    const double ratio = resample ? static_cast<double>(src.sample_rate) / static_cast<double>(sample_rate_) : 1.0;
    const std::int64_t region_len = rg.end - rg.start;

    for (std::int64_t i = a; i < b; ++i) {
      const std::int64_t rel = i - rg.start;  // sample within region
      float g = rg.gain;
      if (rg.fade_in > 0 && rel < rg.fade_in) g *= static_cast<float>(rel) / static_cast<float>(rg.fade_in);
      if (rg.fade_out > 0 && rel >= region_len - rg.fade_out)
        g *= static_cast<float>(region_len - rel) / static_cast<float>(rg.fade_out);

      float sl, sr;
      if (!resample) {
        const std::int64_t si = rg.offset + rel;
        if (si < 0 || si >= len) continue;
        sl = c0[si];
        sr = c1[si];
      } else {
        const double sp = static_cast<double>(rg.offset) + static_cast<double>(rel) * ratio;
        const std::int64_t si = static_cast<std::int64_t>(sp);
        if (si < 0 || si + 1 >= len) continue;
        const float f = static_cast<float>(sp - static_cast<double>(si));
        sl = c0[si] + (c0[si + 1] - c0[si]) * f;
        sr = c1[si] + (c1[si + 1] - c1[si]) * f;
      }
      const std::size_t o = static_cast<std::size_t>(i - p0);
      l[o] += sl * g;
      r[o] += sr * g;
    }
  }
}

// ─── RT: instruments ─────────────────────────────────────────────────────────

void Session::render_instrument(const RenderTrack& t, SlotState& st, float* l, float* r, int n) {
  // Gather sequenced events inside this segment (sample-accurate offsets).
  Event events[kMaxEvents];
  int count = 0;
  if (playing_) {
    const std::int64_t p0 = pos_, p1 = pos_ + n;
    // notes are sorted by start; find first with end > p0 via linear scan from a lower bound
    auto lo = std::lower_bound(t.notes.begin(), t.notes.end(), p0 - sample_rate_ * 60,  // notes ≤ 60 s long
                               [](const RenderNote& a, std::int64_t v) { return a.start < v; });
    for (auto it = lo; it != t.notes.end() && count < kMaxEvents - 1; ++it) {
      if (it->start >= p1) break;
      if (it->start >= p0) events[count++] = {static_cast<int>(it->start - p0), true, it->pitch, it->vel};
      if (it->end >= p0 && it->end < p1) events[count++] = {static_cast<int>(it->end - p0), false, it->pitch, 0};
    }
    std::sort(events, events + count, [](const Event& a, const Event& b) {
      return a.offset != b.offset ? a.offset < b.offset : (!a.on && b.on);  // offs before ons at same sample
    });
  }
  int done = 0;
  for (int e = 0; e <= count; ++e) {
    const int until = e < count ? events[e].offset : n;
    if (until > done) { st.synth.render_add(l + done, until - done); done = until; }
    if (e < count) {
      if (events[e].on) st.synth.note_on(events[e].pitch, events[e].vel);
      else st.synth.note_off(events[e].pitch);
    }
  }
  std::memcpy(r, l, static_cast<std::size_t>(n) * sizeof(float));
}

// ─── RT: metronome ───────────────────────────────────────────────────────────

void Session::render_metronome(const RenderSnapshot* s, float* l, float* r, int n) {
  if (!s || s->spb_num <= 0) return;
  // next beat index k with beat_sample(k) >= pos_, beat_sample(k) = round(k * num / den)
  auto beat_sample = [&](std::int64_t k) -> std::int64_t {
    const domain::wide_int num = static_cast<domain::wide_int>(k) * s->spb_num;
    return static_cast<std::int64_t>((num + s->spb_den / 2) / s->spb_den);
  };
  if (metro_next_beat_ < 0) {
    std::int64_t k = static_cast<std::int64_t>((static_cast<domain::wide_int>(pos_) * s->spb_den) / s->spb_num);
    while (beat_sample(k) < pos_) ++k;
    while (k > 0 && beat_sample(k - 1) >= pos_) --k;
    metro_next_beat_ = k;
  }
  std::int64_t next = beat_sample(metro_next_beat_);
  for (int i = 0; i < n; ++i) {
    const std::int64_t here = pos_ + i;
    if (here == next) {
      const bool downbeat = s->sig_num > 0 && (metro_next_beat_ % s->sig_num) == 0;
      metro_remaining_ = static_cast<int>(sample_rate_ / 40);  // 25 ms
      metro_phase_ = 0.f;
      metro_inc_ = (downbeat ? 1600.f : 1000.f) / static_cast<float>(sample_rate_);
      ++metro_next_beat_;
      next = beat_sample(metro_next_beat_);
    }
    if (metro_remaining_ > 0) {
      const float env = static_cast<float>(metro_remaining_) / static_cast<float>(sample_rate_ / 40);
      const float v = std::sin(metro_phase_ * 6.2831853f) * env * env * 0.5f;
      metro_phase_ += metro_inc_;
      if (metro_phase_ >= 1.f) metro_phase_ -= 1.f;
      --metro_remaining_;
      l[i] += v;
      r[i] += v;
    }
  }
}

// ─── RT: block ───────────────────────────────────────────────────────────────

void Session::process(const float* const* in, int in_channels, float* const* out, int out_channels, int frames) {
  const auto t_start = std::chrono::steady_clock::now();
  for (int c = 0; c < out_channels; ++c) std::memset(out[c], 0, static_cast<std::size_t>(frames) * sizeof(float));

  const RenderSnapshot* s = snapshot_.rt_acquire();
  if (s && s->serial != seen_serial_) adopt_snapshot(*s);
  drain_commands(s);

  if (s && frames > 0) {
    int offset = 0;
    while (offset < frames) {
      int n = std::min(frames - offset, max_block_);
      if (playing_ && loop_on_ && pos_ < loop_end_ && pos_ + n > loop_end_) n = static_cast<int>(loop_end_ - pos_);
      render_segment(*s, in, in_channels, out, out_channels, offset, n);
      if (playing_) {
        pos_ += n;
        if (loop_on_ && pos_ >= loop_end_) { pos_ = loop_start_; metro_next_beat_ = -1; all_notes_off(false); }
      }
      offset += n;
    }
  }

  // master meter
  float ml = 0.f, mr = 0.f;
  if (out_channels > 0) for (int i = 0; i < frames; ++i) ml = std::max(ml, std::fabs(out[0][i]));
  if (out_channels > 1) for (int i = 0; i < frames; ++i) mr = std::max(mr, std::fabs(out[1][i]));
  const float decay = std::pow(meter_decay_, static_cast<float>(frames));
  master_peak_[0].store(std::max(master_peak_[0].load(std::memory_order_relaxed) * decay, ml), std::memory_order_relaxed);
  master_peak_[1].store(std::max(master_peak_[1].load(std::memory_order_relaxed) * decay, mr), std::memory_order_relaxed);

  a_playing_.store(playing_, std::memory_order_relaxed);
  a_recording_.store(recording_, std::memory_order_relaxed);
  a_loop_.store(loop_on_, std::memory_order_relaxed);
  a_metro_.store(metronome_, std::memory_order_relaxed);
  a_pos_.store(pos_, std::memory_order_relaxed);
  a_loop_start_.store(loop_start_, std::memory_order_relaxed);
  a_loop_end_.store(loop_end_, std::memory_order_relaxed);
  snapshot_.rt_release();

  const auto t_end = std::chrono::steady_clock::now();
  const double us = std::chrono::duration<double, std::micro>(t_end - t_start).count();
  const double budget_us = frames > 0 ? 1e6 * static_cast<double>(frames) / static_cast<double>(sample_rate_) : 1.0;
  const float load = static_cast<float>(us / budget_us);
  a_cpu_.store(0.9f * a_cpu_.load(std::memory_order_relaxed) + 0.1f * load, std::memory_order_relaxed);
}

void Session::render_segment(const RenderSnapshot& s, const float* const* in, int in_ch, float* const* out, int out_ch,
                             int offset, int n) {
  const std::size_t stride = static_cast<std::size_t>(max_block_);
  auto acc_l = [&](int slot) { return acc_.data() + (static_cast<std::size_t>(slot) * 2) * stride; };
  auto acc_r = [&](int slot) { return acc_.data() + (static_cast<std::size_t>(slot) * 2 + 1) * stride; };

  for (const RenderTrack& t : s.tracks) {
    std::fill(acc_l(t.slot), acc_l(t.slot) + n, 0.0);
    std::fill(acc_r(t.slot), acc_r(t.slot) + n, 0.0);
  }

  bool any_solo = false;
  for (const RenderTrack& t : s.tracks)
    if (t.kind != domain::TrackKind::kMaster && slots_[static_cast<std::size_t>(t.slot)].solo) { any_solo = true; break; }

  float* l = scratch_.data();
  float* r = scratch_.data() + stride;
  const float decay = std::pow(meter_decay_, static_cast<float>(n));

  for (const RenderTrack& t : s.tracks) {
    SlotState& st = slots_[static_cast<std::size_t>(t.slot)];
    std::fill(l, l + n, 0.f);
    std::fill(r, r + n, 0.f);

    switch (t.kind) {
      case domain::TrackKind::kAudio: {
        const bool live = st.arm && (st.monitor || recording_);
        if (playing_ && !(live && recording_)) read_audio_regions(t, l, r, n);  // input replaces playback while printing
        const float* src = (in && t.input_channel >= 0 && t.input_channel < in_ch) ? in[t.input_channel] + offset : nullptr;
        if (live && src) for (int i = 0; i < n; ++i) { l[i] += src[i]; r[i] += src[i]; }
        if (st.recording && recording_ && playing_) capture_input(st, src, n);
        break;
      }
      case domain::TrackKind::kInstrument:
        render_instrument(t, st, l, r, n);
        break;
      case domain::TrackKind::kAux:
      case domain::TrackKind::kBus:
      case domain::TrackKind::kMaster:
        finalize_bus(acc_l(t.slot), l, static_cast<std::size_t>(n));
        finalize_bus(acc_r(t.slot), r, static_cast<std::size_t>(n));
        break;
      case domain::TrackKind::kVca:
        continue;
    }

    // pre-fader sends (MIX-06: pre taps post-inserts, pre-fader)
    for (const RenderSend& snd : t.sends)
      if (snd.pre && snd.dest_slot >= 0) {
        accumulate_bus(l, acc_l(snd.dest_slot), static_cast<std::size_t>(n), snd.gain);
        accumulate_bus(r, acc_r(snd.dest_slot), static_cast<std::size_t>(n), snd.gain);
      }

    // fader + pan + mute/solo, 5 ms dezippered (MIX-03, MIX-10, AUT-02)
    const bool audible = !st.mute && (t.kind == domain::TrackKind::kMaster || t.kind == domain::TrackKind::kBus ||
                                      t.kind == domain::TrackKind::kAux || !any_solo || st.solo);
    const double g = audible ? db_to_gain(fader_db(st.fader_pos)) : 0.0;
    // MIX-10: mono strips → constant-power pan law (−3 dB center); stereo strips → balance.
    StereoGain pan;
    if (t.stereo) pan = {std::min(1.0, 1.0 - st.pan), std::min(1.0, 1.0 + st.pan)};
    else pan = constant_power_pan(st.pan);
    const float tl = static_cast<float>(g * pan.left), tr = static_cast<float>(g * pan.right);
    if (!st.gains_primed) { st.gl = tl; st.gr = tr; st.gains_primed = true; }
    float pk_l = 0.f, pk_r = 0.f;
    for (int i = 0; i < n; ++i) {
      st.gl += smooth_coef_ * (tl - st.gl);
      st.gr += smooth_coef_ * (tr - st.gr);
      l[i] *= st.gl;
      r[i] *= st.gr;
      pk_l = std::max(pk_l, std::fabs(l[i]));
      pk_r = std::max(pk_r, std::fabs(r[i]));
    }
    st.hold_l = std::max(st.hold_l * decay, pk_l);
    st.hold_r = std::max(st.hold_r * decay, pk_r);
    peak_[static_cast<std::size_t>(t.slot) * 2].store(st.hold_l, std::memory_order_relaxed);
    peak_[static_cast<std::size_t>(t.slot) * 2 + 1].store(st.hold_r, std::memory_order_relaxed);
    if (pk_l > 1.f || pk_r > 1.f) clip_[static_cast<std::size_t>(t.slot)].store(true, std::memory_order_relaxed);

    // post-fader sends
    for (const RenderSend& snd : t.sends)
      if (!snd.pre && snd.dest_slot >= 0) {
        accumulate_bus(l, acc_l(snd.dest_slot), static_cast<std::size_t>(n), snd.gain);
        accumulate_bus(r, acc_r(snd.dest_slot), static_cast<std::size_t>(n), snd.gain);
      }

    // output (MIX-01: → bus / master / hardware)
    if (t.kind == domain::TrackKind::kMaster || (t.out_slot < 0 && s.master_slot < 0)) {
      if (out_ch == 1) for (int i = 0; i < n; ++i) out[0][offset + i] += 0.5f * (l[i] + r[i]);
      else if (out_ch >= 2) for (int i = 0; i < n; ++i) { out[0][offset + i] += l[i]; out[1][offset + i] += r[i]; }
    } else {
      const int dest = t.out_slot >= 0 ? t.out_slot : s.master_slot;
      accumulate_bus(l, acc_l(dest), static_cast<std::size_t>(n), 1.0);
      accumulate_bus(r, acc_r(dest), static_cast<std::size_t>(n), 1.0);
    }
  }

  if (metronome_ && playing_ && out_ch >= 1) {
    std::fill(l, l + n, 0.f);
    std::fill(r, r + n, 0.f);
    render_metronome(&s, l, r, n);
    for (int i = 0; i < n; ++i) out[0][offset + i] += l[i];
    if (out_ch >= 2) for (int i = 0; i < n; ++i) out[1][offset + i] += r[i];
  }
}

// ─── Offline bounce ──────────────────────────────────────────────────────────

std::vector<std::vector<float>> render_offline(std::unique_ptr<RenderSnapshot> snapshot, std::int64_t start,
                                               std::int64_t end, int block) {
  std::vector<std::vector<float>> result(2);
  if (!snapshot || end <= start) return result;
  Session s;
  s.prepare(snapshot->sample_rate, block);
  s.publish(std::move(snapshot));
  s.locate(start);
  s.play();
  const std::size_t total = static_cast<std::size_t>(end - start);
  result[0].resize(total);
  result[1].resize(total);
  std::vector<float> bl(static_cast<std::size_t>(block)), br(static_cast<std::size_t>(block));
  float* outs[2] = {bl.data(), br.data()};
  std::size_t done = 0;
  while (done < total) {
    const int n = static_cast<int>(std::min<std::size_t>(static_cast<std::size_t>(block), total - done));
    s.process(nullptr, 0, outs, 2, n);
    std::copy(bl.begin(), bl.begin() + n, result[0].begin() + static_cast<std::ptrdiff_t>(done));
    std::copy(br.begin(), br.begin() + n, result[1].begin() + static_cast<std::ptrdiff_t>(done));
    done += static_cast<std::size_t>(n);
  }
  return result;
}

}  // namespace s7::engine
