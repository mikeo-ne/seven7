# S7-VSN · Vision, Positioning & Scope

| | |
|---|---|
| **Status** | DRAFT FOR REVIEW |
| **Version** | 0.9 |
| **Date** | 2026-09-15 |
| **Owner** | Principal Audio SWE / Product Design |
| **Downstream docs** | 01–06 |

---

## 1. Product Vision

**seven7 is the DAW for people who write like Logic users and mix like Pro Tools engineers.**

The industry is split across two mental models:

- **Logic Pro** optimizes for *creation*: fluid arrange canvas, deep stock plug-in suite, Smart Controls, Live Loops, unbeatable media/browser story — but with opinionated routing, region-centric automation, and editing precision that stops at the region level.
- **Pro Tools** optimizes for *engineering*: sample-accurate editing, explicit bus/routing discipline, VCAs, delay-compensation transparency, clip-based gain and grouping — but with a comparatively rigid creative workflow and thin bundled content.

seven7's thesis: these models are **two views over one project model**, not two products. A user composes in **Canvas mode**, presses one key, and lands in **Precision mode** with every region, fade, send, and automation point bit-identical — because nothing was converted.

### Non-negotiable invariants

1. **One timeline, two bases.** Every event owns *both* a musical position (960 PPQ ticks) and an absolute position (samples). Editing in either domain never produces sub-sample ambiguity.
2. **Nothing is destructive by default.** Sources on disk are immutable; all edits are overlay data.
3. **Logic interchange is a first-class feature**, not an import filter. Round trips must be metadata-lossless (see 05).
4. **Latency is engineered, not averaged.** Hybrid buffering keeps armed tracks on the shortest possible path while background tracks are pre-rendered ahead of the DAC clock.

---

## 2. What We Take From Each

| Domain | From Logic Pro | From Pro Tools | seven7 resolution |
|---|---|---|---|
| Arrange view | Track-focused, colorful, tool-light | Dense, edit-focused | **Dual-mode canvas** — one toggle, state-aware layouts (UIW-02) |
| Editing tools | Marquee, drag-mode fluidity | Smart Tool (Trim/Selector/Grabber), edit modes (Slip/Grid/Shuffle/Spot) | **Zone-based Smart Tool + Marquee as ⌘-tool**, both edit-mode systems (UIW-06, EDT-04) |
| Mixer | Channel-strip hierarchy, per-strip EQ/Comp defaults | Explicit buses, VCAs, sends view, delay readouts | **PT-style mixer geometry + Logic-style strip UX** (MIX-01…09) |
| Automation | Region-based (moves with content) | Track-based, sample-accurate | **Both systems coexist; convertible** (AUT-01…06) |
| Clip gain | Region gain | Clip-gain line with breakpoints | **Clip-gain envelope −60…+24 dB, breakpoint-based** (EDT-05) |
| Fades | Region fade in/out + crossfade | Micro-fades, fade shapes, batch fades | **Full superset** (EDT-03) |
| MIDI | Step Sequencer, Live Loops, MIDI FX | Basic MIDI | **Logic-tier MIDI + PT-tier timing accuracy** (MID-01…06) |
| Plug-ins | Large native suite (AU) | AAX-dominant, sparse stock | **S7 stock suite ≈ Logic parity; host AU + VST3** (MIX-12, ARC-PGN) |
| Project files | `.logicx` | `.ptx` | **`.s7proj` native + lossless `.logicx` round-trip** (CMP-01) |
| Keymaps | Logic key commands | PT keyboard focus | **Three switchable keymap personalities** (UIW-09) |

---

## 3. Personas

### P-1 "The Producer" (Logic lineage)
Writes with software instruments, loops, and pattern sequencing; expects Smart Controls and a browser full of content; rarely opens a routing matrix. Success = never opens Precision mode by force; hybrid engine keeps instrument latency imperceptible.

### P-2 "The Mix Engineer" (PT lineage)
Receives sessions, needs explicit I/O, VCAs, delay-compensation readouts, clip groups, playlists, and a mixer that maps 1:1 to a control surface. Success = can run a 300-track mix with the same muscle memory as Pro Tools, including keyboard-focused editing.

### P-3 "Post / Immersive"
Needs 7.1.4 beds, Atmos object workflows, sample-accurate spotting to picture, and AAF interchange. Success = delivers an ADM master without leaving seven7 (MIX-11).

---

## 4. Success Metrics (acceptance-level)

| ID | Metric | Target |
|---|---|---|
| VSN-M1 | Logic round-trip metadata fidelity | 100% of chunks preserved (interpreted or opaque); 0 silent losses (CMP-M) |
| VSN-M2 | Round-trip latency, armed instrument path | ≤ 4.0 ms @ 48 kHz / 64-sample buffer, Apple Silicon reference hardware |
| VSN-M3 | Mix capacity | 512 stereo audio tracks + 128 plug-in instances @ 48 kHz, ≤ 70% RT budget on reference hardware |
| VSN-M4 | Mode-switch continuity | Canvas ⇄ Precision toggle ≤ 250 ms, zero audio dropout, zero model conversion |
| VSN-M5 | Edit resolution | All SDK/CLI-reported edit bounds are integer sample positions; no float drift over 24 h timelines |
| VSN-M6 | First-run experience | Producer persona completes "record vocals over a loop" ≤ 5 min from launch |

---

## 5. Design Pillars

| # | Pillar | Consequence |
|---|---|---|
| P1 | **Truth lives in the sample domain** | Musical time is a view. Conversion rules are specified once (ARC-TIME) and tested. |
| P2 | **Two rooms, one house** | Modes are layouts + toolchains over a shared immutable model; no import/export between them. |
| P3 | **None-destructive everything** | Sources immutable; undo is a command log; freezes/bounces are caches, not commitments. |
| P4 | **Explicit beats clever** | Routing, compensation, and gain staging are *shown* (delay readouts, visible buses) rather than inferred. |
| P5 | **Familiar on entry, honest on depth** | Keymap personalities + Logic-compatible defaults; advanced surfaces (routing matrix, clip groups) one toggle away. |
| P6 | **The engine is a real-time system** | No locks, allocations, or I/O on the audio callback. Everything else is negotiable. |
| P7 | **Interchange is a product feature** | `.logicx` is a supported file type with a conformance suite, not a best-effort filter. |

---

## 6. Scope

### 6.1 v1.0 (in scope)

- macOS 14+ (Apple Silicon + Intel) and Windows 11 (x64) hosts.
- Audio engine: hybrid buffering, PDC, 1024-track ceiling, immersive (7.1.4) buses.
- Dual-mode UI with full edit/mix surfaces; three keymap personalities.
- MIDI: piano roll, step/pattern sequencer, Live Loops grid, MIDI FX chain, MPE.
- Editing: sample-accurate ops, clip gain, micro-fades, playlists/comping, Flex Time/Pitch.
- Mixing: PT-geometry mixer, VCAs, groups, surround/immersive panning, full metering.
- Plug-ins: AUv2/AUv3 + VST3 hosting (sandboxed); S7 stock suite v1.
- Files: `.s7proj` native, `.logicx` import/export (Logic 10.7–12.x), BWF/RF64, AAF export.

### 6.2 Explicitly deferred (post-1.0)

- Video engine beyond 1 synced movie track (no NLE features).
- Score editor at Logic parity (basic notation preview only).
- EuCon/control-surface SDK → v1.1 (Mackie HUI + MIDI CC surfaces at launch).
- Cloud/collaboration sessions → v1.2.
- Linux host.

---

## 7. Requirement Traceability

Every requirement in docs 01–05 carries a stable ID (`ARC-xx`, `UIW-xx`, `MIX-xx`, `EDT-xx`, `MID-xx`, `AUT-xx`, `CMP-xx`) with acceptance criteria. Changes require a spec diff review; IDs are never reused.
