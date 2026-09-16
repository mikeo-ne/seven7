// seven7 — Application layer: Controller implementation (see controller.h)

#include "app/controller.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

#include "engine/mix_math.h"
#include "pal/project_codec.h"
#include "pal/wav_file.h"

namespace s7::app {

namespace fs = std::filesystem;
using pal::Json;

Controller::Controller() {
  sched_.sample_rate = 48000;
  sched_.hardware_buffer = 256;
  new_demo_project();
}

void Controller::prepare(std::int64_t sample_rate, int max_block) {
  sched_.sample_rate = sample_rate;
  sched_.hardware_buffer = max_block;
  session_.prepare(sample_rate, max_block);
  project_.sample_rate = sample_rate;
  microfade_samples_ = sample_rate / 100;
  commit();
}

// ─── project construction ────────────────────────────────────────────────────

void Controller::new_empty_project() {
  project_ = domain::Project{};
  sources_ = engine::SourceStore{};
  project_.sample_rate = sched_.sample_rate;
  project_.name = "Untitled";
  domain::add_track(project_, domain::TrackKind::kMaster, "Master");
  undo_ = domain::UndoStack{};
  bundle_path_.clear();
  commit();
}

void Controller::add_demo_content() {
  using namespace domain;
  Project& p = project_;
  const std::int64_t sr = p.sample_rate;
  const std::int64_t beat = (sr * 60 * p.tempo.den) / p.tempo.num;  // integer @120 → exact
  const std::int64_t bar = beat * 4;
  const std::int64_t tick_beat = kTicksPerQuarter;

  auto midi_region = [&](Id track, const char* name, std::int64_t start_bar, std::int64_t bars, int color) {
    Region r;
    r.id = p.allocate_id();
    r.track_id = track;
    r.kind = RegionKind::kMidi;
    r.name = name;
    r.color = color;
    r.start_sample = start_bar * bar;
    r.length_samples = bars * bar;
    p.regions.push_back(r);
    return &p.regions.back();
  };
  auto note = [&](Region* r, double beat_pos, double beats, int pitch, int vel) {
    Note n;
    n.tick = static_cast<std::int64_t>(beat_pos * static_cast<double>(tick_beat));
    n.length_ticks = static_cast<std::int64_t>(beats * static_cast<double>(tick_beat));
    n.sample = static_cast<std::int64_t>(beat_pos * static_cast<double>(beat));
    n.length_samples = static_cast<std::int64_t>(beats * static_cast<double>(beat));
    n.pitch = static_cast<std::uint8_t>(pitch);
    n.velocity = static_cast<std::uint8_t>(vel);
    r->notes.push_back(n);
  };

  const Id drums = add_track(p, TrackKind::kInstrument, "Drums", 0);
  const Id bass = add_track(p, TrackKind::kInstrument, "Sub Bass", 4);
  const Id keys = add_track(p, TrackKind::kInstrument, "Prism Keys", 8);
  const Id pad = add_track(p, TrackKind::kInstrument, "Halo Pad", 13);
  const Id vox = add_track(p, TrackKind::kAudio, "Vox Ld", 17);
  const Id bus = add_track(p, TrackKind::kBus, "Inst Bus", 20);
  const Id verb = add_track(p, TrackKind::kAux, "Verb Return", 22);
  p.find_track(drums)->instrument = 5;
  p.find_track(bass)->instrument = 0;
  p.find_track(keys)->instrument = 1;
  p.find_track(pad)->instrument = 2;
  p.find_track(bass)->fader_pos = 0.70;
  p.find_track(pad)->fader_pos = 0.62;
  p.find_track(keys)->fader_pos = 0.68;
  p.find_track(drums)->fader_pos = 0.72;
  p.find_track(verb)->fader_pos = 0.55;
  p.find_track(keys)->pan = -0.2;
  p.find_track(pad)->pan = 0.25;
  for (Id t : {drums, bass, keys, pad}) p.find_track(t)->output = bus;
  p.find_track(keys)->sends[0] = Send{verb, -9.0, false, false, true};
  p.find_track(pad)->sends[0] = Send{verb, -6.0, false, false, true};
  p.find_track(vox)->inserts[0] = {"S7 EQ-8", false, 0};
  p.find_track(vox)->inserts[1] = {"S7 Comp", false, 0};
  p.find_track(bus)->inserts[0] = {"S7 TapeSat", false, 0};
  p.find_track(pad)->inserts[0] = {"S7 Filter-6", false, 0};
  p.find_track(vox)->arm = false;
  p.find_track(vox)->input_channel = 0;

  // Drums: 8 bars, 4-on-the-floor + snare 2/4 + 8th hats
  for (int b = 0; b < 2; ++b) {
    Region* r = midi_region(drums, b == 0 ? "Beat A" : "Beat B", b * 4, 4, -1);
    for (int bar_i = 0; bar_i < 4; ++bar_i) {
      const double o = bar_i * 4.0;
      for (int k = 0; k < 4; ++k) note(r, o + k, 0.25, 36, k == 0 ? 120 : 100);
      note(r, o + 1, 0.25, 38, 110);
      note(r, o + 3, 0.25, 38, 112);
      for (int h = 0; h < 8; ++h) note(r, o + h * 0.5, 0.2, 42, h % 2 ? 60 : 84);
      if (bar_i == 3 && b == 1) { note(r, o + 3.5, 0.25, 39, 100); note(r, o + 3.75, 0.25, 46, 90); }
    }
  }
  // Bass: root movement Am – F – C – G (A1 F1 C2 G1)
  {
    Region* r = midi_region(bass, "Bass Line", 0, 8, -1);
    const int roots[4] = {33, 29, 36, 31};
    for (int bar_i = 0; bar_i < 8; ++bar_i) {
      const double o = bar_i * 4.0;
      const int root = roots[bar_i % 4];
      note(r, o + 0, 0.75, root, 110);
      note(r, o + 1.5, 0.5, root, 90);
      note(r, o + 2.5, 0.5, root + 7, 95);
      note(r, o + 3.0, 0.75, root, 105);
      note(r, o + 3.75, 0.25, root + 12, 80);
    }
  }
  // Keys: chords on the off-beats
  {
    Region* r = midi_region(keys, "Chords", 0, 8, -1);
    const int chords[4][3] = {{57, 60, 64}, {53, 57, 60}, {55, 60, 64}, {55, 59, 62}};
    for (int bar_i = 0; bar_i < 8; ++bar_i) {
      const double o = bar_i * 4.0;
      const auto& c = chords[bar_i % 4];
      for (double pos : {1.5, 3.5}) for (int nn : c) note(r, o + pos, 0.45, nn + 12, 78);
    }
  }
  // Pad: long chords, enters at bar 4
  {
    Region* r = midi_region(pad, "Pad Swell", 4, 4, -1);
    const int chords[4][3] = {{57, 60, 64}, {53, 57, 60}, {55, 60, 64}, {55, 59, 62}};
    for (int bar_i = 0; bar_i < 4; ++bar_i) for (int nn : chords[bar_i]) note(r, bar_i * 4.0, 3.9, nn + 24, 64);
  }
  // Vox Ld: a synthesized lead-vocal take (bars 5–8) so the audio path — waveform
  // peaks, clip gain, fades, micro-fades on split — is exercised out of the box.
  {
    auto audio = std::make_shared<engine::SourceAudio>();
    audio->sample_rate = sr;
    const std::int64_t len = 4 * bar;
    audio->ch.assign(1, std::vector<float>(static_cast<std::size_t>(len), 0.f));
    std::vector<float>& x = audio->ch[0];
    struct Ph { double beat, len; int pitch; };
    const Ph phrase[] = {{0, 1, 76}, {1, 1, 74}, {2, 1, 72}, {3, 1, 69}, {4, 1, 72}, {5, 1, 74}, {6, 2, 76},
                         {8, 1, 79}, {9, 1, 76}, {10, 1, 74}, {11, 1, 72}, {12, 1, 74}, {13, 1, 72}, {14, 1.75, 69}};
    const double two_pi = 6.283185307179586;
    std::uint32_t rng = 0x5EED5EEDu;
    auto noise = [&] { rng = rng * 1664525u + 1013904223u; return (static_cast<double>(rng >> 8) / 16777216.0) * 2.0 - 1.0; };
    for (const Ph& ph : phrase) {
      const std::int64_t n0 = static_cast<std::int64_t>(ph.beat * static_cast<double>(beat));
      const std::int64_t n1 = std::min(len, n0 + static_cast<std::int64_t>(ph.len * static_cast<double>(beat) * 0.92));
      const double f0 = 440.0 * std::pow(2.0, (ph.pitch - 69) / 12.0);
      double phase = 0.0, lp = 0.0;
      for (std::int64_t n = n0; n < n1; ++n) {
        const double t = static_cast<double>(n - n0) / static_cast<double>(sr);
        const double dur = static_cast<double>(n1 - n0) / static_cast<double>(sr);
        const double env = std::min(1.0, t / 0.03) * std::min(1.0, (dur - t) / 0.12) * (0.85 + 0.15 * std::sin(two_pi * 0.7 * t));
        const double vib = t > 0.15 ? 0.02 * std::sin(two_pi * 5.5 * (t - 0.15)) * std::min(1.0, (t - 0.15) / 0.3) : 0.0;
        phase += two_pi * f0 * (1.0 + vib) / static_cast<double>(sr);
        double v = std::sin(phase) + 0.5 * std::sin(2 * phase) + 0.33 * std::sin(3 * phase + 0.4) + 0.18 * std::sin(4 * phase) + 0.1 * std::sin(5 * phase);
        lp += 0.02 * (noise() - lp);  // breath: low-passed noise
        v = 0.30 * v * env + 0.03 * lp * env;
        x[static_cast<std::size_t>(n)] += static_cast<float>(v);
      }
    }
    Source s;
    s.id = p.allocate_id();
    s.name = "Vox Ld Comp.wav";
    s.length_samples = len;
    s.channels = 1;
    s.sample_rate = sr;
    p.sources.push_back(s);
    sources_.add(s.id, audio);
    Region r;
    r.id = p.allocate_id();
    r.track_id = vox;
    r.kind = RegionKind::kAudio;
    r.source_id = s.id;
    r.name = "Vox Ld Comp";
    r.start_sample = 4 * bar;
    r.length_samples = len;
    r.fade_in_samples = 480;
    r.fade_out_samples = 4800;
    p.regions.push_back(r);
  }
  p.markers.push_back({p.allocate_id(), "Intro", 0});
  p.markers.push_back({p.allocate_id(), "Verse", 4 * bar});
  p.markers.push_back({p.allocate_id(), "End", 8 * bar});
}

void Controller::new_demo_project() {
  project_ = domain::Project{};
  sources_ = engine::SourceStore{};
  project_.sample_rate = sched_.sample_rate;
  project_.name = "Midnight City";
  project_.key = "Am";
  add_demo_content();
  domain::add_track(project_, domain::TrackKind::kMaster, "Master");
  undo_ = domain::UndoStack{};
  bundle_path_.clear();
  commit();
}

// ─── publish ─────────────────────────────────────────────────────────────────

void Controller::assign_slots() {
  int slot = 0;
  for (auto& t : project_.tracks) t.rt_slot = (t.kind == domain::TrackKind::kVca) ? -1 : slot++;
}

void Controller::sync_track_params(const domain::Track& t) {
  if (t.rt_slot < 0) return;
  session_.set_param(t.rt_slot, engine::Command::kFader, t.fader_pos);
  session_.set_param(t.rt_slot, engine::Command::kPan, t.pan);
  session_.set_param(t.rt_slot, engine::Command::kMute, t.mute ? 1.0 : 0.0);
  session_.set_param(t.rt_slot, engine::Command::kSolo, t.solo ? 1.0 : 0.0);
  session_.set_param(t.rt_slot, engine::Command::kArm, t.arm ? 1.0 : 0.0);
  session_.set_param(t.rt_slot, engine::Command::kMonitor, t.monitor ? 1.0 : 0.0);
}

void Controller::push_all_params() {
  for (const auto& t : project_.tracks) sync_track_params(t);
}

void Controller::commit() {
  assign_slots();
  for (auto& t : project_.tracks) {
    t.reported_latency = 0;
    for (const auto& i : t.inserts) if (!i.bypassed) t.reported_latency += i.latency_samples;
  }
  engine::CompileReport rep;
  session_.publish(engine::compile_snapshot(project_, sources_, sched_, &rep));
  if (rep.warnings != last_warnings_) {
    for (const auto& w : rep.warnings) if (on_log) on_log(w);
    last_warnings_ = rep.warnings;
  }
  ++state_version_;
}

void Controller::begin_edit(const std::string& label) { undo_.push(project_, label); }

// ─── project I/O ─────────────────────────────────────────────────────────────

bool Controller::save_bundle(const std::string& dir, std::string* error) {
  std::error_code ec;
  fs::create_directories(fs::path(dir) / "Media" / "Audio Files", ec);
  if (ec) { if (error) *error = "cannot create bundle: " + ec.message(); return false; }
  // The bundle name is the project name (Logic/PT convention) — apply before serializing.
  const std::string name = fs::path(dir).stem().string();
  if (!name.empty() && project_.name != name) { project_.name = name; ++state_version_; }
  {
    std::ofstream m(fs::path(dir) / "manifest.json");
    m << pal::manifest_json().dump(2);
  }
  {
    std::ofstream p(fs::path(dir) / "project.json");
    if (!p) { if (error) *error = "cannot write project.json"; return false; }
    p << pal::project_to_json(project_).dump(2);
  }
  for (const auto& s : project_.sources) {
    const fs::path target = fs::path(dir) / "Media" / "Audio Files" / s.name;
    if (fs::exists(target, ec)) continue;
    if (auto audio = sources_.get(s.id)) pal::write_wav(target.string(), *audio, 24, 0);
  }
  bundle_path_ = dir;
  if (on_log) on_log("saved " + dir);
  return true;
}

bool Controller::load_bundle(const std::string& dir, std::string* error) {
  std::ifstream pf(fs::path(dir) / "project.json");
  if (!pf) { if (error) *error = "no project.json in " + dir; return false; }
  const std::string text((std::istreambuf_iterator<char>(pf)), std::istreambuf_iterator<char>());
  Json j;
  std::string err;
  if (!Json::parse(text, j, &err)) { if (error) *error = "project.json: " + err; return false; }
  domain::Project loaded;
  if (!pal::project_from_json(j, loaded, &err)) { if (error) *error = err; return false; }

  engine::SourceStore store;
  for (const auto& s : loaded.sources) {
    auto audio = std::make_shared<engine::SourceAudio>();
    pal::WavInfo info;
    const fs::path path = fs::path(dir) / "Media" / "Audio Files" / s.name;
    if (pal::read_wav(path.string(), *audio, info)) store.add(s.id, audio);
    else if (on_log) on_log("source offline: " + s.name + " (" + info.error + ")");
  }
  session_.stop();
  project_ = std::move(loaded);
  if (project_.sample_rate != sched_.sample_rate) {
    if (on_log) on_log("project rate " + std::to_string(project_.sample_rate) + " != device rate " + std::to_string(sched_.sample_rate) + " — sources are resampled on playback");
  }
  sources_ = std::move(store);
  undo_ = domain::UndoStack{};
  bundle_path_ = dir;
  commit();
  push_all_params();
  if (on_log) on_log("opened " + dir);
  return true;
}

bool Controller::import_audio_file(const std::string& path, domain::Id track, std::int64_t at_sample, std::string* error) {
  auto audio = std::make_shared<engine::SourceAudio>();
  pal::WavInfo info;
  if (!pal::read_wav(path, *audio, info)) { if (error) *error = info.error; return false; }
  begin_edit("Import Audio");
  domain::Source s;
  s.id = project_.allocate_id();
  s.name = fs::path(path).filename().string();
  s.length_samples = audio->length();
  s.channels = audio->channels();
  s.sample_rate = audio->sample_rate;
  // Ensure unique media name inside the bundle
  int n = 1;
  const std::string base = fs::path(s.name).stem().string(), ext = fs::path(s.name).extension().string();
  while (std::any_of(project_.sources.begin(), project_.sources.end(), [&](const domain::Source& x) { return x.name == s.name; }))
    s.name = base + "-" + std::to_string(++n) + ext;
  project_.sources.push_back(s);
  sources_.add(s.id, audio);
  if (track == domain::kNoId || !project_.find_track(track))
    track = domain::add_track(project_, domain::TrackKind::kAudio, base, static_cast<int>(project_.tracks.size() * 5 % 24));
  domain::Region r;
  r.id = project_.allocate_id();
  r.track_id = track;
  r.source_id = s.id;
  r.name = base;
  r.start_sample = std::max<std::int64_t>(0, at_sample);
  // Region length is in *project* samples; convert when the file rate differs.
  r.length_samples = audio->sample_rate == project_.sample_rate
      ? audio->length()
      : static_cast<std::int64_t>(static_cast<double>(audio->length()) * static_cast<double>(project_.sample_rate) / static_cast<double>(audio->sample_rate));
  project_.regions.push_back(r);
  commit();
  if (on_log) on_log("imported " + s.name + " (" + std::to_string(info.channels) + " ch, " + std::to_string(info.sample_rate) + " Hz, " + std::to_string(info.bits) + (info.is_float ? "f" : "") + ")");
  return true;
}

std::string Controller::next_take_name(const domain::Track& t) const {
  int n = 1;
  for (const auto& s : project_.sources) if (s.name.rfind(t.name + " take", 0) == 0) ++n;
  return t.name + " take " + std::to_string(n) + ".wav";
}

bool Controller::tick() {
  bool changed = false;
  session_.service([&](engine::FinishedTake take) {
    const domain::Track* trk = nullptr;
    for (const auto& t : project_.tracks) if (t.rt_slot == take.slot) { trk = &t; break; }
    if (!trk || !take.audio || take.audio->length() == 0) return;
    begin_edit("Record");
    domain::Source s;
    s.id = project_.allocate_id();
    s.name = next_take_name(*trk);
    s.length_samples = take.audio->length();
    s.channels = take.audio->channels();
    s.sample_rate = take.audio->sample_rate;
    project_.sources.push_back(s);
    sources_.add(s.id, take.audio);
    if (!bundle_path_.empty()) {
      std::error_code ec;
      fs::create_directories(fs::path(bundle_path_) / "Media" / "Audio Files", ec);
      pal::write_wav((fs::path(bundle_path_) / "Media" / "Audio Files" / s.name).string(), *take.audio, 24, take.start_sample);
    }
    domain::Region r;
    r.id = project_.allocate_id();
    r.track_id = trk->id;
    r.source_id = s.id;
    r.name = fs::path(s.name).stem().string();
    r.start_sample = take.start_sample;
    r.length_samples = take.audio->length();
    if (auto_microfade_) { r.fade_in_samples = std::min(microfade_samples_, r.length_samples / 2); r.fade_out_samples = r.fade_in_samples; }
    project_.regions.push_back(r);
    changed = true;
    if (on_log) on_log("recorded " + s.name + " @ " + std::to_string(take.start_sample));
  });
  if (changed) commit();
  return changed;
}

// ─── JSON: state / status / peaks ───────────────────────────────────────────

Json Controller::state_json() const {
  Json j = pal::project_to_json(project_);
  j["version"] = static_cast<std::int64_t>(state_version_);
  j["bundle_path"] = bundle_path_;
  j["can_undo"] = undo_.can_undo();
  j["can_redo"] = undo_.can_redo();
  j["undo_label"] = undo_.undo_label();
  j["redo_label"] = undo_.redo_label();
  j["edit_mode"] = static_cast<int>(edit_mode_);
  j["content_end"] = project_.content_end_sample();
  // per-track engine slots + latency readouts (MIX-07 exact values)
  Json slots = Json::array();
  for (const auto& t : project_.tracks) slots.push(Json{{"id", Json(t.id)}, {"slot", Json(t.rt_slot)}, {"latency", Json(t.reported_latency)}});
  j["slots"] = slots;
  Json presets = Json::array();
  for (const auto& p : engine::kSynthPresets) presets.push(Json(p.name));
  j["instrument_presets"] = presets;
  return j;
}

Json Controller::status_json() const {
  const engine::TransportStatus t = session_.transport();
  Json j = Json::object();
  j["playing"] = t.playing;
  j["recording"] = t.recording;
  j["loop"] = t.loop_on;
  j["metronome"] = t.metronome;
  j["playhead"] = t.playhead;
  j["loop_start"] = t.loop_start;
  j["loop_end"] = t.loop_end;
  j["cpu"] = static_cast<double>(t.cpu_load);
  j["xruns"] = static_cast<std::int64_t>(t.xruns);
  j["rt_tracks"] = t.rt_tracks;
  j["mae_tracks"] = t.mae_tracks;
  j["pdc_worst"] = t.pdc_worst;
  j["sample_rate"] = session_.sample_rate();
  j["buffer"] = sched_.hardware_buffer;
  j["version"] = static_cast<std::int64_t>(state_version_);
  Json meters = Json::array();
  for (const auto& trk : project_.tracks) {
    if (trk.rt_slot < 0) { meters.push(Json::array({Json(0.0), Json(0.0), Json(false)})); continue; }
    meters.push(Json::array({Json(static_cast<double>(session_.peak(trk.rt_slot, 0))), Json(static_cast<double>(session_.peak(trk.rt_slot, 1))), Json(session_.clip(trk.rt_slot))}));
  }
  j["meters"] = meters;
  j["master"] = Json::array({Json(static_cast<double>(session_.master_peak(0))), Json(static_cast<double>(session_.master_peak(1)))});
  return j;
}

Json Controller::peaks_json(domain::Id source, std::int64_t from, std::int64_t to, int buckets) const {
  Json j = Json::object();
  Json data = Json::array();
  const std::vector<float>* pk = sources_.peaks(source);
  auto audio = sources_.get(source);
  if (!pk || !audio || buckets <= 0 || to <= from) { j["peaks"] = data; return j; }
  const int chs = audio->channels();
  const std::int64_t blocks = static_cast<std::int64_t>(pk->size()) / (2 * chs);
  const std::int64_t pb = engine::SourceStore::kPeakBlock;
  for (int b = 0; b < buckets; ++b) {
    const std::int64_t s0 = from + (to - from) * b / buckets;
    const std::int64_t s1 = std::max(s0 + 1, from + (to - from) * (b + 1) / buckets);
    float mn = 0.f, mx = 0.f;
    if (s1 - s0 < pb * 2) {
      // fine: read samples directly
      const auto& ch = audio->ch[0];
      const std::int64_t a = std::max<std::int64_t>(0, s0), e = std::min<std::int64_t>(audio->length(), s1);
      if (e > a) { mn = 1.f; mx = -1.f; }
      for (std::int64_t i = a; i < e; ++i) { mn = std::min(mn, ch[static_cast<std::size_t>(i)]); mx = std::max(mx, ch[static_cast<std::size_t>(i)]); }
      if (e <= a) { mn = 0.f; mx = 0.f; }
    } else {
      const std::int64_t b0 = std::max<std::int64_t>(0, s0 / pb), b1 = std::min(blocks, (s1 + pb - 1) / pb);
      if (b1 > b0) { mn = 1.f; mx = -1.f; }
      for (std::int64_t k = b0; k < b1; ++k) {
        for (int c = 0; c < chs; ++c) {
          const std::size_t o = static_cast<std::size_t>((k * chs + c) * 2);
          mn = std::min(mn, (*pk)[o]);
          mx = std::max(mx, (*pk)[o + 1]);
        }
      }
      if (b1 <= b0) { mn = 0.f; mx = 0.f; }
    }
    data.push(Json(static_cast<double>(std::round(mn * 1000.f) / 1000.f)));
    data.push(Json(static_cast<double>(std::round(mx * 1000.f) / 1000.f)));
  }
  j["source"] = source;
  j["from"] = from;
  j["to"] = to;
  j["peaks"] = data;
  return j;
}

// ─── JSON: commands ──────────────────────────────────────────────────────────

Json Controller::command_text(const std::string& json_text) {
  Json j;
  std::string err;
  if (!Json::parse(json_text, j, &err)) return Json{{"ok", Json(false)}, {"error", Json("bad JSON: " + err)}};
  return command(j);
}

Json Controller::command(const Json& c) {
  using namespace domain;
  const std::string op = c["op"].as_string();
  auto ok = [&](Json extra = Json::object()) { extra["ok"] = true; extra["version"] = static_cast<std::int64_t>(state_version_); return extra; };
  auto fail = [&](const std::string& m) { return Json{{"ok", Json(false)}, {"error", Json(m)}}; };
  const std::int64_t sr = project_.sample_rate;

  // ── transport ──
  if (op == "play") { session_.play(); return ok(); }
  if (op == "stop") { session_.stop(); return ok(); }
  if (op == "toggle_play") { if (session_.transport().playing) session_.stop(); else session_.play(); return ok(); }
  if (op == "locate") { session_.locate(c["sample"].as_int()); return ok(); }
  if (op == "return_to_zero") { session_.locate(0); return ok(); }
  if (op == "record") { session_.set_record(c["on"].as_bool(true)); return ok(); }
  if (op == "loop") { session_.set_loop(c["on"].as_bool(), c["start"].as_int(), c["end"].as_int()); return ok(); }
  if (op == "metronome") { session_.set_metronome(c["on"].as_bool()); return ok(); }
  if (op == "panic") { engine::Command cmd{engine::Command::kAllNotesOff}; session_.send(cmd); return ok(); }

  // ── live MIDI (musical typing / on-screen keyboard) ──
  if (op == "note_on" || op == "note_off") {
    const Track* t = project_.find_track(id_of(c["track"]));
    if (!t || t->rt_slot < 0) return fail("no such track");
    if (op == "note_on") session_.note_on(t->rt_slot, static_cast<int>(c["pitch"].as_int(60)), static_cast<int>(c["velocity"].as_int(100)));
    else session_.note_off(t->rt_slot, static_cast<int>(c["pitch"].as_int(60)));
    return ok();
  }

  // ── mixer (live: ring command first, project second — no undo entry per move) ──
  if (op == "set_track") {
    Track* t = project_.find_track(id_of(c["track"]));
    if (!t) return fail("no such track");
    const std::string what = c["param"].as_string();
    // Switch params arrive as JSON booleans from the UI, numbers from scripts — accept both.
    const double v = c["value"].is_bool() ? (c["value"].as_bool() ? 1.0 : 0.0) : c["value"].as_double();
    bool structural = false;
    if (what == "fader") t->fader_pos = std::clamp(v, 0.0, 1.0);
    else if (what == "pan") t->pan = std::clamp(v, -1.0, 1.0);
    else if (what == "mute") t->mute = v != 0;
    else if (what == "solo") t->solo = v != 0;
    else if (what == "arm") { t->arm = v != 0; structural = true; }
    else if (what == "monitor") { t->monitor = v != 0; structural = true; }
    else if (what == "name") { begin_edit("Rename Track"); t->name = c["value"].as_string(); structural = true; }
    else if (what == "color") { t->color = static_cast<int>(v); ++state_version_; return ok(); }
    else if (what == "instrument") { t->instrument = static_cast<int>(v); if (t->rt_slot >= 0) session_.set_preset(t->rt_slot, t->instrument); structural = true; }
    else if (what == "input") { t->input_channel = static_cast<int>(v); structural = true; }
    else if (what == "output") { begin_edit("Route Output"); t->output = id_of(c["value"]); structural = true; }
    else if (what == "automation_mode") { t->automation_mode = c["value"].as_string("read"); ++state_version_; return ok(); }
    else return fail("unknown track param " + what);
    sync_track_params(*t);
    if (structural) commit(); else ++state_version_;
    return ok();
  }
  if (op == "set_send") {
    Track* t = project_.find_track(id_of(c["track"]));
    const std::size_t idx = static_cast<std::size_t>(c["index"].as_int());
    if (!t || idx >= t->sends.size()) return fail("no such send");
    begin_edit("Edit Send");
    Send& s = t->sends[idx];
    if (c.has("dest")) { s.dest_track = id_of(c["dest"]); s.active = s.dest_track != kNoId; }
    if (c.has("level_db")) s.level_db = std::clamp(c["level_db"].as_double(), -144.0, 12.0);
    if (c.has("pre")) s.pre = c["pre"].as_bool();
    if (c.has("muted")) s.muted = c["muted"].as_bool();
    commit();
    return ok();
  }
  if (op == "set_insert") {
    Track* t = project_.find_track(id_of(c["track"]));
    const std::size_t idx = static_cast<std::size_t>(c["index"].as_int());
    if (!t || idx >= t->inserts.size()) return fail("no such insert slot");
    begin_edit("Edit Insert");
    Insert& i = t->inserts[idx];
    if (c.has("name")) i.name = c["name"].as_string();
    if (c.has("bypassed")) i.bypassed = c["bypassed"].as_bool();
    if (c.has("latency")) i.latency_samples = std::max<std::int64_t>(0, c["latency"].as_int());
    commit();
    return ok();
  }

  // ── tracks ──
  if (op == "add_track") {
    begin_edit("Add Track");
    const std::string kind = c["kind"].as_string("audio");
    const Id id = add_track(project_, pal::track_kind_from(kind), c["name"].as_string(kind == "instrument" ? "Inst" : (kind == "audio" ? "Audio" : kind)),
                            static_cast<int>(c["color"].as_int(static_cast<std::int64_t>(project_.tracks.size() * 5 % 24))));
    if (kind == "instrument") project_.find_track(id)->instrument = static_cast<int>(c["instrument"].as_int(1));
    commit();
    sync_track_params(*project_.find_track(id));
    return ok(Json{{"track", Json(id)}});
  }
  if (op == "remove_track") {
    begin_edit("Delete Track");
    if (!remove_track(project_, id_of(c["track"]))) { undo_.undo(project_); return fail("cannot remove track"); }
    commit();
    return ok();
  }
  if (op == "reorder_track") {
    const Id id = id_of(c["track"]);
    const std::size_t to = static_cast<std::size_t>(std::max<std::int64_t>(0, c["index"].as_int()));
    auto it = std::find_if(project_.tracks.begin(), project_.tracks.end(), [&](const Track& t) { return t.id == id; });
    if (it == project_.tracks.end() || it->kind == TrackKind::kMaster) return fail("cannot move");
    begin_edit("Reorder Tracks");
    Track moved = *it;
    project_.tracks.erase(it);
    const std::size_t limit = project_.tracks.size() - 1;  // keep master last
    project_.tracks.insert(project_.tracks.begin() + static_cast<std::ptrdiff_t>(std::min(to, limit)), moved);
    commit();
    return ok();
  }

  // ── regions (EDT-02 battery) ──
  if (op == "split") {
    begin_edit("Split");
    const Id right = split_region(project_, id_of(c["region"]), c["sample"].as_int(), auto_microfade_ ? microfade_samples_ : 0);
    if (right == kNoId) { undo_.undo(project_); return fail("split point outside region"); }
    commit();
    return ok(Json{{"region", Json(right)}});
  }
  if (op == "split_at_playhead") {
    const std::int64_t at = session_.transport().playhead;
    begin_edit("Split at Playhead");
    std::vector<Id> made;
    for (const Json& rid : c["regions"].as_array()) {
      const Id r = split_region(project_, id_of(rid), at, auto_microfade_ ? microfade_samples_ : 0);
      if (r != kNoId) made.push_back(r);
    }
    if (made.empty()) { undo_.undo(project_); return fail("playhead not inside selected regions"); }
    commit();
    Json arr = Json::array();
    for (Id r : made) arr.push(Json(r));
    return ok(Json{{"regions", arr}});
  }
  if (op == "move") {
    begin_edit("Move");
    for (const Json& m : c["moves"].as_array()) {
      if (!move_region(project_, id_of(m["region"]), m["start"].as_int(), m.has("track") ? id_of(m["track"]) : kNoId)) { undo_.undo(project_); return fail("bad move"); }
    }
    if (c.has("region")) move_region(project_, id_of(c["region"]), c["start"].as_int(), c.has("track") ? id_of(c["track"]) : kNoId);
    commit();
    return ok();
  }
  if (op == "nudge") {
    begin_edit("Nudge");
    for (const Json& rid : c["regions"].as_array()) nudge_region(project_, id_of(rid), c["delta"].as_int());
    commit();
    return ok();
  }
  if (op == "trim") {
    begin_edit("Trim");
    const Id rid = id_of(c["region"]);
    bool res = true;
    if (c.has("start")) res = trim_head(project_, rid, c["start"].as_int());
    if (res && c.has("end")) res = trim_tail(project_, rid, c["end"].as_int());
    if (!res) { undo_.undo(project_); return fail("no such region"); }
    commit();
    return ok();
  }
  if (op == "delete") {
    begin_edit("Delete");
    for (const Json& rid : c["regions"].as_array()) delete_region(project_, id_of(rid), edit_mode_);
    commit();
    return ok();
  }
  if (op == "duplicate") {
    begin_edit("Duplicate");
    Json made = Json::array();
    for (const Json& rid : c["regions"].as_array()) { const Id d = duplicate_region(project_, id_of(rid)); if (d != kNoId) made.push(Json(d)); }
    commit();
    return ok(Json{{"regions", made}});
  }
  if (op == "set_region") {
    Region* r = project_.find_region(id_of(c["region"]));
    if (!r) return fail("no such region");
    begin_edit("Edit Region");
    if (c.has("gain_db")) set_region_gain(project_, r->id, c["gain_db"].as_double());
    if (c.has("fade_in") || c.has("fade_out")) set_region_fades(project_, r->id, c["fade_in"].as_int(r->fade_in_samples), c["fade_out"].as_int(r->fade_out_samples));
    if (c.has("muted")) r->muted = c["muted"].as_bool();
    if (c.has("name")) r->name = c["name"].as_string();
    if (c.has("color")) r->color = static_cast<int>(c["color"].as_int(-1));
    commit();
    return ok();
  }
  if (op == "add_midi_region") {
    Track* t = project_.find_track(id_of(c["track"]));
    if (!t) return fail("no such track");
    begin_edit("Create MIDI Region");
    Region r;
    r.id = project_.allocate_id();
    r.track_id = t->id;
    r.kind = RegionKind::kMidi;
    r.name = c["name"].as_string("MIDI");
    r.start_sample = std::max<std::int64_t>(0, c["start"].as_int());
    r.length_samples = std::max<std::int64_t>(1, c["length"].as_int(sr * 2));
    project_.regions.push_back(r);
    commit();
    return ok(Json{{"region", Json(r.id)}});
  }
  if (op == "set_notes") {
    Region* r = project_.find_region(id_of(c["region"]));
    if (!r || r->kind != RegionKind::kMidi) return fail("not a MIDI region");
    begin_edit("Edit Notes");
    const TempoMap map = project_.tempo_map();
    const std::int64_t base_tick = map.sample_to_tick(r->start_sample);
    r->notes.clear();
    for (const Json& n : c["notes"].as_array()) {
      Note nt;
      nt.sample = std::max<std::int64_t>(0, n["sample"].as_int());
      nt.length_samples = std::max<std::int64_t>(1, n["length"].as_int(sr / 4));
      nt.tick = map.sample_to_tick(r->start_sample + nt.sample) - base_tick;
      nt.length_ticks = map.sample_to_tick(r->start_sample + nt.sample + nt.length_samples) - base_tick - nt.tick;
      nt.pitch = static_cast<std::uint8_t>(std::clamp<std::int64_t>(n["pitch"].as_int(60), 0, 127));
      nt.velocity = static_cast<std::uint8_t>(std::clamp<std::int64_t>(n["velocity"].as_int(100), 1, 127));
      r->notes.push_back(nt);
    }
    commit();
    return ok();
  }

  // ── snap / edit mode ──
  if (op == "snap") {
    SnapGrid g;
    const std::string unit = c["unit"].as_string("beat");
    g.unit = unit == "bar" ? SnapUnit::kBar : unit == "beat" ? SnapUnit::kBeat : unit == "division" ? SnapUnit::kDivision
           : unit == "samples" ? SnapUnit::kSamples : unit == "seconds" ? SnapUnit::kSeconds : unit == "frames" ? SnapUnit::kFrames : SnapUnit::kOff;
    g.division = static_cast<int>(c["division"].as_int(4));
    g.samples = c["samples"].as_int(1);
    g.fps = c["fps"].as_double(24.0);
    return ok(Json{{"sample", Json(snap_sample(project_, g, c["sample"].as_int()))}, {"step", Json(grid_step_samples(project_, g))}});
  }
  if (op == "edit_mode") {
    const std::string m = c["mode"].as_string("slip");
    edit_mode_ = m == "grid" ? EditMode::kGrid : m == "shuffle" ? EditMode::kShuffle : m == "spot" ? EditMode::kSpot : EditMode::kSlip;
    ++state_version_;
    return ok();
  }

  // ── project ──
  if (op == "set_project") {
    begin_edit("Project Settings");
    if (c.has("name")) project_.name = c["name"].as_string();
    if (c.has("tempo_num") && c.has("tempo_den")) { const std::int64_t n = c["tempo_num"].as_int(), d = c["tempo_den"].as_int(); if (n > 0 && d > 0) project_.tempo = {n, d}; }
    else if (c.has("bpm")) { const double bpm = c["bpm"].as_double(120.0); if (bpm >= 20 && bpm <= 500) project_.tempo = {static_cast<std::int64_t>(std::llround(bpm * 1000.0)), 1000}; }
    if (c.has("sig_num")) project_.sig_num = static_cast<int>(std::clamp<std::int64_t>(c["sig_num"].as_int(4), 1, 32));
    if (c.has("sig_den")) project_.sig_den = static_cast<int>(std::clamp<std::int64_t>(c["sig_den"].as_int(4), 1, 32));
    if (c.has("key")) project_.key = c["key"].as_string("C");
    commit();
    return ok();
  }
  if (op == "add_marker") {
    begin_edit("Add Marker");
    project_.markers.push_back({project_.allocate_id(), c["name"].as_string("Marker"), c["sample"].as_int(session_.transport().playhead)});
    commit();
    return ok();
  }
  if (op == "remove_marker") {
    begin_edit("Delete Marker");
    const Id id = id_of(c["marker"]);
    project_.markers.erase(std::remove_if(project_.markers.begin(), project_.markers.end(), [&](const Marker& m) { return m.id == id; }), project_.markers.end());
    commit();
    return ok();
  }
  if (op == "undo") { if (!undo_.undo(project_)) return fail("nothing to undo"); commit(); push_all_params(); return ok(); }
  if (op == "redo") { if (!undo_.redo(project_)) return fail("nothing to redo"); commit(); push_all_params(); return ok(); }
  if (op == "new_project") { if (c["demo"].as_bool(false)) new_demo_project(); else new_empty_project(); push_all_params(); return ok(); }
  if (op == "save") { std::string err; const std::string dir = c["path"].as_string(bundle_path_); if (dir.empty()) return fail("no path"); if (!save_bundle(dir, &err)) return fail(err); return ok(); }
  if (op == "open") { std::string err; if (!load_bundle(c["path"].as_string(), &err)) return fail(err); return ok(); }
  if (op == "import_audio") { std::string err; if (!import_audio_file(c["path"].as_string(), id_of(c["track"]), c["sample"].as_int(), &err)) return fail(err); return ok(); }
  if (op == "get_state") return state_json();
  if (op == "get_status") return status_json();
  if (op == "get_peaks") return peaks_json(id_of(c["source"]), c["from"].as_int(), c["to"].as_int(), static_cast<int>(c["buckets"].as_int(512)));

  return fail("unknown op '" + op + "'");
}

}  // namespace s7::app
