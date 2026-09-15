#pragma once
// seven7 — Domain layer: Time Model (spec: docs/01 ARC-TIME)
//
// The sample-accuracy contract:
//   - Musical grid : 960 PPQ, 64-bit tick domain.
//   - Absolute grid: 64-bit sample positions.
//   - Tempo map    : piecewise-constant segments (start_tick, start_sample, bpm).
//   - Conversions are *integer rational accumulation* with nearest-even rounding.
//     No iterative floats → conversion is exactly invertible and drift-free
//     (acceptance target ARC-TIME-A1, exercised in tests/test_time_model.cpp).

#include <cstdint>
#include <stdexcept>
#include <vector>

namespace s7::domain {

inline constexpr std::int64_t kTicksPerQuarter = 960; // PPQ
inline constexpr std::int64_t kSecondsPerMinute = 60;

#if defined(__SIZEOF_INT128__)
using wide_int = __int128_t; // intermediate math only; all stored state stays 64-bit
#else
#error "seven7 time model requires a 128-bit integer type (GCC/Clang on all v1 targets)."
#endif

/// Tempo expressed as a rational number of beats per minute (num/den, both > 0).
/// Rationals are used so e.g. 120.005 BPM (Logic import) never becomes a float residue.
struct Bpm {
  std::int64_t num;
  std::int64_t den;
};

/// Piecewise-constant tempo map with continuous, gap-free segment edges.
///
/// Continuity invariant: when a segment is appended, its start_sample is computed
/// from the *previous* segment, so both representations agree exactly at the seam.
class TempoMap {
public:
  struct Segment {
    std::int64_t start_tick;
    std::int64_t start_sample;
    std::int64_t bpm_num;
    std::int64_t bpm_den;
  };

  /// A map always covers [0, +inf); the initial tempo applies from tick 0 / sample 0.
  TempoMap(std::int64_t sample_rate, Bpm initial_tempo)
      : sample_rate_{sample_rate} {
    if (sample_rate <= 0 || initial_tempo.num <= 0 || initial_tempo.den <= 0)
      throw std::invalid_argument("TempoMap: bad sample rate or tempo");
    segments_.push_back({0, 0, initial_tempo.num, initial_tempo.den});
  }

  /// Appends a segment beginning at `start_tick` (must be after the last segment start).
  /// Returns the computed start_sample (the segment seam, on the absolute grid).
  std::int64_t append_segment(std::int64_t start_tick, Bpm tempo) {
    if (tempo.num <= 0 || tempo.den <= 0)
      throw std::invalid_argument("TempoMap::append_segment: bad tempo");
    const Segment& last = segments_.back();
    if (start_tick <= last.start_tick)
      throw std::invalid_argument("TempoMap::append_segment: ticks must increase");

    const std::int64_t dt = start_tick - last.start_tick;
    const wide_int n = static_cast<wide_int>(dt) * kSecondsPerMinute * sample_rate_ * last.bpm_den;
    const wide_int d = static_cast<wide_int>(kTicksPerQuarter) * last.bpm_num;
    const std::int64_t seam_sample = last.start_sample + round_nearest_even(n, d);

    segments_.push_back({start_tick, seam_sample, tempo.num, tempo.den});
    return seam_sample;
  }

  /// Musical position → absolute sample (nearest-even rounding at the sample grid).
  std::int64_t tick_to_sample(std::int64_t tick) const {
    const Segment& s = segment_for_tick(tick);
    const std::int64_t dt = tick - s.start_tick; // >= 0 (domain is [0, +inf))
    const wide_int n = static_cast<wide_int>(dt) * kSecondsPerMinute * sample_rate_ * s.bpm_den;
    const wide_int d = static_cast<wide_int>(kTicksPerQuarter) * s.bpm_num;
    return s.start_sample + round_nearest_even(n, d);
  }

  /// Absolute sample → musical position (exact inverse with the same rounding rule).
  std::int64_t sample_to_tick(std::int64_t sample) const {
    const Segment& s = segment_for_sample(sample);
    const std::int64_t ds = sample - s.start_sample; // >= 0
    const wide_int n = static_cast<wide_int>(ds) * kTicksPerQuarter * s.bpm_num;
    const wide_int d = static_cast<wide_int>(kSecondsPerMinute) * sample_rate_ * s.bpm_den;
    return s.start_tick + round_nearest_even(n, d);
  }

  std::int64_t sample_rate() const { return sample_rate_; }
  const std::vector<Segment>& segments() const { return segments_; }

private:
  /// Nearest-integer division with ties-to-even over wide integers (n >= 0, d > 0).
  static std::int64_t round_nearest_even(wide_int n, wide_int d) {
    const wide_int q = n / d;
    const wide_int r = n % d;
    const wide_int twice = 2 * r;
    wide_int result = q;
    if (twice > d || (twice == d && (q & 1) == 1)) result += 1;
    return static_cast<std::int64_t>(result);
  }

  const Segment& segment_for_tick(std::int64_t tick) const {
    std::size_t lo = 0, hi = segments_.size(); // binary search: last seg with start_tick <= tick
    while (lo + 1 < hi) {
      const std::size_t mid = lo + (hi - lo) / 2;
      if (segments_[mid].start_tick <= tick) lo = mid; else hi = mid;
    }
    return segments_[lo];
  }

  const Segment& segment_for_sample(std::int64_t sample) const {
    std::size_t lo = 0, hi = segments_.size();
    while (lo + 1 < hi) {
      const std::size_t mid = lo + (hi - lo) / 2;
      if (segments_[mid].start_sample <= sample) lo = mid; else hi = mid;
    }
    return segments_[lo];
  }

  std::int64_t sample_rate_;
  std::vector<Segment> segments_;
};

} // namespace s7::domain
