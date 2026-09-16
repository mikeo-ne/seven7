#pragma once
// seven7 — Application layer: Controller (spec: docs/01 L3 domain services, ARC-C06/C08/C10)
//
// The Controller owns the authoritative Project, the undo stack, the SourceStore
// and the engine Session. It exposes a *single* JSON command entry point so that
// every UI (JUCE WebView, browser dev preview, CLI, tests) drives seven7 through
// the same protocol — see docs/07-ui-bridge-protocol.md.
//
//   command(json) → json          UI → controller (edits, transport, mixer)
//   state_json()  → json          full project state (published after every edit)
//   status_json() → json          ~30 Hz transport + meters + engine health
//
// Threading: everything here runs on the message/control thread. The Session's
// process() runs on the audio thread and only ever sees published snapshots and
// ring-buffered commands (ARC-RT-02/03).

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "domain/edit_engine.h"
#include "domain/project.h"
#include "engine/session.h"
#include "pal/json.h"

namespace s7::app {

class Controller {
public:
  Controller();
  Controller(const Controller&) = delete;
  Controller& operator=(const Controller&) = delete;

  // ── lifecycle ─────────────────────────────────────────────────────────────
  /// Must be called before publishing anything: sets the engine rate/block.
  void prepare(std::int64_t sample_rate, int max_block);
  /// Reset to a fresh demo project (instrument tracks + regions that make sound).
  void new_demo_project();
  void new_empty_project();

  // ── project I/O (.s7proj bundle directory) ────────────────────────────────
  bool save_bundle(const std::string& dir, std::string* error = nullptr);
  bool load_bundle(const std::string& dir, std::string* error = nullptr);
  /// Import an audio file as a new source (+ region on `track`, or a new track when kNoId).
  bool import_audio_file(const std::string& path, domain::Id track, std::int64_t at_sample, std::string* error = nullptr);
  const std::string& bundle_path() const { return bundle_path_; }

  // ── the UI protocol ───────────────────────────────────────────────────────
  pal::Json command(const pal::Json& cmd);
  pal::Json command_text(const std::string& json_text);
  pal::Json command_text(const char* json_text) { return command_text(std::string(json_text)); }
  pal::Json state_json() const;
  pal::Json status_json() const;
  /// Waveform peaks for a source at a given samples-per-pixel (mono min/max pairs).
  pal::Json peaks_json(domain::Id source, std::int64_t from, std::int64_t to, int buckets) const;

  /// Called ~30 Hz from a timer: services recordings; returns true if state changed.
  bool tick();

  engine::Session& session() { return session_; }
  const domain::Project& project() const { return project_; }
  const engine::SourceStore& sources() const { return sources_; }

  /// Set by the shell to receive log lines (import reports, routing warnings).
  std::function<void(const std::string&)> on_log;
  std::uint64_t state_version() const { return state_version_; }

private:
  void begin_edit(const std::string& label);
  void commit();  // republish snapshot + bump state version
  void assign_slots();
  void push_all_params();
  void sync_track_params(const domain::Track& t);
  domain::Id id_of(const pal::Json& v) const { return static_cast<domain::Id>(v.as_int()); }
  std::string next_take_name(const domain::Track& t) const;
  void add_demo_content();

  domain::Project project_;
  domain::UndoStack undo_;
  engine::SourceStore sources_;
  engine::Session session_;
  engine::SchedulerConfig sched_;
  std::string bundle_path_;
  std::uint64_t state_version_ = 0;
  std::vector<std::string> log_;
  domain::EditMode edit_mode_ = domain::EditMode::kSlip;
  bool auto_microfade_ = true;
  std::int64_t microfade_samples_ = 480;  // 10 ms @ 48k (EDT-03 default)
  std::vector<std::string> last_warnings_;
};

}  // namespace s7::app
