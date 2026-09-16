#pragma once
// seven7 — Domain layer: Project model (spec: docs/01 ARC-MDL; docs/03 MIX-02; docs/04 EDT-01)
//
// The project is plain data. Every timeline bound is an *integer sample position*
// (EDT-01: "All bounds are integer sample positions, tick view derived").
// Musical time is a view computed through the TempoMap (ARC-TIME).
//
// The model is dependency-free (no JSON, no threads). Serialization lives in the
// application layer; render-side state lives in engine/session.h.

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "domain/time_model.h"

namespace s7::domain {

using Id = std::uint32_t;
inline constexpr Id kNoId = 0;

enum class TrackKind : std::uint8_t { kAudio, kInstrument, kAux, kBus, kVca, kMaster };
enum class RegionKind : std::uint8_t { kAudio, kMidi };

/// Metadata of an immutable media source. Audio memory is owned by the engine's
/// SourceStore (engine/session.h) and referenced by `id`.
struct Source {
  Id id = kNoId;
  std::string name;
  std::int64_t length_samples = 0;
  int channels = 1;
  std::int64_t sample_rate = 48000;
};

/// MIDI note. Positions are relative to the owning region start, stored in both
/// domains (MID-01): ticks are authoritative for MIDI, samples are pinned/derived.
struct Note {
  std::int64_t tick = 0;
  std::int64_t length_ticks = 0;
  std::int64_t sample = 0;
  std::int64_t length_samples = 0;
  std::uint8_t pitch = 60;
  std::uint8_t velocity = 100;
};

/// Region/clip: a reference into an immutable source with bounds, fades and clip gain.
struct Region {
  Id id = kNoId;
  Id track_id = kNoId;
  RegionKind kind = RegionKind::kAudio;
  Id source_id = kNoId;  // audio regions only
  std::string name;
  int color = -1;                  // −1 = inherit track color
  std::int64_t start_sample = 0;   // absolute timeline position
  std::int64_t offset_sample = 0;  // read offset into the source
  std::int64_t length_samples = 0;
  double gain_db = 0.0;            // static clip gain (EDT-05); −60…+24
  std::int64_t fade_in_samples = 0;
  std::int64_t fade_out_samples = 0;
  bool muted = false;
  std::vector<Note> notes;         // MIDI regions

  std::int64_t end_sample() const { return start_sample + length_samples; }
  bool intersects(std::int64_t t0, std::int64_t t1) const { return start_sample < t1 && end_sample() > t0; }
};

/// Insert slot (MIX-01 slots 1–10). v0.1: metadata only — names + declared latency feed
/// the PDC math (MIX-07); audio processing of inserts lands with plug-in hosting (ARC-PGN).
struct Insert {
  std::string name;
  bool bypassed = false;
  std::int64_t latency_samples = 0;
};

/// Send (MIX-06): destination strip, level, pre/post, mute.
struct Send {
  Id dest_track = kNoId;
  double level_db = 0.0;
  bool pre = false;
  bool muted = false;
  bool active = false;
};

struct Track {
  Id id = kNoId;
  std::string name;
  TrackKind kind = TrackKind::kAudio;
  int color = 0;  // index into the 24-hue track palette (UIW §1.1)
  bool mute = false;
  bool solo = false;
  bool arm = false;
  bool monitor = false;
  double fader_pos = 0.75;   // MIX-03 taper position; 0.75 = unity
  double pan = 0.0;          // −1…+1
  int input_channel = 0;     // hardware input index (audio tracks)
  Id output = kNoId;         // destination strip; kNoId = master
  std::vector<Insert> inserts;
  std::vector<Send> sends;
  std::string automation_mode = "read";
  int instrument = 0;        // instrument tracks: preset index (engine/synth.h)
  int rt_slot = -1;          // engine render slot, assigned by the session controller
  std::int64_t reported_latency = 0;  // Σ insert latency (readout, MIX-07)
};

struct Marker {
  Id id = kNoId;
  std::string name;
  std::int64_t sample = 0;
};

struct Project {
  std::string name = "Untitled";
  std::int64_t sample_rate = 48000;
  Bpm tempo{120, 1};
  int sig_num = 4;
  int sig_den = 4;
  std::string key = "C";
  std::vector<Track> tracks;
  std::vector<Region> regions;
  std::vector<Source> sources;
  std::vector<Marker> markers;
  Id next_id = 1;

  Id allocate_id() { return next_id++; }

  Track* find_track(Id id) { return find_in(tracks, id); }
  const Track* find_track(Id id) const { return find_in(tracks, id); }
  Region* find_region(Id id) { return find_in(regions, id); }
  const Region* find_region(Id id) const { return find_in(regions, id); }
  Source* find_source(Id id) { return find_in(sources, id); }
  const Source* find_source(Id id) const { return find_in(sources, id); }

  const Track* master() const {
    for (const auto& t : tracks) if (t.kind == TrackKind::kMaster) return &t;
    return nullptr;
  }

  /// Constant-tempo map for the project (ARC-TIME conversions).
  TempoMap tempo_map() const { return TempoMap(sample_rate, tempo); }

  /// Exact samples per quarter note as a rational (num/den) — no float residue.
  std::int64_t samples_per_beat_num() const { return sample_rate * kSecondsPerMinute * tempo.den; }
  std::int64_t samples_per_beat_den() const { return tempo.num; }

  /// Timeline extent: end of the last region (≥ 0).
  std::int64_t content_end_sample() const {
    std::int64_t end = 0;
    for (const auto& r : regions) end = std::max(end, r.end_sample());
    return end;
  }

  /// Region ids on a track, sorted by start (then id) — the arrangement order.
  std::vector<Id> regions_on_track(Id track_id) const {
    std::vector<const Region*> rs;
    for (const auto& r : regions) if (r.track_id == track_id) rs.push_back(&r);
    std::sort(rs.begin(), rs.end(), [](const Region* a, const Region* b) {
      return a->start_sample != b->start_sample ? a->start_sample < b->start_sample : a->id < b->id;
    });
    std::vector<Id> out;
    out.reserve(rs.size());
    for (const auto* r : rs) out.push_back(r->id);
    return out;
  }

  bool any_solo() const {
    for (const auto& t : tracks) if (t.solo) return true;
    return false;
  }

private:
  template <class Vec, class Elem = typename Vec::value_type>
  static auto find_in(Vec& v, Id id) -> decltype(&v[0]) {
    if (id == kNoId) return nullptr;
    for (auto& e : v) if (e.id == id) return &e;
    return nullptr;
  }
};

}  // namespace s7::domain
