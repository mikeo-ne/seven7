#pragma once
// seven7 — Engine layer: Hybrid Buffer Scheduler (spec: docs/01 ARC-ENG-01…03)
//
// Splits the session into two latency-aligned processing lanes:
//   RT lane : record-armed / input-monitored / live-instrument tracks (+ upstream cone)
//             processed at the hardware block size.
//   MAE lane: parked playback tracks, buses, masters — rendered ahead of the DAC clock
//             in large blocks (>= 4096 samples) for CPU efficiency.
//
// This skeleton contains the *pure* decision logic (classification, look-ahead,
// transition crossfades) — fully unit-testable without audio hardware.

#include <cstdint>

namespace s7::engine {

enum class Lane { kRt, kMae };

struct TrackFlags {
  bool record_armed = false;
  bool input_monitoring = false;
  bool live_instrument = false;
};

struct SchedulerConfig {
  std::int64_t sample_rate = 48000;
  std::int64_t hardware_buffer = 64;   // device block size in samples (32–512)
  std::int64_t mae_block = 4096;       // ARC-ENG-01: background render block
  std::int64_t adc_cap = 32768;        // MIX-07: project-wide PDC cap
};

/// A lane transition (record-arm toggle, ARC-ENG-03) is executed as an
/// equal-power crossfade in the stitch buffer so it is click-free and stays
/// sample-aligned across lanes.
struct TransitionPlan {
  Lane from;
  Lane to;
  std::int64_t crossfade_samples;
};

class LaneScheduler {
public:
  explicit LaneScheduler(SchedulerConfig cfg) : cfg_{cfg} {}

  /// ARC-ENG-01 lane assignment. `has_rt_dependency` is provided by the graph layer
  /// (upstream cone membership of any RT-lane track).
  Lane classify(const TrackFlags& flags, bool has_rt_dependency) const {
    const bool rt = flags.record_armed || flags.input_monitoring || flags.live_instrument ||
                    has_rt_dependency;
    return rt ? Lane::kRt : Lane::kMae;
  }

  /// Look-ahead of the MAE lane relative to the DAC clock:
  ///   L_ahead = max(mae_block, 2 * hw_buffer) + max(0, bg_latency - adc_cap)
  /// Background plug-in latency is absorbed by look-ahead *before* spending the PDC cap.
  std::int64_t look_ahead(std::int64_t max_background_chain_latency) const {
    const std::int64_t base =
        cfg_.mae_block > 2 * cfg_.hardware_buffer ? cfg_.mae_block : 2 * cfg_.hardware_buffer;
    const std::int64_t spill = max_background_chain_latency - cfg_.adc_cap;
    return base + (spill > 0 ? spill : 0);
  }

  /// ARC-ENG-03: 10 ms equal-power crossfade at lane transitions.
  std::int64_t transition_crossfade_samples() const { return cfg_.sample_rate / 100; }

  TransitionPlan plan_transition(const TrackFlags& before, bool before_rt_dep,
                                 const TrackFlags& after, bool after_rt_dep) const {
    return {classify(before, before_rt_dep), classify(after, after_rt_dep),
            transition_crossfade_samples()};
  }

  const SchedulerConfig& config() const { return cfg_; }

private:
  SchedulerConfig cfg_;
};

} // namespace s7::engine
