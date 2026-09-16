// seven7 — edit engine tests (docs/04 EDT-01…05 sample-accurate battery; docs/02 UIW-07 snap)

#include <cstdint>

#include "domain/edit_engine.h"
#include "s7_test.h"

using namespace s7::domain;

namespace {

Project make_project() {
  Project p;
  p.sample_rate = 48000;
  p.tempo = {120, 1};
  Source s;
  s.id = p.allocate_id();
  s.name = "vox.wav";
  s.length_samples = 480000;  // 10 s
  p.sources.push_back(s);
  const Id t = add_track(p, TrackKind::kAudio, "Vox", 3);
  add_track(p, TrackKind::kMaster, "Master");
  Region r;
  r.id = p.allocate_id();
  r.track_id = t;
  r.source_id = s.id;
  r.name = "vox";
  r.start_sample = 48000;
  r.offset_sample = 1000;
  r.length_samples = 96000;
  r.fade_in_samples = 480;
  r.fade_out_samples = 960;
  p.regions.push_back(r);
  return p;
}

// EDT-02A: every boundary lands within 0 samples of target.
void split_is_exact() {
  Project p = make_project();
  const Id rid = p.regions[0].id;
  const Id right = split_region(p, rid, 48000 + 12345, 480);
  S7_CHECK(right != kNoId);
  const Region* l = p.find_region(rid);
  const Region* r = p.find_region(right);
  S7_CHECK(l && r);
  S7_CHECK(l->start_sample == 48000);
  S7_CHECK(l->length_samples == 12345);
  S7_CHECK(l->end_sample() == r->start_sample);          // contiguous, no gap, no overlap
  S7_CHECK(r->offset_sample == 1000 + 12345);            // source continuity: phase preserved
  S7_CHECK(r->length_samples == 96000 - 12345);
  S7_CHECK(l->fade_out_samples == 480 && r->fade_in_samples == 480);  // auto micro-fades (EDT-03)
  S7_CHECK(l->fade_in_samples == 480);                   // original head fade retained
  S7_CHECK(r->fade_out_samples == 960);                  // original tail fade retained

  // Out-of-range splits are no-ops.
  S7_CHECK(split_region(p, rid, 48000) == kNoId);
  S7_CHECK(split_region(p, rid, 48000 + 12345) == kNoId);
  S7_CHECK(p.regions.size() == 2);
}

void trims_are_exact_and_bounded() {
  Project p = make_project();
  const Id rid = p.regions[0].id;
  S7_CHECK(trim_head(p, rid, 48000 + 777));
  const Region* r = p.find_region(rid);
  S7_CHECK(r->start_sample == 48000 + 777);
  S7_CHECK(r->offset_sample == 1000 + 777);
  S7_CHECK(r->length_samples == 96000 - 777);
  S7_CHECK(r->end_sample() == 48000 + 96000);            // tail did not move

  // Head trim cannot reveal audio before the source start (offset ≥ 0).
  S7_CHECK(trim_head(p, rid, 0));
  r = p.find_region(rid);
  S7_CHECK(r->offset_sample == 0);
  S7_CHECK(r->start_sample == 48000 + 777 - (1000 + 777));

  // Tail trim is clamped to the source end.
  S7_CHECK(trim_tail(p, rid, 10'000'000));
  r = p.find_region(rid);
  S7_CHECK(r->offset_sample + r->length_samples == 480000);
  // and to at least one sample
  S7_CHECK(trim_tail(p, rid, r->start_sample - 5));
  r = p.find_region(rid);
  S7_CHECK(r->length_samples == 1);
}

void nudge_move_duplicate_delete() {
  Project p = make_project();
  const Id rid = p.regions[0].id;
  S7_CHECK(nudge_region(p, rid, 1));
  S7_CHECK(p.find_region(rid)->start_sample == 48001);   // 1-sample nudge (EDT-02 ladder)
  S7_CHECK(nudge_region(p, rid, -100000));
  S7_CHECK(p.find_region(rid)->start_sample == 0);       // clamps at session start
  S7_CHECK(move_region(p, rid, 96000));
  const Id dup = duplicate_region(p, rid);
  S7_CHECK(dup != kNoId);
  S7_CHECK(p.find_region(dup)->start_sample == 96000 + 96000);  // butt-spliced after original
  const Id dup2 = duplicate_region(p, dup);

  // Shuffle delete closes the gap for later regions on the track (EDT-02 "Shuffle close-gap").
  S7_CHECK(delete_region(p, dup, EditMode::kShuffle));
  S7_CHECK(p.find_region(dup) == nullptr);
  S7_CHECK(p.find_region(dup2)->start_sample == 96000 + 96000);
  // Slip delete leaves others untouched.
  const Id dup3 = duplicate_region(p, dup2);
  S7_CHECK(delete_region(p, dup2, EditMode::kSlip));
  S7_CHECK(p.find_region(dup3)->start_sample == 96000 + 96000 + 96000);
}

void clip_gain_and_fades() {
  Project p = make_project();
  const Id rid = p.regions[0].id;
  S7_CHECK(set_region_gain(p, rid, -6.04));
  S7_CHECK_NEAR(p.find_region(rid)->gain_db, -6.0, 1e-12);   // 0.1 dB steps (EDT-05)
  S7_CHECK(set_region_gain(p, rid, 99.0));
  S7_CHECK_NEAR(p.find_region(rid)->gain_db, 24.0, 1e-12);   // clamp +24
  S7_CHECK(set_region_gain(p, rid, -99.0));
  S7_CHECK_NEAR(p.find_region(rid)->gain_db, -60.0, 1e-12);  // clamp −60
  S7_CHECK(set_region_fades(p, rid, 1'000'000, -5));
  S7_CHECK(p.find_region(rid)->fade_in_samples == 96000);    // clamp to region length
  S7_CHECK(p.find_region(rid)->fade_out_samples == 0);
}

// UIW-07: snap grid. Bar/beat/division go through the tempo map; samples are exact.
void snap_grid() {
  Project p = make_project();  // 120 BPM @ 48k → beat = 24000 samples, bar (4/4) = 96000
  SnapGrid g;
  g.unit = SnapUnit::kBeat;
  S7_CHECK(grid_step_samples(p, g) == 24000);
  S7_CHECK(snap_sample(p, g, 24000 + 11999) == 24000);
  S7_CHECK(snap_sample(p, g, 24000 + 12001) == 48000);
  g.unit = SnapUnit::kBar;
  S7_CHECK(grid_step_samples(p, g) == 96000);
  S7_CHECK(snap_sample(p, g, 100000) == 96000);
  g.unit = SnapUnit::kDivision;
  g.division = 4;  // 1/16
  S7_CHECK(grid_step_samples(p, g) == 6000);
  S7_CHECK(snap_sample(p, g, 6000 * 7 + 100) == 42000);
  g.unit = SnapUnit::kSamples;
  g.samples = 1;
  for (std::int64_t s : {0LL, 1LL, 123457LL, 99999999LL}) S7_CHECK(snap_sample(p, g, s) == s);  // tolerance exactly 0
  g.samples = 100;
  S7_CHECK(snap_sample(p, g, 149) == 100);
  S7_CHECK(snap_sample(p, g, 150) == 200);
  g.unit = SnapUnit::kOff;
  S7_CHECK(snap_sample(p, g, 777) == 777);

  // Non-integer beat length: 121 BPM @ 44.1k → 44100*60/121 = 21867.77…; grid stays drift-free.
  p.sample_rate = 44100;
  p.tempo = {121, 1};
  g.unit = SnapUnit::kBeat;
  const TempoMap map = p.tempo_map();
  for (std::int64_t beat = 0; beat < 5000; beat += 37) {
    const std::int64_t exact = map.tick_to_sample(beat * kTicksPerQuarter);
    S7_CHECK(snap_sample(p, g, exact + 3) == exact);
  }
}

void undo_redo_snapshots() {
  Project p = make_project();
  UndoStack undo(3);
  const Id rid = p.regions[0].id;
  undo.push(p, "Split");
  split_region(p, rid, 60000);
  S7_CHECK(p.regions.size() == 2);
  undo.push(p, "Nudge");
  nudge_region(p, rid, 10);
  S7_CHECK(undo.undo_label() == "Nudge");
  S7_CHECK(undo.undo(p));
  S7_CHECK(p.find_region(rid)->start_sample == 48000);
  S7_CHECK(p.regions.size() == 2);
  S7_CHECK(undo.undo(p));
  S7_CHECK(p.regions.size() == 1);
  S7_CHECK(!undo.can_undo());
  S7_CHECK(undo.redo(p));
  S7_CHECK(p.regions.size() == 2);
  S7_CHECK(undo.redo(p));
  S7_CHECK(p.find_region(rid)->start_sample == 48010);
  S7_CHECK(!undo.can_redo());
  // capacity is enforced
  for (int i = 0; i < 10; ++i) { undo.push(p, "x"); nudge_region(p, rid, 1); }
  S7_CHECK(undo.depth() == 3);
}

void track_ops() {
  Project p = make_project();
  S7_CHECK(p.tracks.back().kind == TrackKind::kMaster);
  const Id bus = add_track(p, TrackKind::kBus, "Drum Bus");
  S7_CHECK(p.tracks.back().kind == TrackKind::kMaster);  // master stays last
  S7_CHECK(p.tracks[p.tracks.size() - 2].id == bus);
  p.tracks[0].output = bus;
  p.tracks[0].sends[0].dest_track = bus;
  p.tracks[0].sends[0].active = true;
  S7_CHECK(remove_track(p, bus));
  S7_CHECK(p.tracks[0].output == kNoId);                 // dangling routes are cleared
  S7_CHECK(!p.tracks[0].sends[0].active);
  S7_CHECK(!remove_track(p, p.master()->id));            // master cannot be removed
  const Id vox = p.tracks[0].id;
  S7_CHECK(remove_track(p, vox));
  S7_CHECK(p.regions.empty());                           // regions go with the track
}

}  // namespace

int main() {
  split_is_exact();
  trims_are_exact_and_bounded();
  nudge_move_duplicate_delete();
  clip_gain_and_fades();
  snap_grid();
  undo_redo_snapshots();
  track_ops();
  return s7::test::summary("edit_engine (EDT-02A / UIW-07 / ARC-MDL undo)");
}
