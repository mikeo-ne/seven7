// seven7 — mixer math tests (docs/03 MIX-03 fader law, MIX-10 pan law; ARC-T03 summing)

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "engine/mix_math.h"
#include "s7_test.h"

using namespace s7::engine;

namespace {

void fader_law() {
  S7_CHECK_NEAR(fader_db(1.000), 12.0, 1e-9);   // top: +12 dB
  S7_CHECK_NEAR(fader_db(0.750), 0.0, 1e-9);    // unity detent
  S7_CHECK_NEAR(fader_db(0.150), -60.0, 1e-9);  // end of dB-linear region
  S7_CHECK_NEAR(fader_db(0.005), -144.0, 1e-9); // floor
  S7_CHECK(std::isinf(fader_db(0.004)));        // mute below floor
  S7_CHECK(fader_db(0.004) < 0.0);

  // Monotonic non-decreasing over travel, and always on the 0.1 dB grid.
  double prev = fader_db(0.005);
  for (int i = 6; i <= 1000; ++i) {
    const double db = fader_db(i / 1000.0);
    S7_CHECK(db >= prev);
    S7_CHECK_NEAR(db * 10.0, std::round(db * 10.0), 1e-9);
    prev = db;
  }

  S7_CHECK(db_to_gain(0.0) == 1.0);
  S7_CHECK(db_to_gain(-144.0) == 0.0); // mute floor
  S7_CHECK_NEAR(db_to_gain(12.0), 3.9810717055349722, 1e-12);
}

void pan_law() {
  const auto hard_l = constant_power_pan(-1.0);
  S7_CHECK_NEAR(hard_l.left, 1.0, 1e-12);
  S7_CHECK_NEAR(hard_l.right, 0.0, 1e-12);

  const auto center = constant_power_pan(0.0);
  // −3 dB constant power: each side ≈ 0.7071, total power == 1 (± MIX-10 tolerance)
  S7_CHECK_NEAR(center.left, center.right, 1e-12);
  S7_CHECK_NEAR(center.left * center.left + center.right * center.right, 1.0, 1e-9);

  for (int i = -100; i <= 100; ++i) {
    const auto g = constant_power_pan(i / 100.0);
    S7_CHECK_NEAR(g.left * g.left + g.right * g.right, 1.0, 1e-9);
  }
}

void summing_precision() {
  // ARC-T03: unity-gain single-input passthrough is bit-exact through the
  // double accumulator (float → double → float round trip loses nothing).
  constexpr std::size_t n = 4096;
  std::vector<float> src(n), out(n);
  std::vector<double> acc(n, 0.0);
  std::uint32_t state = 0x12345678u;
  for (auto& s : src) { // xorshift noise in [-1, 1)
    state ^= state << 13; state ^= state >> 17; state ^= state << 5;
    s = static_cast<float>(static_cast<std::int32_t>(state)) / 2147483648.0f;
  }
  accumulate_bus(src.data(), acc.data(), n, 1.0);
  finalize_bus(acc.data(), out.data(), n);
  for (std::size_t i = 0; i < n; ++i) S7_CHECK(out[i] == src[i]);

  // Gain staging: fader at unity applies exactly 1.0.
  const double g = db_to_gain(fader_db(0.75));
  S7_CHECK(g == 1.0);
}

} // namespace

int main() {
  fader_law();
  pan_law();
  summing_precision();
  return s7::test::summary("mix_math");
}
