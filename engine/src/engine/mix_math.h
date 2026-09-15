#pragma once
// seven7 — Engine layer: Mixer math (spec: docs/03 MIX-03, MIX-10; ARC-T03)
//
// Header-only numeric kernels, kept dependency-free so they can sit at the heart
// of the engine. Signal path is 32-bit float; every summing junction accumulates
// in 64-bit double and single-rounds at the output (ARC-T03).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace s7::engine {

inline constexpr double kFaderFloorDb = -144.0; // mute floor (MIX-03)

/// MIX-03 fader taper.
///   pos 1.00 → +12 dB (top)
///   pos 0.75 →   0 dB (unity detent)
///   pos 0.15 → −60 dB (end of dB-linear region)
///   pos 0.005 → −144 dB (floor)
///   pos < 0.005 → −inf (mute)
/// Output is quantized to 0.1 dB steps per the mixer spec.
inline double fader_db(double position) {
  const double pos = std::clamp(position, 0.0, 1.0);
  if (pos < 0.005) return -std::numeric_limits<double>::infinity();

  double db;
  if (pos >= 0.75) {
    db = 12.0 * (pos - 0.75) / 0.25;
  } else if (pos >= 0.15) {
    db = -60.0 * (0.75 - pos) / 0.60;
  } else {
    db = -60.0 - 84.0 * (0.15 - pos) / 0.145;
  }
  return std::round(db * 10.0) / 10.0; // 0.1 dB steps
}

/// dB → linear gain, honoring the mute floor.
inline double db_to_gain(double db) {
  if (db <= kFaderFloorDb) return 0.0;
  return std::pow(10.0, db / 20.0);
}

struct StereoGain {
  double left;
  double right;
};

/// MIX-10 constant-power pan law (−3 dB center), pan ∈ [−1, +1].
inline StereoGain constant_power_pan(double pan) {
  const double p = std::clamp(pan, -1.0, 1.0);
  const double angle = (p + 1.0) * 0.7853981633974483; // (pan+1) * π/4
  return {std::cos(angle), std::sin(angle)};
}

/// Accumulate a float source into a double-precision bus with gain (single kernel pass).
inline void accumulate_bus(const float* src, double* dst, std::size_t frames, double gain) {
  for (std::size_t i = 0; i < frames; ++i)
    dst[i] += static_cast<double>(src[i]) * gain;
}

/// Single-round a double accumulator into the float output stage.
inline void finalize_bus(const double* acc, float* out, std::size_t frames) {
  for (std::size_t i = 0; i < frames; ++i)
    out[i] = static_cast<float>(acc[i]);
}

} // namespace s7::engine
