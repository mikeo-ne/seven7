# S7-UIB · UI ↔ Engine Bridge Protocol

**Status:** implemented (engine `src/app/controller.cpp`, UI `ui/src/bridge/`, host `app/src/WebShell.cpp`)
**Scope:** the single seam between every seven7 front end and the engine. Whatever drives seven7 — the JUCE desktop shell, the browser dev preview, a test, a CLI — speaks this protocol and nothing else.

## 1. Architecture

```
┌──────────────────────────────┐      s7Command / s7State / s7Status / s7Peaks      ┌─────────────────────────┐
│  React UI  (ui/)             │ ───────────────────────────────────────────────▶ │  s7::app::Controller    │
│  EngineBridge seam           │ ◀─────────────────────────────────────────────── │  project · undo · media │
│   JuceBridge  │  HttpBridge  │      events: s7Status · s7StateChanged · s7Log    │  ↓ snapshots / commands │
└───────┬───────┴──────┬───────┘                                                    │  engine::Session (RT)   │
        │              │ Vite proxy /api → s7bridge (:8787)                          └─────────────────────────┘
   JUCE WebBrowserComponent (app/)                                    dev-only HTTP wrapper (engine/apps/s7bridge)
```

* **Message thread only.** `Controller::command()` runs on the host's message thread. The RT thread never sees JSON; it adopts published snapshots and drains a lock-free command ring (ARC-RT-02/03).
* **Two transports, one contract.** `ui/src/bridge/types.ts` defines `EngineBridge` (`command`, `getState`, `getStatus`, `getPeaks`, optional `renderUrl`, `chooseFile`, `openAudioSettings`). `JuceBridge` maps it to native functions; `HttpBridge` maps it to `/api/*`. The UI detects the desktop host when `s7Command` is listed in `window.__JUCE__.initialisationData.__juce__functions`.
* **Push, don't poll (desktop).** The host emits `s7Status` at 30 Hz and `s7StateChanged` whenever `Controller::state_version()` moves. In the browser the UI polls `/api/status` at 30 Hz and refetches state when `version` changes.

## 2. Envelope

Request: `{"op": "<name>", ...args}` · Response: `{"ok": true, "version": N, ...result}` or `{"ok": false, "error": "<message>"}`.

`version` is the project state version after the command. It increments on every structural change (edits, track ops, project load). **Live mixer moves bump the version without creating an undo entry** (they are coalesced into the project, not the history).

## 3. Operations

### Transport
| op | args | notes |
|---|---|---|
| `play` · `stop` · `toggle_play` | — | |
| `locate` | `sample` | |
| `return_to_zero` | — | |
| `record` | `on` | needs ≥1 armed audio track to produce takes |
| `loop` | `on, start, end` | |
| `metronome` | `on` | |
| `panic` | — | all notes off |

### Live MIDI
`note_on {track, pitch, velocity}` · `note_off {track, pitch}` — routed to the track's RT slot (musical typing / on-screen keys).

### Mixer (no undo entries)
| op | args |
|---|---|
| `set_track` | `track, param ∈ fader\|pan\|mute\|solo\|arm\|monitor\|name\|color\|instrument\|input\|output\|automation_mode, value` — switches accept booleans **or** numbers |
| `set_send` | `track, index, dest?, level_db?, pre?, muted?` |
| `set_insert` | `track, index, name?, bypassed?, latency?` |

`fader_pos` is the normalized fader position: `0.75` = unity, `1.0` = +12 dB, `0.15` = −60 dB, `< 0.005` = −∞ (MIX-01 taper).

### Tracks (undoable)
`add_track {kind ∈ audio|instrument|aux|bus, name, color, instrument} → {track}` · `remove_track {track}` · `reorder_track {track, index}`

### Regions (undoable, sample-accurate)
| op | args |
|---|---|
| `split` | `region, sample` |
| `split_at_playhead` | `regions[]` — fails unless the playhead is inside every region |
| `move` | `moves: [{region, start, track?}]` |
| `nudge` | `regions[], delta` |
| `trim` | `region, start?, end?` |
| `delete` | `regions[]` |
| `duplicate` | `regions[] → {regions[]}` |
| `set_region` | `region, gain_db?, fade_in?, fade_out?, muted?, name?, color?` |
| `add_midi_region` | `track, name, start, length` |
| `set_notes` | `region, notes[]` — note = `[tick, len_ticks, sample, len_samples, pitch, vel]` |

### Grid & edit modes
`snap {unit ∈ bar|beat|division|samples|seconds|frames, division, samples, fps, sample} → {sample, step}` · `edit_mode {mode ∈ slip|grid|shuffle|spot}`

### Project
| op | args |
|---|---|
| `set_project` | `name?, bpm?` or `tempo_num + tempo_den`, `sig_num?, sig_den?, key?` |
| `add_marker` | `name, sample` · `remove_marker {marker}` |
| `undo` · `redo` | — |
| `new_project` | `demo` (true → "Midnight City") |
| `save` | `path?` (defaults to the open bundle) · `open {path}` |
| `import_audio` | `path, track, sample` (track 0 → new track) |
| `get_state` · `get_status` · `get_peaks {source, from, to, buckets}` | read-only |

## 4. Shapes

**State** — `{name, sample_rate, tempo{num,den}, sig{num,den}, key, tracks[], regions[], sources[{id,name,length,channels,sample_rate}], markers[{id,name,sample}], version, bundle_path, can_undo, can_redo, undo_label, redo_label, edit_mode, content_end, next_id, schema_rev, slots[{id,latency}], instrument_presets[]}`

**Track** — `{id, name, kind ∈ audio|instrument|aux|bus|master, color, mute, solo, arm, monitor, fader_pos, pan, input_channel, output, instrument, automation_mode, inserts[5]{name,bypassed,latency}, sends[4]{active,dest,level_db,pre,muted}}`

**Region** — `{id, track, kind ∈ midi|audio, name, color (−1 = inherit), start, length, offset, source, gain_db, fade_in, fade_out, muted, notes[]}`

**Status** (30 Hz) — `{playing, recording, loop, loop_start, loop_end, metronome, playhead, cpu, xruns, rt_tracks, mae_tracks, pdc_worst, sample_rate, buffer, version, meters[[L,R,clip]…], master[L,R]}`

All positions are **samples at the project rate**; ticks (960 PPQ) appear only inside MIDI notes.

## 5. Desktop host (app/)

| Direction | Name | Payload |
|---|---|---|
| JS → native | `s7Command(json)` | returns response JSON text |
| JS → native | `s7State()` · `s7Status()` · `s7Peaks(source, from, to, buckets)` | JSON text |
| JS → native | `s7ChooseFile(kind ∈ open-project\|save-project\|import-audio)` | path or `""` |
| JS → native | `s7AudioSettings()` | opens the native device dialog |
| native → JS | `s7Status` | status JSON text, 30 Hz |
| native → JS | `s7StateChanged` | new version |
| native → JS | `s7Log` | log line |
| native → JS | `s7Menu` | `newProject · openProject · saveProject · saveProjectAs · importAudio · undo · redo · toggleMode` |

Native menus deliberately **forward to the UI** (`s7Menu`) instead of calling the Controller directly, so a menu item, a toolbar button and a shortcut all run the same code in `ui/src/lib/commands.ts`.

The production UI is embedded (`juce_add_binary_data` of `ui/dist`) and served via the WebBrowserComponent resource provider; `-DS7_UI_DEV_SERVER=http://localhost:5174` points the host at Vite for hot reload.

## 6. Recording contract

1. UI arms a track (`set_track arm`), enables `record`, presses `play`.
2. RT thread captures input into pooled chunks (ARC-RT-01, no allocation).
3. `stop` finishes the take; the host's 30 Hz `Controller::tick()` assembles it into a **Source** (`"<Track> take N.wav"`, BWF, written into the bundle's `Media/Audio Files/`) and a **Region** with auto microfades (10 ms), pushing one `"Record"` undo entry.
4. `s7StateChanged` fires → the UI refetches and the region appears.

## 7. Dev loop

```
cmake -B build -S engine -G Ninja && cmake --build build -j && ./build/s7bridge --port 8787
cd ui && npm install && npm run dev            # http://localhost:5174, /api proxied to :8787
npm run test:engine                            # integration suite against the live bridge
```
