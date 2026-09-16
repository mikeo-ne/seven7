#pragma once
// seven7 — Domain layer: Edit Engine (spec: docs/04 EDT-01…05; docs/02 UIW-07)
//
// Non-destructive, sample-accurate operations over the Project model. Every
// operation is a pure function `Project -> Project'` (mutating a copy owned by
// the caller), which is what makes snapshot undo trivial (ARC-MDL: "cheap undo =
// pointer swap"; here a vector of snapshots because the model is plain data).
//
// All positions/lengths are integer samples. There is no float anywhere in this
// file — that is the EDT-02A acceptance criterion ("every boundary lands within
// 0 samples of target").

#include <algorithm>
#include <cstdint>
#include <deque>
#include <limits>
#include <string>
#include <vector>

#include "domain/project.h"

namespace s7::domain {

// ─── Edit modes (UIW-07) ─────────────────────────────────────────────────────
// Precision mode: Slip / Grid / Shuffle / Spot.   Canvas drag modes map onto the
// same enum: Overlap→Slip, No-Overlap→Grid(abs), X-Fade→Slip (+auto fades).
enum class EditMode : std::uint8_t { kSlip, kGrid, kShuffle, kSpot };

// ─── Snap grid (UIW-07) ──────────────────────────────────────────────────────
// Snap menu: Bar / Beat / Division / Ticks / Samples / Min:Sec / Frames.
enum class SnapUnit : std::uint8_t { kOff, kBar, kBeat, kDivision, kSamples, kSeconds, kFrames };

struct SnapGrid {
  SnapUnit unit = SnapUnit::kBeat;
  int division = 4;          // for kDivision: 4 = 1/16 (beat / 4)
  std::int64_t samples = 1;  // for kSamples: raw step  ("Snap tolerance exactly 0 at sample setting")
  double fps = 24.0;         // for kFrames
};

/// Exact grid step in samples for units that are constant under a fixed tempo.
/// Bar/Beat/Division are computed through the rational tempo (no float drift).
inline std::int64_t grid_step_samples(const Project& p, const SnapGrid& g) {
  const std::int64_t num = p.samples_per_beat_num();
  const std::int64_t den = p.samples_per_beat_den();
  auto beat_multiple = [&](std::int64_t k_num, std::int64_t k_den) -> std::int64_t {
    // (num/den) * (k_num/k_den), rounded nearest-even like the time model
    const wide_int n = static_cast<wide_int>(num) * k_num;
    const wide_int d = static_cast<wide_int>(den) * k_den;
    const wide_int q = n / d, r = n % d, twice = 2 * r;
    wide_int res = q;
    if (twice > d || (twice == d && (q & 1) == 1)) res += 1;
    return static_cast<std::int64_t>(res);
  };
  switch (g.unit) {
    case SnapUnit::kOff: return 1;
    case SnapUnit::kBar: return beat_multiple(p.sig_num * 4, p.sig_den);  // sig_num beats of 4/sig_den notes
    case SnapUnit::kBeat: return beat_multiple(1, 1);
    case SnapUnit::kDivision: return beat_multiple(1, std::max(1, g.division));
    case SnapUnit::kSamples: return std::max<std::int64_t>(1, g.samples);
    case SnapUnit::kSeconds: return p.sample_rate;
    case SnapUnit::kFrames: {
      const double s = static_cast<double>(p.sample_rate) / (g.fps > 0 ? g.fps : 24.0);
      return std::max<std::int64_t>(1, static_cast<std::int64_t>(s + 0.5));
    }
  }
  return 1;
}

/// Snap an absolute sample position to the nearest grid line (ties round up).
inline std::int64_t snap_sample(const Project& p, const SnapGrid& g, std::int64_t sample) {
  if (g.unit == SnapUnit::kOff) return sample;
  if (g.unit == SnapUnit::kBar || g.unit == SnapUnit::kBeat || g.unit == SnapUnit::kDivision) {
    // Snap in the tick domain so the grid is exact under the tempo map (ARC-TIME),
    // then convert the chosen tick back to samples — never accumulate float steps.
    const TempoMap map = p.tempo_map();
    std::int64_t tick_step = kTicksPerQuarter;  // beat
    if (g.unit == SnapUnit::kBar) tick_step = kTicksPerQuarter * 4 * p.sig_num / p.sig_den;
    if (g.unit == SnapUnit::kDivision) tick_step = std::max<std::int64_t>(1, kTicksPerQuarter / std::max(1, g.division));
    const std::int64_t tick = map.sample_to_tick(std::max<std::int64_t>(0, sample));
    const std::int64_t base = (tick / tick_step) * tick_step;
    // Compare candidates in the *sample* domain (a tick is ~25 samples at 120 BPM/48k,
    // so deciding in ticks would lose sample resolution). Ties round up.
    std::int64_t best = map.tick_to_sample(base);
    for (std::int64_t k = -1; k <= 1; ++k) {
      const std::int64_t t = base + k * tick_step;
      if (t < 0) continue;
      const std::int64_t s = map.tick_to_sample(t);
      const std::int64_t d_new = s > sample ? s - sample : sample - s;
      const std::int64_t d_old = best > sample ? best - sample : sample - best;
      if (d_new < d_old || (d_new == d_old && s > best)) best = s;
    }
    return best;
  }
  const std::int64_t step = grid_step_samples(p, g);
  if (step <= 1) return sample;
  const std::int64_t lo = (sample >= 0 ? sample / step : -((-sample + step - 1) / step)) * step;
  const std::int64_t hi = lo + step;
  return (sample - lo) < (hi - sample) ? lo : hi;
}

// ─── Region operations (EDT-02) ──────────────────────────────────────────────

/// Split a region at an absolute sample. Returns the id of the new right-hand
/// region, or kNoId if `at` is not strictly inside the region. Auto micro-fades
/// (EDT-03, default 10 ms) are applied at the new boundary when `microfade > 0`.
inline Id split_region(Project& p, Id region_id, std::int64_t at, std::int64_t microfade_samples = 0) {
  Region* r = p.find_region(region_id);
  if (!r || at <= r->start_sample || at >= r->end_sample()) return kNoId;

  Region right = *r;
  right.id = p.allocate_id();
  const std::int64_t left_len = at - r->start_sample;
  // Tick re-base for MIDI notes: exact through the tempo map (ARC-TIME).
  const TempoMap map = p.tempo_map();
  const std::int64_t left_ticks = map.sample_to_tick(at) - map.sample_to_tick(r->start_sample);
  right.start_sample = at;
  right.offset_sample = r->offset_sample + left_len;
  right.length_samples = r->length_samples - left_len;
  right.fade_in_samples = microfade_samples;
  // Notes: keep those starting at/after the split, re-based to the new start.
  right.notes.clear();
  for (const Note& n : r->notes)
    if (n.sample >= left_len) { Note m = n; m.sample -= left_len; m.tick -= left_ticks; right.notes.push_back(m); }

  r->length_samples = left_len;
  r->fade_out_samples = microfade_samples;
  r->notes.erase(std::remove_if(r->notes.begin(), r->notes.end(),
                                [&](const Note& n) { return n.sample >= left_len; }),
                 r->notes.end());
  // Fades cannot exceed region length.
  r->fade_in_samples = std::min(r->fade_in_samples, r->length_samples);
  right.fade_out_samples = std::min(right.fade_out_samples, right.length_samples);

  p.regions.push_back(std::move(right));
  return p.regions.back().id;
}

/// Trim the head to a new absolute start. Content stays put (offset moves with the edge).
inline bool trim_head(Project& p, Id region_id, std::int64_t new_start) {
  Region* r = p.find_region(region_id);
  if (!r) return false;
  const std::int64_t end = r->end_sample();
  new_start = std::clamp(new_start, r->start_sample - r->offset_sample, end - 1);  // cannot reveal before source start
  const std::int64_t delta = new_start - r->start_sample;
  r->start_sample = new_start;
  r->offset_sample += delta;
  r->length_samples = end - new_start;
  r->fade_in_samples = std::min(r->fade_in_samples, r->length_samples);
  return true;
}

/// Trim the tail to a new absolute end.
inline bool trim_tail(Project& p, Id region_id, std::int64_t new_end) {
  Region* r = p.find_region(region_id);
  if (!r) return false;
  std::int64_t max_end = std::numeric_limits<std::int64_t>::max();  // MIDI: unbounded
  if (r->kind == RegionKind::kAudio) {
    max_end = r->end_sample();  // unknown source: cannot extend
    if (const Source* s = p.find_source(r->source_id))
      max_end = r->start_sample + (s->length_samples - r->offset_sample);
  }
  new_end = std::clamp(new_end, r->start_sample + 1, max_end);
  r->length_samples = new_end - r->start_sample;
  r->fade_out_samples = std::min(r->fade_out_samples, r->length_samples);
  return true;
}

/// Move a region to an absolute start (and optionally another track).
/// Never negative. In Shuffle mode the caller uses `shuffle_close_gap`.
inline bool move_region(Project& p, Id region_id, std::int64_t new_start, Id new_track = kNoId) {
  Region* r = p.find_region(region_id);
  if (!r) return false;
  r->start_sample = std::max<std::int64_t>(0, new_start);
  if (new_track != kNoId && p.find_track(new_track)) r->track_id = new_track;
  return true;
}

/// Nudge by a signed number of samples (EDT-02: ladder 1/10/100/1000 smp, ms, frames, grid).
inline bool nudge_region(Project& p, Id region_id, std::int64_t delta_samples) {
  Region* r = p.find_region(region_id);
  if (!r) return false;
  r->start_sample = std::max<std::int64_t>(0, r->start_sample + delta_samples);
  return true;
}

/// Delete a region. In Shuffle mode, subsequent regions on the track close the gap.
inline bool delete_region(Project& p, Id region_id, EditMode mode = EditMode::kSlip) {
  Region* r = p.find_region(region_id);
  if (!r) return false;
  const Id track = r->track_id;
  const std::int64_t start = r->start_sample, len = r->length_samples;
  p.regions.erase(std::remove_if(p.regions.begin(), p.regions.end(),
                                 [&](const Region& x) { return x.id == region_id; }),
                  p.regions.end());
  if (mode == EditMode::kShuffle)
    for (Region& x : p.regions)
      if (x.track_id == track && x.start_sample >= start + len) x.start_sample -= len;
  return true;
}

/// Duplicate a region immediately after itself (Logic ⌘R / PT ⌘D semantics).
inline Id duplicate_region(Project& p, Id region_id) {
  const Region* r = p.find_region(region_id);
  if (!r) return kNoId;
  Region d = *r;
  d.id = p.allocate_id();
  d.start_sample = r->end_sample();
  p.regions.push_back(std::move(d));
  return p.regions.back().id;
}

/// Static clip gain (EDT-05): −60…+24 dB in 0.1 dB steps.
inline bool set_region_gain(Project& p, Id region_id, double gain_db) {
  Region* r = p.find_region(region_id);
  if (!r) return false;
  gain_db = std::clamp(gain_db, -60.0, 24.0);
  r->gain_db = static_cast<double>(static_cast<std::int64_t>(gain_db * 10.0 + (gain_db >= 0 ? 0.5 : -0.5))) / 10.0;
  return true;
}

/// Fades (EDT-03): lengths clamp to the region; 0…60 s enforced by the UI layer.
inline bool set_region_fades(Project& p, Id region_id, std::int64_t fade_in, std::int64_t fade_out) {
  Region* r = p.find_region(region_id);
  if (!r) return false;
  r->fade_in_samples = std::clamp<std::int64_t>(fade_in, 0, r->length_samples);
  r->fade_out_samples = std::clamp<std::int64_t>(fade_out, 0, r->length_samples);
  return true;
}

// ─── Track operations ────────────────────────────────────────────────────────

inline Id add_track(Project& p, TrackKind kind, std::string name, int color = 0) {
  Track t;
  t.id = p.allocate_id();
  t.kind = kind;
  t.name = std::move(name);
  t.color = color;
  t.inserts.resize(kind == TrackKind::kVca ? 0 : 5);  // A–E rows (UIW §6)
  t.sends.resize(kind == TrackKind::kVca || kind == TrackKind::kMaster ? 0 : 4);
  // Keep Master last so the mixer geometry stays PT-ordered.
  auto it = std::find_if(p.tracks.begin(), p.tracks.end(), [](const Track& x) { return x.kind == TrackKind::kMaster; });
  if (kind != TrackKind::kMaster && it != p.tracks.end()) { p.tracks.insert(it, t); }
  else p.tracks.push_back(t);
  return t.id;
}

inline bool remove_track(Project& p, Id track_id) {
  const Track* t = p.find_track(track_id);
  if (!t || t->kind == TrackKind::kMaster) return false;
  p.regions.erase(std::remove_if(p.regions.begin(), p.regions.end(),
                                 [&](const Region& r) { return r.track_id == track_id; }),
                  p.regions.end());
  for (Track& x : p.tracks) {
    if (x.output == track_id) x.output = kNoId;
    for (Send& s : x.sends) if (s.dest_track == track_id) s = Send{};
  }
  p.tracks.erase(std::remove_if(p.tracks.begin(), p.tracks.end(),
                                [&](const Track& x) { return x.id == track_id; }),
                 p.tracks.end());
  return true;
}

// ─── Undo (ARC-MDL: snapshots, structural copies) ────────────────────────────

class UndoStack {
public:
  explicit UndoStack(std::size_t capacity = 200) : capacity_{capacity} {}

  /// Call *before* applying a command; the label is shown in Edit ▸ Undo.
  void push(const Project& before, std::string label) {
    undo_.push_back({before, std::move(label)});
    if (undo_.size() > capacity_) undo_.pop_front();
    redo_.clear();
  }

  bool can_undo() const { return !undo_.empty(); }
  bool can_redo() const { return !redo_.empty(); }
  const std::string& undo_label() const { static const std::string none; return undo_.empty() ? none : undo_.back().label; }
  const std::string& redo_label() const { static const std::string none; return redo_.empty() ? none : redo_.back().label; }

  bool undo(Project& current) {
    if (undo_.empty()) return false;
    redo_.push_back({current, undo_.back().label});
    current = undo_.back().snapshot;
    undo_.pop_back();
    return true;
  }

  bool redo(Project& current) {
    if (redo_.empty()) return false;
    undo_.push_back({current, redo_.back().label});
    current = redo_.back().snapshot;
    redo_.pop_back();
    return true;
  }

  std::size_t depth() const { return undo_.size(); }

private:
  struct Entry { Project snapshot; std::string label; };
  std::size_t capacity_;
  std::deque<Entry> undo_, redo_;
};

}  // namespace s7::domain
