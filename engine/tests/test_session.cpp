// seven7 — session renderer tests
//   ARC-ENG-02  sample accuracy of region bounds / fades / notes in the render
//   ARC-RT-02   RCU snapshot swap while "audio" is running
//   ARC-RT-03   parameter events via lock-free ring
//   MIX-01/05   routing: sends, buses, cycle rejection at compile
//   MIX-13      offline bounce determinism

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include "domain/edit_engine.h"
#include "engine/rt_primitives.h"
#include "engine/session.h"
#include "s7_test.h"

using namespace s7::domain;
using namespace s7::engine;

namespace {

constexpr double kC = 0.70710678118654752;  // MIX-10 constant-power center (−3 dB)

std::shared_ptr<SourceAudio> dc_source(std::int64_t len, float value, int channels = 1) {
  auto a = std::make_shared<SourceAudio>();
  a->sample_rate = 48000;
  a->ch.assign(static_cast<std::size_t>(channels), std::vector<float>(static_cast<std::size_t>(len), value));
  return a;
}

struct Fixture {
  Project p;
  SourceStore store;
  SchedulerConfig sched;
  Id src = kNoId, vox = kNoId, master = kNoId;

  Fixture() {
    p.sample_rate = 48000;
    Source s;
    s.id = p.allocate_id();
    s.length_samples = 48000;
    p.sources.push_back(s);
    src = s.id;
    store.add(src, dc_source(48000, 0.5f));
    vox = add_track(p, TrackKind::kAudio, "Vox");
    master = add_track(p, TrackKind::kMaster, "Master");
    assign_slots();
  }
  void assign_slots() {
    int slot = 0;
    for (auto& t : p.tracks) t.rt_slot = slot++;
  }
  Id region(Id track, std::int64_t start, std::int64_t len, std::int64_t offset = 0) {
    Region r;
    r.id = p.allocate_id();
    r.track_id = track;
    r.source_id = src;
    r.start_sample = start;
    r.offset_sample = offset;
    r.length_samples = len;
    p.regions.push_back(r);
    return r.id;
  }
  std::unique_ptr<RenderSnapshot> compile(CompileReport* rep = nullptr) { return compile_snapshot(p, store, sched, rep); }
};

// Unity fader, mono source at center: constant-power law → 0.5 × cos(π/4) per side.
void region_bounds_are_sample_exact() {
  Fixture f;
  f.region(f.vox, 1000, 500);
  auto out = render_offline(f.compile(), 0, 2000, 128);
  S7_CHECK(out[0].size() == 2000);
  S7_CHECK(std::fabs(out[0][999]) < 1e-9);             // one sample before the region: silence
  S7_CHECK_NEAR(out[0][1000], 0.5 * kC, 1e-4);        // first sample of region
  S7_CHECK_NEAR(out[0][1499], 0.5 * kC, 1e-4);        // last sample of region
  S7_CHECK(std::fabs(out[0][1500]) < 1e-9);            // first sample after: silence
  S7_CHECK_NEAR(out[1][1250], 0.5 * kC, 1e-4);        // right channel identical (mono source, center)
}

void fades_and_clip_gain_are_applied_per_sample() {
  Fixture f;
  const Id rid = f.region(f.vox, 0, 1000);
  set_region_fades(f.p, rid, 100, 200);
  set_region_gain(f.p, rid, -6.0);
  auto out = render_offline(f.compile(), 0, 1000, 256);
  const double g = std::pow(10.0, -6.0 / 20.0) * 0.5 * kC;
  S7_CHECK(std::fabs(out[0][0]) < 1e-9);                // fade-in starts at 0
  S7_CHECK_NEAR(out[0][50], g * 0.5, 2e-3);            // linear fade midpoint
  S7_CHECK_NEAR(out[0][500], g, 2e-3);                 // plateau at clip gain
  S7_CHECK_NEAR(out[0][900], g * 0.5, 2e-3);           // fade-out midpoint (1000-200=800 → 900 is half)
  S7_CHECK_NEAR(out[0][999], g * (1.0 / 200.0), 2e-3); // last sample nearly silent
}

void loop_wraps_sample_accurately() {
  Fixture f;
  f.region(f.vox, 0, 100);  // 100 samples of 0.5 then silence
  Session s;
  s.prepare(48000, 64);
  s.publish(f.compile());
  s.set_loop(true, 0, 150);
  s.locate(0);
  s.play();
  std::vector<float> l(1000), r(1000);
  float* outs[2] = {l.data(), r.data()};
  // Render in blocks of 64 → the loop boundary (150) falls mid-block; the session must split the block.
  std::vector<float> all;
  for (int i = 0; i < 6; ++i) {
    s.process(nullptr, 0, outs, 2, 64);
    all.insert(all.end(), l.begin(), l.begin() + 64);
  }
  // Timeline: [0,100) audio, [100,150) silence, wrap, [150,250) audio again...
  S7_CHECK_NEAR(all[99], 0.5 * kC, 1e-3);
  S7_CHECK(std::fabs(all[120]) < 1e-6);
  S7_CHECK_NEAR(all[150], 0.5 * kC, 1e-3);   // content restarts exactly at the wrap sample
  S7_CHECK_NEAR(all[249], 0.5 * kC, 1e-3);
  S7_CHECK(std::fabs(all[260]) < 1e-6);
  const auto t = s.transport();
  S7_CHECK(t.playing && t.loop_on);
  S7_CHECK(t.playhead == (6 * 64) % 150);
}

void instrument_notes_are_sample_accurate() {
  Fixture f;
  const Id inst = add_track(f.p, TrackKind::kInstrument, "Keys");
  f.assign_slots();
  Region r;
  r.id = f.p.allocate_id();
  r.track_id = inst;
  r.kind = RegionKind::kMidi;
  r.start_sample = 1000;
  r.length_samples = 10000;
  Note n;
  n.sample = 500;         // absolute 1500
  n.length_samples = 2000;
  n.pitch = 69;
  n.velocity = 127;
  r.notes.push_back(n);
  f.p.regions.push_back(r);
  f.p.find_track(inst)->instrument = 3;  // S7 Pluck: 2 ms attack, sustain 0
  auto out = render_offline(f.compile(), 0, 14000, 100);
  double before = 0, during = 0, after = 0;
  for (int i = 1300; i < 1500; ++i) before += std::fabs(out[0][i]);
  for (int i = 1500; i < 1900; ++i) during += std::fabs(out[0][i]);
  for (int i = 12000; i < 14000; ++i) after += std::fabs(out[0][i]);
  S7_CHECK(before == 0.0);                 // strictly nothing before the note-on sample
  S7_CHECK(during > 1.0);                  // audible during
  S7_CHECK(std::fabs(out[0][1499]) == 0.0);
  S7_CHECK(std::fabs(out[0][1500 + 60]) > 0.0);  // ringing within the first ms after onset
  S7_CHECK(after == 0.0);                  // note-off at 3500 + 150 ms release → fully idle
}

void routing_sends_buses_and_cycles() {
  Fixture f;
  const Id bus = add_track(f.p, TrackKind::kBus, "Vox Bus");
  const Id aux = add_track(f.p, TrackKind::kAux, "Verb");
  f.assign_slots();
  f.region(f.vox, 0, 1000);
  Track* vox = f.p.find_track(f.vox);
  vox->output = bus;
  vox->sends[0] = Send{aux, 0.0, false, false, true};   // post-fader send at 0 dB
  f.p.find_track(aux)->fader_pos = 0.005;                // −144 dB → effectively mute the aux return
  CompileReport rep;
  auto snap = f.compile(&rep);
  S7_CHECK(rep.warnings.empty());
  auto out = render_offline(std::move(snap), 0, 1000, 250);
  S7_CHECK_NEAR(out[0][600], 0.5 * kC, 1e-3);            // vox (pan law) → bus (balance) → master

  // Aux return audible when its fader is up: send doubles the energy at master.
  f.p.find_track(aux)->fader_pos = 0.75;
  out = render_offline(f.compile(), 0, 1000, 250);
  S7_CHECK_NEAR(out[0][600], 1.0 * kC, 2e-3);

  // Cycle: bus → aux → bus must be rejected at compile (MIX-05), with a warning.
  f.p.find_track(bus)->output = aux;
  f.p.find_track(aux)->output = bus;
  rep = CompileReport{};
  snap = f.compile(&rep);
  S7_CHECK(!rep.warnings.empty());
  out = render_offline(std::move(snap), 0, 1000, 250);
  S7_CHECK(std::isfinite(out[0][600]));                   // graph still renders (no feedback blow-up)
}

void mute_solo_fader_pan_via_commands() {
  Fixture f;
  f.region(f.vox, 0, 48000);
  Session s;
  s.prepare(48000, 256);
  s.publish(f.compile());
  s.play();
  std::vector<float> l(256), r(256);
  float* outs[2] = {l.data(), r.data()};
  auto settle = [&] { for (int i = 0; i < 20; ++i) s.process(nullptr, 0, outs, 2, 256); };
  settle();
  S7_CHECK_NEAR(l[100], 0.5 * kC, 1e-3);
  s.set_param(0, Command::kPan, -1.0);   // hard left: full level on L, nothing on R
  settle();
  S7_CHECK_NEAR(l[100], 0.5, 1e-3);
  S7_CHECK(std::fabs(r[100]) < 1e-3);
  s.set_param(0, Command::kPan, 0.0);
  s.set_param(0, Command::kFader, 1.0);  // +12 dB
  settle();
  S7_CHECK_NEAR(l[100], 0.5 * 3.9810717 * kC, 2e-3);
  s.set_param(0, Command::kMute, 1.0);
  settle();
  S7_CHECK(std::fabs(l[100]) < 1e-4);
  s.set_param(0, Command::kMute, 0.0);
  s.set_param(0, Command::kFader, 0.75);
  // Solo on another track silences this one; solo on this track keeps it.
  const Id other = add_track(f.p, TrackKind::kAudio, "Other");
  f.assign_slots();
  s.publish(f.compile());
  s.set_param(f.p.find_track(other)->rt_slot, Command::kSolo, 1.0);
  settle();
  S7_CHECK(std::fabs(l[100]) < 1e-4);
  s.set_param(0, Command::kSolo, 1.0);
  settle();
  S7_CHECK_NEAR(l[100], 0.5 * kC, 1e-3);
  S7_CHECK(s.peak(0, 0) > 0.3f);        // meters follow
}

void recording_round_trip() {
  Fixture f;
  f.p.find_track(f.vox)->arm = true;
  Session s;
  s.prepare(48000, 128);
  s.publish(f.compile());
  s.set_param(0, Command::kArm, 1.0);
  s.locate(5000);
  s.set_record(true);
  std::vector<float> in(128), l(128), r(128);
  const float* ins[1] = {in.data()};
  float* outs[2] = {l.data(), r.data()};
  // 3 blocks of a ramp so we can verify sample order and start position.
  for (int b = 0; b < 3; ++b) {
    for (int i = 0; i < 128; ++i) in[static_cast<std::size_t>(i)] = static_cast<float>(b * 128 + i) / 1000.f;
    s.process(ins, 1, outs, 2, 128);
  }
  S7_CHECK_NEAR(l[10], (2 * 128 + 10) / 1000.0 * kC, 1e-4);     // input monitoring while printing
  s.stop();
  s.process(nullptr, 0, outs, 2, 128);
  bool got = false;
  FinishedTake take;
  s.service([&](FinishedTake t) { got = true; take = std::move(t); });
  S7_CHECK(got);
  S7_CHECK(take.slot == 0);
  S7_CHECK(take.start_sample == 5000);
  S7_CHECK(take.audio && take.audio->length() == 384);
  if (take.audio && take.audio->length() == 384) {
    S7_CHECK_NEAR(take.audio->ch[0][0], 0.0, 1e-9);
    S7_CHECK_NEAR(take.audio->ch[0][383], 383 / 1000.f, 1e-9);
  }
  S7_CHECK(!s.transport().recording);
}

void rcu_swap_under_load() {
  Fixture f;
  f.region(f.vox, 0, 48000);
  Session s;
  s.prepare(48000, 64);
  s.publish(f.compile());
  s.play();
  std::atomic<bool> stop{false};
  std::atomic<int> blocks{0};
  std::thread rt([&] {
    std::vector<float> l(64), r(64);
    float* outs[2] = {l.data(), r.data()};
    while (!stop.load()) { s.process(nullptr, 0, outs, 2, 64); blocks.fetch_add(1); }
  });
  for (int i = 0; i < 300; ++i) {
    std::string name = "T";
    name += std::to_string(i);
    add_track(f.p, TrackKind::kAudio, name);
    f.assign_slots();
    s.publish(f.compile());
    if (i % 10 == 0) s.service(nullptr);
    std::this_thread::sleep_for(std::chrono::microseconds(50));
  }
  stop.store(true);
  rt.join();
  s.service(nullptr);
  S7_CHECK(blocks.load() > 0);
  S7_CHECK(s.transport().playhead > 0);
}

void snapshot_carries_mixer_state() {
  Fixture f;
  f.region(f.vox, 0, 1000);
  Track* vox = f.p.find_track(f.vox);
  vox->fader_pos = 1.0;   // +12 dB
  vox->pan = 1.0;         // hard right
  auto out = render_offline(f.compile(), 0, 1000, 100);
  S7_CHECK(std::fabs(out[0][500]) < 1e-6);
  S7_CHECK_NEAR(out[1][500], 0.5 * 3.9810717, 2e-3);
  vox->mute = true;
  out = render_offline(f.compile(), 0, 1000, 100);
  S7_CHECK(std::fabs(out[1][500]) < 1e-6);
}

void offline_bounce_is_deterministic() {
  Fixture f;
  f.region(f.vox, 100, 30000);
  const Id inst = add_track(f.p, TrackKind::kInstrument, "Keys");
  f.assign_slots();
  Region r;
  r.id = f.p.allocate_id();
  r.track_id = inst;
  r.kind = RegionKind::kMidi;
  r.length_samples = 40000;
  for (int i = 0; i < 8; ++i) { Note n; n.sample = i * 4000; n.length_samples = 3000; n.pitch = static_cast<std::uint8_t>(60 + i); r.notes.push_back(n); }
  f.p.regions.push_back(r);
  auto a = render_offline(f.compile(), 0, 40000, 64);
  auto b = render_offline(f.compile(), 0, 40000, 512);  // different block size → identical samples
  S7_CHECK(a[0].size() == b[0].size());
  double max_diff = 0;
  for (std::size_t i = 0; i < a[0].size(); ++i) max_diff = std::max(max_diff, static_cast<double>(std::fabs(a[0][i] - b[0][i])));
  S7_CHECK(max_diff == 0.0);
}

void spsc_ring_and_rcu_primitives() {
  SpscRing<int, 8> ring;
  for (int i = 0; i < 7; ++i) S7_CHECK(ring.push(i));
  S7_CHECK(!ring.push(99));  // full at N-1
  int v = -1;
  for (int i = 0; i < 7; ++i) { S7_CHECK(ring.pop(v)); S7_CHECK(v == i); }
  S7_CHECK(!ring.pop(v));

  RcuSlot<int> slot;
  S7_CHECK(slot.control_peek() == nullptr);
  slot.publish(std::make_unique<int>(1));
  const int* p1 = slot.rt_acquire();
  S7_CHECK(p1 && *p1 == 1);
  slot.publish(std::make_unique<int>(2));   // retired: 1 (RT still may hold it)
  S7_CHECK(slot.retired_count() == 1);
  slot.rt_release();
  slot.publish(std::make_unique<int>(3));   // RT saw gen 2 → snapshot 1 reclaimable; 2 retired now
  S7_CHECK(slot.retired_count() == 1);
  S7_CHECK(*slot.control_peek() == 3);
}

}  // namespace

int main() {
  spsc_ring_and_rcu_primitives();
  region_bounds_are_sample_exact();
  fades_and_clip_gain_are_applied_per_sample();
  loop_wraps_sample_accurately();
  instrument_notes_are_sample_accurate();
  routing_sends_buses_and_cycles();
  mute_solo_fader_pan_via_commands();
  recording_round_trip();
  rcu_swap_under_load();
  snapshot_carries_mixer_state();
  offline_bounce_is_deterministic();
  return s7::test::summary("session (ARC-ENG-02 / ARC-RT-02,03 / MIX-01,05 / MIX-13)");
}
