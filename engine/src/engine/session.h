#pragma once
// seven7 — Engine layer: Session renderer (spec: docs/01 ARC-C02…C06, ARC-RT-01…04; docs/03 MIX-01…08)
//
// The Session is the pull-based render core that sits behind the audio callback.
//
//   control thread                          RT thread (audio callback)
//   ──────────────                          ─────────────────────────
//   compile(Project) → RenderSnapshot ──RCU──▶ borrow snapshot for one block
//   commands (play/locate/fader/notes) ─ring─▶ drain at block start
//   service(): finalize recordings     ◀─ring── filled record chunks
//   poll meters/transport              ◀─atomics
//
// RT rules honored by process(): no allocation, no locks, no I/O; all buffers are
// sized in prepare(). Sample-accurate: loop points, region bounds, note events and
// fades are all resolved to the sample inside the block.

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "domain/project.h"
#include "engine/lane_scheduler.h"
#include "engine/rt_primitives.h"
#include "engine/synth.h"

namespace s7::engine {

// ─── Immutable audio sources (EDT-01: never written by edits) ────────────────

struct SourceAudio {
  std::int64_t sample_rate = 48000;
  std::vector<std::vector<float>> ch;  // de-interleaved channels
  std::int64_t length() const { return ch.empty() ? 0 : static_cast<std::int64_t>(ch[0].size()); }
  int channels() const { return static_cast<int>(ch.size()); }
};

/// Control-thread registry of sources + waveform mipmaps (ARC-C12 peak cache).
class SourceStore {
public:
  static constexpr std::int64_t kPeakBlock = 256;

  struct Entry {
    std::shared_ptr<const SourceAudio> audio;
    std::vector<float> peaks;  // per block: [min,max] × channels, interleaved
  };

  void add(domain::Id id, std::shared_ptr<const SourceAudio> audio);
  void remove(domain::Id id) { entries_.erase(id); }
  std::shared_ptr<const SourceAudio> get(domain::Id id) const;
  const std::vector<float>* peaks(domain::Id id) const;
  std::size_t size() const { return entries_.size(); }

  static std::vector<float> compute_peaks(const SourceAudio& a, std::int64_t block = kPeakBlock);

private:
  std::unordered_map<domain::Id, Entry> entries_;
};

// ─── Render snapshot (compiled from the Project on the control thread) ───────

struct RenderRegion {
  std::int64_t start = 0, end = 0, offset = 0;
  std::int64_t fade_in = 0, fade_out = 0;
  float gain = 1.f;
  std::shared_ptr<const SourceAudio> src;
};

struct RenderNote {
  std::int64_t start = 0, end = 0;  // absolute samples
  std::uint8_t pitch = 60, vel = 100;
};

struct RenderSend {
  int dest_slot = -1;
  float gain = 1.f;
  bool pre = false;
};

struct RenderTrack {
  domain::Id id = domain::kNoId;
  int slot = -1;
  domain::TrackKind kind = domain::TrackKind::kAudio;
  int out_slot = -1;  // -1 = master
  int input_channel = 0;
  int preset = 0;
  bool stereo = false;  // mono strips use the MIX-10 pan law; stereo strips use balance
  Lane lane = Lane::kMae;
  std::int64_t latency = 0;
  // Mixer state as persisted in the project. The Session applies these when it first
  // sees a snapshot; live moves arrive as commands (ARC-RT-03). Contract: the controller
  // writes every parameter change into the Project *before* sending the command, so a
  // republish can never carry a stale value.
  double fader_pos = 0.75, pan = 0.0;
  bool mute = false, solo = false, arm = false, monitor = false;
  std::vector<RenderRegion> regions;
  std::vector<RenderNote> notes;
  std::vector<RenderSend> sends;
};

struct RenderSnapshot {
  std::uint64_t serial = 0;  // assigned by Session::publish
  std::int64_t sample_rate = 48000;
  std::vector<RenderTrack> tracks;  // in processing order (sources → buses → master)
  int master_slot = -1;
  std::int64_t spb_num = 0, spb_den = 1;  // samples per beat, exact rational
  int sig_num = 4;
  int rt_tracks = 0, mae_tracks = 0;
  std::int64_t pdc_worst = 0;
};

/// Compile the project into a render snapshot. Slots must already be assigned to
/// tracks (Track::rt_slot). Reports routing problems through `report`.
struct CompileReport {
  std::vector<std::string> warnings;
  std::int64_t pdc_worst = 0;
};
std::unique_ptr<RenderSnapshot> compile_snapshot(const domain::Project& project, const SourceStore& sources,
                                                 const SchedulerConfig& sched, CompileReport* report = nullptr);

// ─── Commands (control → RT) ─────────────────────────────────────────────────

struct Command {
  enum Type : std::uint8_t {
    kPlay, kStop, kLocate, kLoop, kRecord, kMetronome, kParam, kNoteOn, kNoteOff, kAllNotesOff, kPreset
  } type = kStop;
  enum Param : std::uint8_t { kFader, kPan, kMute, kSolo, kArm, kMonitor } param = kFader;
  int slot = -1;
  int a = 0, b = 0;
  std::int64_t pos = 0, pos2 = 0;
  double value = 0.0;
};

struct TransportStatus {
  bool playing = false, recording = false, loop_on = false, metronome = false;
  std::int64_t playhead = 0, loop_start = 0, loop_end = 0;
  float cpu_load = 0.f;
  std::uint32_t xruns = 0;
  int rt_tracks = 0, mae_tracks = 0;
  std::int64_t pdc_worst = 0;
};

// ─── Recording (RT-safe chunk pool, ARC-RT-01) ──────────────────────────────

struct RecordChunk {
  static constexpr std::int64_t kCapacity = 32768;
  std::array<float, kCapacity> data{};
  std::int64_t used = 0;
};

struct FilledChunk {
  RecordChunk* chunk = nullptr;  // may be null when `last` only signals the end of a take
  int slot = -1;
  std::int64_t start_sample = 0;
  bool last = false;
};

/// Delivered on the control thread when a take is complete.
struct FinishedTake {
  int slot = -1;
  std::int64_t start_sample = 0;
  std::shared_ptr<SourceAudio> audio;
};

// ─── Session ─────────────────────────────────────────────────────────────────

class Session {
public:
  static constexpr int kMaxSlots = 256;
  static constexpr int kMaxBlock = 4096;
  static constexpr int kMaxEvents = 256;

  Session();
  ~Session();
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  // control thread ------------------------------------------------------------
  void prepare(std::int64_t sample_rate, int max_block);
  void publish(std::unique_ptr<RenderSnapshot> snapshot);
  bool send(const Command& c) { return commands_.push(c); }

  void play() { send({Command::kPlay}); }
  void stop() { send({Command::kStop}); }
  void locate(std::int64_t sample) { Command c{Command::kLocate}; c.pos = sample; send(c); }
  void set_loop(bool on, std::int64_t start, std::int64_t end) { Command c{Command::kLoop}; c.a = on; c.pos = start; c.pos2 = end; send(c); }
  void set_record(bool on) { Command c{Command::kRecord}; c.a = on; send(c); }
  void set_metronome(bool on) { Command c{Command::kMetronome}; c.a = on; send(c); }
  void set_param(int slot, Command::Param p, double v) { Command c{Command::kParam}; c.param = p; c.slot = slot; c.value = v; send(c); }
  void note_on(int slot, int pitch, int vel) { Command c{Command::kNoteOn}; c.slot = slot; c.a = pitch; c.b = vel; send(c); }
  void note_off(int slot, int pitch) { Command c{Command::kNoteOff}; c.slot = slot; c.a = pitch; send(c); }
  void set_preset(int slot, int preset) { Command c{Command::kPreset}; c.slot = slot; c.a = preset; send(c); }

  /// Pump the record-chunk pool and assemble finished takes. Call ~30 Hz.
  void service(const std::function<void(FinishedTake)>& on_take);

  TransportStatus transport() const;
  float peak(int slot, int channel) const { return peak_[static_cast<std::size_t>(slot) * 2 + static_cast<std::size_t>(channel)].load(std::memory_order_relaxed); }
  bool clip(int slot) const { return clip_[static_cast<std::size_t>(slot)].load(std::memory_order_relaxed); }
  void clear_clip(int slot) { clip_[static_cast<std::size_t>(slot)].store(false, std::memory_order_relaxed); }
  float master_peak(int channel) const { return master_peak_[static_cast<std::size_t>(channel)].load(std::memory_order_relaxed); }
  std::int64_t sample_rate() const { return sample_rate_; }

  // RT thread -------------------------------------------------------------------
  /// De-interleaved I/O. `in` may be null. Output is *overwritten*.
  void process(const float* const* in, int in_channels, float* const* out, int out_channels, int frames);

private:
  struct SlotState {
    // targets (set by commands) and smoothed gains (RT-owned)
    double fader_pos = 0.75, pan = 0.0;
    bool mute = false, solo = false, arm = false, monitor = false;
    float gl = 0.f, gr = 0.f;  // smoothed post-fader gains
    bool gains_primed = false;  // first block after prepare/publish jumps straight to target (no fade-in)
    Synth synth;
    bool synth_ready = false;
    // recording
    bool recording = false;
    RecordChunk* chunk = nullptr;
    std::int64_t chunk_start = 0;
    std::int64_t rec_pos = 0;
    // meters
    float hold_l = 0.f, hold_r = 0.f;
  };

  struct Event { int offset; bool on; std::uint8_t pitch, vel; };

  void drain_commands(const RenderSnapshot* s);
  void adopt_snapshot(const RenderSnapshot& s);
  void render_segment(const RenderSnapshot& s, const float* const* in, int in_ch, float* const* out, int out_ch,
                      int offset, int n);
  void read_audio_regions(const RenderTrack& t, float* l, float* r, int n);
  void render_instrument(const RenderTrack& t, SlotState& st, float* l, float* r, int n);
  void capture_input(SlotState& st, const float* src, int n);
  void flush_recording(SlotState& st, int slot);
  void start_recording_all(const RenderSnapshot& s);
  void stop_recording_all();
  void render_metronome(const RenderSnapshot* s, float* l, float* r, int n);
  void all_notes_off(bool hard);

  // configuration
  std::int64_t sample_rate_ = 48000;
  int max_block_ = 512;
  float smooth_coef_ = 0.01f;
  float meter_decay_ = 0.999f;

  // RT-owned transport
  std::uint64_t seen_serial_ = 0;
  bool playing_ = false, recording_ = false, loop_on_ = false, metronome_ = false;
  std::int64_t pos_ = 0, loop_start_ = 0, loop_end_ = 0;
  std::int64_t metro_next_beat_ = -1;
  int metro_remaining_ = 0;
  float metro_phase_ = 0.f, metro_inc_ = 0.f;

  // shared state
  std::atomic<bool> a_playing_{false}, a_recording_{false}, a_loop_{false}, a_metro_{false};
  std::atomic<std::int64_t> a_pos_{0}, a_loop_start_{0}, a_loop_end_{0};
  std::atomic<float> a_cpu_{0.f};
  std::atomic<std::uint32_t> a_xruns_{0};
  std::atomic<int> a_rt_tracks_{0}, a_mae_tracks_{0};
  std::atomic<std::int64_t> a_pdc_{0};

  RcuSlot<RenderSnapshot> snapshot_;
  std::uint64_t next_serial_ = 1;  // control thread
  SpscRing<Command, 1024> commands_;
  SpscRing<RecordChunk*, 256> pool_;
  SpscRing<FilledChunk, 256> filled_;
  std::vector<std::unique_ptr<RecordChunk>> chunk_storage_;
  std::unordered_map<int, std::vector<float>> takes_;  // control thread: slot → assembled audio
  std::unordered_map<int, std::int64_t> take_start_;

  std::vector<SlotState> slots_;
  std::vector<double> acc_;      // [slot][2][max_block]
  std::vector<float> scratch_;   // 4 × max_block
  std::array<std::atomic<float>, kMaxSlots * 2> peak_{};
  std::array<std::atomic<bool>, kMaxSlots> clip_{};
  std::array<std::atomic<float>, 2> master_peak_{};
};

/// Offline bounce (MIX-13): renders [start, end) of a snapshot with a private Session.
/// Output is de-interleaved stereo float. Deterministic — used by CI for bit-exact diffs.
std::vector<std::vector<float>> render_offline(std::unique_ptr<RenderSnapshot> snapshot, std::int64_t start,
                                               std::int64_t end, int block = 512);

}  // namespace s7::engine
