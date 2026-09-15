// seven7 — hybrid buffer scheduler tests (docs/01 ARC-ENG-01…03)

#include <cstdint>

#include "engine/lane_scheduler.h"
#include "s7_test.h"

using s7::engine::Lane;
using s7::engine::LaneScheduler;
using s7::engine::TrackFlags;

namespace {

void classification_matrix() {
  LaneScheduler sched{{48000, 64, 4096, 32768}};

  // Any armed/monitored/live track is RT-lane, regardless of dependencies.
  S7_CHECK(sched.classify({.record_armed = true}, false) == Lane::kRt);
  S7_CHECK(sched.classify({.input_monitoring = true}, false) == Lane::kRt);
  S7_CHECK(sched.classify({.live_instrument = true}, false) == Lane::kRt);

  // Parked playback is MAE…
  S7_CHECK(sched.classify({}, false) == Lane::kMae);
  // …unless it feeds an RT track's upstream cone.
  S7_CHECK(sched.classify({}, true) == Lane::kRt);
}

void look_ahead_math() {
  LaneScheduler sched{{48000, 64, 4096, 32768}};
  // L_ahead base = max(4096, 2*64) = 4096; background latency under the cap is free.
  S7_CHECK(sched.look_ahead(1152) == 4096);
  S7_CHECK(sched.look_ahead(32768) == 4096);
  // Beyond the PDC cap, look-ahead grows to absorb it first (MIX-07 interplay).
  S7_CHECK(sched.look_ahead(40000) == 4096 + (40000 - 32768));

  // With a huge hardware buffer the 2×HW term dominates the base.
  LaneScheduler big{{48000, 4096, 4096, 32768}};
  S7_CHECK(big.look_ahead(0) == 8192);
}

void transitions_are_click_free_by_construction() {
  LaneScheduler sched{{48000, 64, 4096, 32768}};
  // ARC-ENG-03: 10 ms at the session rate.
  S7_CHECK(sched.transition_crossfade_samples() == 480);

  const auto plan = sched.plan_transition({}, false, {.record_armed = true}, false);
  S7_CHECK(plan.from == Lane::kMae);
  S7_CHECK(plan.to == Lane::kRt);
  S7_CHECK(plan.crossfade_samples == 480);

  LaneScheduler sr96{{96000, 64, 4096, 32768}};
  S7_CHECK(sr96.transition_crossfade_samples() == 960); // scales with sample rate
}

} // namespace

int main() {
  classification_matrix();
  look_ahead_math();
  transitions_are_click_free_by_construction();
  return s7::test::summary("lane_scheduler");
}
