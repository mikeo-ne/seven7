#pragma once
// seven7 — Engine layer: S7 stock instrument (v0.1 subset of MIX-12 "S7 Sub-An / Prism7 / DrumKit")
//
// A small, allocation-free polyphonic synthesizer used by Software Instrument
// tracks. It exists so a seven7 session makes sound with zero external content:
// 16 voices, 4 waveforms, one-pole low-pass, ADSR, and a pitch-mapped drum mode.
// All state is per-track and lives in the Session (survives snapshot swaps).
//
// RT rules (ARC-RT-01): no allocation, no locks, no I/O in any method here.

#include <array>
#include <cmath>
#include <cstdint>

namespace s7::engine {

enum class Wave : std::uint8_t { kSine, kSaw, kSquare, kTriangle, kNoise };

struct SynthPreset {
  const char* name;
  Wave wave;
  float detune_cents;   // second oscillator detune (0 = single osc)
  float cutoff_hz;      // one-pole low-pass cutoff (0 = bypass)
  float attack_s, decay_s, sustain, release_s;
  float level;          // output trim (linear)
  bool drum_mode;       // pitch selects a drum sound instead of a note
};

/// Preset table. Index == Track::instrument (domain). Order is part of the project format.
inline constexpr std::array<SynthPreset, 6> kSynthPresets{{
    {"S7 Sub-An Bass",   Wave::kSaw,      0.f,   900.f,  0.004f, 0.18f, 0.55f, 0.12f, 0.55f, false},
    {"S7 Prism7 Keys",   Wave::kTriangle, 4.f,   3200.f, 0.006f, 0.35f, 0.40f, 0.30f, 0.50f, false},
    {"S7 Prism7 Pad",    Wave::kSaw,      9.f,   1400.f, 0.45f,  0.60f, 0.80f, 0.90f, 0.32f, false},
    {"S7 Pluck",         Wave::kSquare,   0.f,   2200.f, 0.002f, 0.22f, 0.00f, 0.15f, 0.45f, false},
    {"S7 Lead",          Wave::kSaw,      6.f,   4800.f, 0.010f, 0.10f, 0.70f, 0.20f, 0.40f, false},
    {"S7 DrumKit",       Wave::kSine,     0.f,   0.f,    0.001f, 0.10f, 0.00f, 0.05f, 0.90f, true},
}};

inline constexpr int kSynthVoices = 16;

class Synth {
public:
  void prepare(double sample_rate) {
    sr_ = sample_rate > 0 ? sample_rate : 48000.0;
    all_notes_off(true);
  }

  void set_preset(int index) {
    if (index < 0 || index >= static_cast<int>(kSynthPresets.size())) index = 0;
    if (index != preset_index_) { preset_index_ = index; all_notes_off(true); }
  }
  int preset() const { return preset_index_; }

  void note_on(int pitch, int velocity) {
    const SynthPreset& p = kSynthPresets[static_cast<std::size_t>(preset_index_)];
    Voice& v = allocate_voice();
    v.pitch = pitch;
    v.vel = static_cast<float>(velocity) / 127.f;
    v.stage = Voice::kAttack;
    v.env = 0.f;
    v.phase = 0.f;
    v.phase2 = 0.f;
    v.lp = 0.f;
    v.age = ++clock_;
    v.drum = p.drum_mode;
    v.drum_t = 0.f;
    v.noise = 0x9E3779B9u ^ static_cast<std::uint32_t>(clock_ * 2654435761u);
    const double f = 440.0 * std::pow(2.0, (pitch - 69) / 12.0);
    v.inc = static_cast<float>(f / sr_);
    v.inc2 = static_cast<float>(f * std::pow(2.0, p.detune_cents / 1200.0) / sr_);
  }

  void note_off(int pitch) {
    for (Voice& v : voices_)
      if (v.stage != Voice::kIdle && v.stage != Voice::kRelease && v.pitch == pitch) v.stage = Voice::kRelease;
  }

  void all_notes_off(bool hard) {
    for (Voice& v : voices_) {
      if (hard) { v.stage = Voice::kIdle; v.env = 0.f; }
      else if (v.stage != Voice::kIdle) v.stage = Voice::kRelease;
    }
  }

  bool active() const {
    for (const Voice& v : voices_) if (v.stage != Voice::kIdle) return true;
    return false;
  }

  /// Render `frames` samples, *adding* into a mono float buffer.
  void render_add(float* out, int frames) {
    const SynthPreset& p = kSynthPresets[static_cast<std::size_t>(preset_index_)];
    const float a_rate = rate_per_sample(p.attack_s);
    const float d_rate = rate_per_sample(p.decay_s);
    const float r_rate = rate_per_sample(p.release_s);
    const float lp_coef = p.cutoff_hz > 0.f
        ? 1.f - std::exp(-2.f * 3.14159265f * p.cutoff_hz / static_cast<float>(sr_))
        : 1.f;

    for (Voice& v : voices_) {
      if (v.stage == Voice::kIdle) continue;
      for (int i = 0; i < frames; ++i) {
        // ADSR
        switch (v.stage) {
          case Voice::kAttack:  v.env += a_rate; if (v.env >= 1.f) { v.env = 1.f; v.stage = Voice::kDecay; } break;
          case Voice::kDecay:   v.env -= d_rate * (1.f - p.sustain); if (v.env <= p.sustain) { v.env = p.sustain; v.stage = Voice::kSustain; } break;
          case Voice::kSustain: if (p.sustain <= 0.f) { v.stage = Voice::kIdle; } break;
          case Voice::kRelease: v.env -= r_rate; if (v.env <= 0.f) { v.env = 0.f; v.stage = Voice::kIdle; } break;
          case Voice::kIdle: break;
        }
        if (v.stage == Voice::kIdle) break;

        float s;
        if (v.drum) s = drum_sample(v);
        else {
          s = osc(p.wave, v.phase, v.noise);
          v.phase += v.inc; if (v.phase >= 1.f) v.phase -= 1.f;
          if (p.detune_cents != 0.f) {
            s = 0.5f * (s + osc(p.wave, v.phase2, v.noise));
            v.phase2 += v.inc2; if (v.phase2 >= 1.f) v.phase2 -= 1.f;
          }
          v.lp += lp_coef * (s - v.lp);
          s = v.lp;
        }
        out[i] += s * v.env * v.vel * p.level;
      }
    }
  }

private:
  struct Voice {
    enum Stage : std::uint8_t { kIdle, kAttack, kDecay, kSustain, kRelease } stage = kIdle;
    int pitch = 0;
    float vel = 0.f, env = 0.f;
    float phase = 0.f, inc = 0.f, phase2 = 0.f, inc2 = 0.f, lp = 0.f;
    std::uint64_t age = 0;
    bool drum = false;
    float drum_t = 0.f;
    std::uint32_t noise = 1u;
  };

  float rate_per_sample(float seconds) const {
    const double n = seconds * sr_;
    return n < 1.0 ? 1.f : static_cast<float>(1.0 / n);
  }

  static float osc(Wave w, float ph, std::uint32_t& noise) {
    switch (w) {
      case Wave::kSine:     return std::sin(ph * 6.2831853f);
      case Wave::kSaw:      return 2.f * ph - 1.f;
      case Wave::kSquare:   return ph < 0.5f ? 1.f : -1.f;
      case Wave::kTriangle: return ph < 0.5f ? 4.f * ph - 1.f : 3.f - 4.f * ph;
      case Wave::kNoise:    noise = noise * 1664525u + 1013904223u; return static_cast<float>(noise >> 8) / 8388608.f - 1.f;
    }
    return 0.f;
  }

  /// GM-ish drum map: 35/36 kick, 38/40 snare, 42/44 closed hat, 46 open hat, 39 clap, else tom.
  float drum_sample(Voice& v) {
    const float t = v.drum_t;
    v.drum_t += 1.f / static_cast<float>(sr_);
    float s = 0.f;
    const int p = v.pitch;
    auto noise = [&]() { return osc(Wave::kNoise, 0.f, v.noise); };
    if (p == 35 || p == 36) {                                  // kick: pitch drop 150→45 Hz
      const float f = 45.f + 105.f * std::exp(-t * 28.f);
      v.phase += f / static_cast<float>(sr_); if (v.phase >= 1.f) v.phase -= 1.f;
      s = std::sin(v.phase * 6.2831853f) * std::exp(-t * 7.f);
      if (t > 0.6f) v.stage = Voice::kIdle;
    } else if (p == 38 || p == 40) {                           // snare: tone + noise
      v.phase += 190.f / static_cast<float>(sr_); if (v.phase >= 1.f) v.phase -= 1.f;
      s = 0.5f * std::sin(v.phase * 6.2831853f) * std::exp(-t * 30.f) + 0.6f * noise() * std::exp(-t * 14.f);
      if (t > 0.4f) v.stage = Voice::kIdle;
    } else if (p == 42 || p == 44) {                           // closed hat
      const float n = noise();
      v.lp += 0.6f * (n - v.lp);
      s = (n - v.lp) * 0.7f * std::exp(-t * 60.f);
      if (t > 0.12f) v.stage = Voice::kIdle;
    } else if (p == 46) {                                      // open hat
      const float n = noise();
      v.lp += 0.6f * (n - v.lp);
      s = (n - v.lp) * 0.6f * std::exp(-t * 9.f);
      if (t > 0.7f) v.stage = Voice::kIdle;
    } else if (p == 39) {                                      // clap
      const float burst = std::fmod(t, 0.011f) < 0.006f && t < 0.033f ? 1.f : std::exp(-(t - 0.03f) * 22.f);
      s = noise() * 0.6f * burst;
      if (t > 0.35f) v.stage = Voice::kIdle;
    } else {                                                   // tom family
      const float f = 80.f + 120.f * std::exp(-t * 18.f) + static_cast<float>(p - 45) * 6.f;
      v.phase += f / static_cast<float>(sr_); if (v.phase >= 1.f) v.phase -= 1.f;
      s = std::sin(v.phase * 6.2831853f) * std::exp(-t * 9.f);
      if (t > 0.5f) v.stage = Voice::kIdle;
    }
    return s;
  }

  Voice& allocate_voice() {
    Voice* best = nullptr;
    for (Voice& v : voices_) if (v.stage == Voice::kIdle) return v;
    for (Voice& v : voices_) if (!best || v.age < best->age) best = &v;  // steal oldest
    return *best;
  }

  double sr_ = 48000.0;
  int preset_index_ = 0;
  std::uint64_t clock_ = 0;
  std::array<Voice, kSynthVoices> voices_{};
};

}  // namespace s7::engine
