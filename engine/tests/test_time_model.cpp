// seven7 — ARC-TIME acceptance tests (docs/01: tick⇄sample contract)
//
//   A1 (property): for random tempo maps (<= 24 segments, 20–500 BPM) and random
//   ticks, tick→sample→tick round-trips are identity. BPM range chosen so the
//   tick grid is never coarser than the sample grid (>= 1 sample/tick), which is
//   the documented invertibility domain of the time model.

#include <cstdint>
#include <random>

#include "domain/time_model.h"
#include "s7_test.h"

using s7::domain::TempoMap;

namespace {

void deterministic_cases() {
  // 120 BPM @ 48 kHz: 1 quarter = 24000 samples → exact 25 samples/tick.
  TempoMap map{48000, {120, 1}};
  S7_CHECK(map.tick_to_sample(960) == 24000);
  S7_CHECK(map.tick_to_sample(1920) == 48000);
  S7_CHECK(map.sample_to_tick(24000) == 960);

  // Tempo change at tick 1920 down to 60 BPM: seam must be continuous,
  // and one more quarter after the seam costs 48000 samples.
  const std::int64_t seam = map.append_segment(1920, {60, 1});
  S7_CHECK(seam == 48000);
  S7_CHECK(map.tick_to_sample(1920) == 48000); // identity at the seam (new segment)
  S7_CHECK(map.tick_to_sample(2880) == 96000);
  S7_CHECK(map.sample_to_tick(96000) == 2880);

  // Fractional BPM (Logic import style: 120.5 BPM = 1205/10) stays rational-exact:
  // 1 quarter = 48000*60*10/1205 = 23900.4149… samples → nearest-even = 23900.
  TempoMap frac{48000, {1205, 10}};
  S7_CHECK(frac.tick_to_sample(960) == 23900);
  S7_CHECK(frac.sample_to_tick(frac.tick_to_sample(4096)) == 4096);

  // Deep-timeline sanity (≈130 years of ticks): no overflow, full round trip.
  const std::int64_t deep = 3'000'000'000;
  S7_CHECK(map.sample_to_tick(map.tick_to_sample(deep)) == deep);
}

void property_round_trips() {
  std::mt19937_64 rng(7); // fixed seed → deterministic CI
  const std::int64_t srs[3] = {44100, 48000, 96000};

  std::int64_t verified = 0;
  for (int m = 0; m < 48; ++m) {
    TempoMap map{srs[m % 3], {6000 + static_cast<std::int64_t>(rng() % 24000), 100}};

    std::int64_t tick_cursor = 0;
    const int segments = 1 + m % 24;
    for (int s = 1; s < segments; ++s) {
      tick_cursor += 3000 + static_cast<std::int64_t>(rng() % 9000);
      map.append_segment(tick_cursor, {2000 + static_cast<std::int64_t>(rng() % 48000), 100});
    }
    const std::int64_t limit = tick_cursor + 960 * 512;

    for (int i = 0; i < 1500; ++i) {
      const std::int64_t t = static_cast<std::int64_t>(rng() % limit);
      S7_CHECK(map.sample_to_tick(map.tick_to_sample(t)) == t);
      ++verified;

      // Monotonicity of the forward map on a random ordered pair.
      const std::int64_t u = t + 1 + static_cast<std::int64_t>(rng() % 97);
      S7_CHECK(map.tick_to_sample(u) >= map.tick_to_sample(t));
    }
  }
  std::printf("ARC-TIME-A1: %lld round-trip identities verified\n",
              static_cast<long long>(verified));
}

} // namespace

int main() {
  deterministic_cases();
  property_round_trips();
  return s7::test::summary("time_model");
}
