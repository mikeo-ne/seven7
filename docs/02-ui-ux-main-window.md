# S7-UIW · UI/UX Blueprint & Main Window Layout Specs

| | |
|---|---|
| **Status** | DRAFT FOR REVIEW |
| **Version** | 0.9 |
| **Date** | 2026-09-15 |
| **Owner** | Principal UI/UX Designer |
| **Fulfills** | Output Requirement 2 (UI/UX Wireframe & Layout), Input Requirement 2 (Interface & UX) |

---

## 1. Design Language

seven7 reads like **a Logic Pro session that suddenly grew a Pro Tools toolbar**: dark, saturated, content-first in Canvas mode; denser, label-explicit, numbers-forward in Precision mode. Same type ramp, same color tokens — only *density and disclosure* change.

### 1.1 Tokens

| Token | Value | Use |
|---|---|---|
| `bg.app` | `#17181C` | App background |
| `bg.panel` | `#1E2026` | Inspectors, editors, mixer |
| `bg.raise` | `#262932` | Track headers, strip bodies |
| `bg.sel` | `#31435E` | Selection wash |
| `ink.1 / .2 / .3` | `#E8EAF0 / #AEB3C0 / #6E7485` | Primary / secondary / disabled text |
| `acc.primary` | `#5B9DFF` | Focus, playhead, links |
| `acc.record` | `#FF453A` | Record, punch |
| `acc.solo` | `#FFD60A` | Solo |
| `acc.mute` | `#FF9F0A` | Mute |
| `acc.fade` | `#64D2FF` | Fade/clip-gain overlays |
| `acc.autom` | `#BF5AF2` | Automation lanes/points |
| `grid.major/.minor` | `#FFFFFF 14% / 6%` | Ruler & editors |
| Track palette | 24 curated hues (ΔE-checked) + custom | Track color, regions inherit at 78% saturation |

- **Typography:** UI — SF Pro / Inter, 11–13 px ramp (1 px steps in Compact density); numeric readouts, timecode, and dB scales — SF Mono / JetBrains Mono, tabular figures; LCD — 20 px mono.
- **Grid:** 4 px base unit; corner radii 4 px (controls), 6 px (panels); hairlines via 1 px translucent strokes, no drop shadows inside the canvas.
- **Density:** `Comfort / Default / Compact` toggle scales row heights (arrange 52 / 44 / 34 px default track lane) and mixer strip width (96 / 80 / 64 px).
- **Motion:** only opacity/transform, ≤ 160 ms, `reduce-motion` respected; **the mode toggle crossfades the workspace in 120 ms** — no sliding panels.

### 1.2 Global frame

```text
┌──────────────────────────────────────────────────────────────────────────────────────────┐
│ Title/Mode bar  (h=34)  project name · alternatives · [Canvas | Precision] · search       │
│ Toolbar          (h=44)  tools, transport buttons, edit modes, snap, mode-specific cluster │
│ Control bar/LCD  (h=48)  position/tempo/sig LCD · playhead clock · CPU/HD · metronome      │
│ Content region   (flex)  mode-defined (see §3, §4)                                         │
│ Status bar       (h=22)  PDC state · lane readouts · zoom · notifications                  │
└──────────────────────────────────────────────────────────────────────────────────────────┘
Minimum window 1280×768 · target 1440×900+ · HiDPI-aware · all panes resizable, persist per mode
```

---

## 2. The Dual-Mode Canvas (UIW-02)

> Requirement: *"Smooth toggle between a fluid, track-focused visual canvas (Logic style) and a high-density, sample-accurate edit/mix split view (Pro Tools style)."*

| Aspect | **Canvas mode** (creation) | **Precision mode** (engineering) |
|---|---|---|
| Metaphor | Logic arrange window | Pro Tools Edit + Mix windows |
| Track headers | 260 px: color, icon, name, M/S/R/A, freeze | 360 px: adds I/O, inserts C1–C2, sends 1–2 mini-slots |
| Edit modes | Drag modes: Overlap / No-Overlap / X-Fade | **Slip / Grid (abs+rel) / Shuffle / Spot** |
| Default tool | Pointer (marquee on ⌘) | **Smart Tool** (zone-based, §5) |
| Automation | Per-track overlay toggle (A) | Lanes view, write-mode cluster on header |
| Mixer | Bottom drawer or float | **Bottom split — always docked, faders live** |
| Keymap personality | Logic-style | PT keyboard-focus |
| Ruler default | Bars & Beats | Min:Sec / Samples (user-set, per mode) |

**Behavior contract**
- `⌃⌘M` (or the title-bar segmented control) toggles; **audio never drops**; switch ≤ 250 ms (VSN-M4).
- Each mode persists its own zoom, scroll, visible pane set, tool, edit mode, and ruler format. Project data is shared by construction (Pillar P2).
- A toolbar "mode cluster" swaps contents (drag-mode picker ⇄ edit-mode picker) so muscle memory from either DAW lands on the same screen coordinates.

---

## 3. Main Window — Canvas Mode (wireframe)

```text
┌───────────────────────────────────────────────────────────────────────────────────────────┐
│ ◆ seven7 — Midnight City ▸ Alt 000        Workspace: (Canvas | Precision )  ⌃⌘M   🔍  ⚙  │
├───────────────────────────────────────────────────────────────────────────────────────────┤
│ ✎ Pointer▾ │ Drag: X-Fade▾ │ Snap: Smart▾ │ ◀◀ ▶ ⏺ ⏸ ⏹ │ Cycle ⤢ │ Count ●│ ⤓ I/O │ ☰ Tools│
│ 101 1 1 00 │ 1:00:12:000 │ ▶ 120.000 4/4 │ Key D♭ │ 2963 smp │ CPU ▓▓░░░ IOH ▓░░ │ ⏻ LLM  │
├───┬───────────────────┬──────────────────────────────────────────────────────────────┬──────┤
│ L │ INSPECTOR   260px │ TRACKS / ARRANGE (ruler h=28:  1   2   3   4 | bars·min·smp) │ LIB  │
│ O │ ┌───────────────┐ │ ┌─Track header─┐                                             │ RAR  │
│ O │ │Region ▸ params│ │ │▮ S7 VoxLd    │  ▁▂▅▇▅▂▁▁▂▅▇▇▅▂▁   ▁▂▅▇▅▂▁                    │ Y    │
│ P │ │ Q▔gain fades  │ │ │ M S R ❄ 🔈   │      ▕region▏      ▕region▏                 │ ┌──┐ │
│ S │ ├───────────────┤ │ ├──────────────┤ ─ ─ ─playhead▼─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─│ │▮▮│ │
│   │ │Track ▸ stack  │ │ │▮ BgVox (stk) │   ▁▃▅▃▁  ▁▃▅▃▁▁▃▅▃▁                          │ │▮▮│ │
│ ▶ │ │ ch strip mini │ │ │ M S R  ▾▾    │                                             │ └──┘ │
│   │ ├───────────────┤ │ ├──────────────┤   ●●●● ●● ●●●● ●●●●●● ●●●●●  (MIDI region)    │      │
│   │ │Smart Controls │ │ │▮ Keys        │  ▕▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▔▏                │ loops│
│   │ │ [knob grid]   │ │ ├──────────────┤ ▕AUTOMATION lane: Volume ▁▁╱▔▔▔╲▁▁▁          │ audio│
│   │ └───────────────┘ │ │ ...          │                                             │ MIDI │
├───┴───────────────────┴──────────────────────────────────────────────────────────────┴──────┤
│ EDITOR DRAWER (h=288, tabs)  [Smart Controls | Piano Roll | Step | Audio | Mixer | LiveLoops]│
│  ┌ Pattern/Audio editor per selection — contextual, single-selection driven                 │
├───────────────────────────────────────────────────────────────────────────────────────────┤
│ PDC: ON  cap 32768  worst=1152smp ●green │ Lane: MAE 96ms │ Zoom  H▒▒▒▒ V▒▒ │ ✓ autosaved    │
└───────────────────────────────────────────────────────────────────────────────────────────┘
```

**Layout numbers (Canvas):** browser strip 64 px (loops button col) / inspector 260 px (220–420) / library 300 px collapsible / editor drawer 288 px (144–60%H, `Y` toggles) / header 260 px (180–480 drag).

---

## 4. Precision Mode — Edit/Mix Split (wireframe) (UIW-03)

```text
┌───────────────────────────────────────────────────────────────────────────────────────────┐
│ ◆ seven7 — Midnight City ▸ Alt 000        Workspace: ( Canvas | ●Precision)  ⌃⌘M   🔍  ⚙  │
├───────────────────────────────────────────────────────────────────────────────────────────┤
│ Edit: ◉Slip ○Grid ○Shfl ○Spot │ Nudge [▸ 1 | smp▾] ◀ ▶ │ SmartTool ✎ │ ∞ LinkTL/EditSel    │
│ 101 1 1 00 │ 1:00:12:000.000 │ m:s │ smp │ ⏱ 120.000 │ pre ▣03:00 post ▣03:00 │ ⌾ KBfocus │
├──────────────────────┬────────────────────────────────────────────────────────────────────┤
│ TRACKS (h=60%)       │ EDIT LIST mini (selection: track, start/end/length in samples)     │
│ ┌─ 360px header ───┐ │ Ruler: min:sec.samples  0:01.000   0:02.000   0:03.000 · grid at smp │
│ │▮ S7 VoxLd    ▾grp│ │ .....|........|........|........|........|........|............   │
│ │ in ▸[I1] out ▸B07│ │ ▁▂▅▇▅▂▁▁▂▅▇▇▅▂▁ ▏clip▕ ▁▂▅▇▅▂▁  ▏■grp badges a■                      │
│ │ ins C1 C2 ●─ ●─  │ │ ▕fade▏◣clip gain line▔╲▁▁▕  micro-fade ticks at bounds             │
│ │ snd 1●B12 2●B01  │ │ Playlist ▾  ▏lane1▕ ▏lane2 (comp)▕ ▏lane3▕   takes stack          │
│ │ M S R ⌾ ❄  delay:│ │ Vol ▁▁╱▔▔▔╲▁  Pan ─●───  Mute ─────  (lanes, sample pts)           │
│ │  1152 smp ●grn   │ │                                                                            │
│ ├──────────────────┤ │                                                                             │
│ │▮ BgVox …6 more   │ │                                                                             │
├──────────────────────┴────────────────────────────────────────────────────────────────────┤
│ MIX SPLIT (h=40%, draggable divider; strips scroll w/ tracks)  Groups: a…z ✓ALL SUSP       │
│ VCA: ▮Drums ▮Gtrs        Drums   VoxLd   BgVox   GtrBus  …   B07 VoxBus      Mstr          │
│                     ┌────────┬────────┬────────┬────────┐ ┌────────┐   ┌────────┐          │
│ inserts A–E rows    │A EQ-7  │A —     │A Compr │A EQ-7  │ …│A —     │   │A L2 LIM│          │
│ (click = open)      │B Compr │B —     │B —     │B Sat   │  │B —     │   │B —     │          │
│                     │S1 B12 -│S1 —    │S1 B12  │S1 —    │  │S1 —    │   │S1 —    │          │
│ sends 1–4 rows      │ pre -6 │        │ post -3│        │  │        │   │        │          │
│ I/O + delay badge   │o I1 B07│o I3 B07│o I5 B07│o —  B09│  │o B07 O1│   │o B09 OM│          │
│ pan (surround puck) │ ⟨·⟩    │ ⟨·⟩    │ ⟨·⟩    │ ⟨·⟩    │  │ ⟨sur⟩  │   │ ⟨sur⟩  │          │
│ GR meter + fader    │─58dB ▐▌│─64    │─60     │─58     │  │─52     │   │▮▮0.0   │          │
│ meters: PK/TP + LUFS│║▐▌║    │║▐ ║    │║▐▌║    │║▐ ║    │  │║▐▌║    │   │║▐▌║ clip│          │
└─────────────────────┴────────┴────────┴────────┴────────┴──┴────────┴───┴────────┴──────────┘
```

**Layout numbers (Precision):** track header 360 px fixed-info (collapses to 260); edit h : mix h default 60:40, divider drag 20–80%; strips narrow 64 / wide 96 px; fader travel 240 px; edit-list column 240 px optional (right side, PT-style) — toggled `⌥=`.

**Reading the design intent:** PT users keep eyes top-left (edit modes, nudge, counters) and bottom (faders); Logic users keep eyes center-left (inspector) — each mode's critical cluster sits exactly where that user's muscle memory expects it.

---

## 5. Edit Tools (UIW-06) — Smart Tool + Marquee contract

### 5.1 Smart Tool (Precision default): three tools by cursor zone

```text
        region/clip
   ┌─────────────────────────────┐
   │ ▏SELECTOR zone (top 50%)▏   │   drag = time-range selection; double-click = select clip
   ├─TRIM─◄  |  ►─TRIM───────────│   edge ±6 px hot zones: trim head/tail (respects edit mode)
   │ ▏GRABBER zone (bottom 50%)▏ │   drag = move/separate clip; ⇧-drag = vertical lock; ⌥-drag = copy
   └─────────────────────────────┘
```

| Zone | Cursor | Gesture | Result (edit-mode aware) |
|---|---|---|---|
| Top half | I-beam | drag | Selector range (snaps per Grid mode) |
| Bottom half | Hand | drag | Grabber move; `T` cycle → Object/Time/Separation grabber variants |
| Head/tail ±6 px | Trim bracket | drag | Trim; with ⌥ = time-stretch trim; targets nearest fade handle first |
| Fade corner badges | Curve glyph | drag | Micro-fade length; right-click = shape menu |
| Clip-gain line | `±dB` pencil | drag line / add point | Clip-gain envelope edit (EDT-05) |

**Marquee contract (both modes):** `⌘`-drag anywhere = marquee selection (transient-aware snapping on); in Canvas, Marquee is also a first-class assignable click tool. Double-marquee-click splits at both ends and selects the middle (Logic behavior preserved verbatim).

### 5.2 Tool matrix & focus

- `T` cycles tool palette; `Esc` shows floating palette; tool state is **per-mode** (Canvas remembers Pointer+Pencil, Precision remembers Smart Tool).
- Nudge cluster (`+`/`-` on keypad, `⌥` duplicates-position): nudge values include **1 sample, 10, 100, 1000 samples**, ms, frames, grid — the precision baseline (EDT-02).
- **Keyboard Focus** (⌾ button, `⌥⌘K`): single-key PT-style commands in Precision (`R/T` zoom, `B` separate, `A/S/D/F/G` trim-assist bank…); Canvas maps the same keys to Logic-alike commands. Conflicts impossible — focus state is visible and per-mode.

### 5.3 Edit modes vs drag modes (UIW-07)

| | Slip | Grid (abs/rel) | Shuffle | Spot |
|---|---|---|---|---|
| Precision | free placement | quantize start/sync point | close gaps on remove | dialog: spot to TC/sample/bar |
| Canvas | Overlap | No-Overlap | X-Fade | — (X-Fade auto-crossfades overlaps) |

Snap menu always offers: Bar / Beat / Division / Ticks / **Samples** / Min:Sec / Frames; "Snap to Zero Crossings" and "Transient ±" toggles. Smart Snap (adaptive) is the Canvas default.

---

## 6. Mixer Window Specification (geometry)

Per-strip stack, top→bottom (PT order; Logic parity of single-click actions):

| Row | H (px) | Contents |
|---|---|---|
| Track badge | 18 | color, name, group letters, VCA assignment, HEAT-style latency badge |
| Inserts A–E (scrollable to J) | 5×20 | slot name, ⌥-click bypass, drag to reorder/copy across strips |
| Sends 1–4 (→10) | 4×20 | destination + level; right-click: pre/post, mute, flp |
| I/O | 2×20 | input path, output path; `⌥`-click cascades |
| Automation mode | 18 | Off/Read/Tch/Ltc/Trim + Write enable dot |
| Pan | 44 | surround/immersive puck (§MIX-10); stereo shows balance dot |
| Record/Solo/Mute Polarity | 22 | R S M Ø buttons |
| Fader + meter | 240 | dB scale −∞…+12, unity detent, GR mini-meter, PK/TP double meter |
| Level readout | 18 | numeric, clip-hold indicator (clear on click) |
| Bottom: delay badge | 16 | e.g. `1152 smp` colored green/amber/red (MIX-07) |

Strip families: Audio / Instrument / Aux / Bus-Master / **VCA** (no audio; darker face, spill button) / Master (red fader cap, dither slot). Sidebar selectors: all/aux/instr/vca/outs; Mix window supports **narrow/wide** per strip and **bank-follow** with EUCon-later/HUI surfaces at launch.

---

## 7. Auxiliary Surfaces

| Surface | Spec |
|---|---|
| Piano roll | Time in bars+samples tooltip; velocity/gain lanes unlimited stacking; brush/line/eraser; MPE per-note curves as overlays; scale/highlight layers; 960 PPQ grid with "display samples" option |
| Step sequencer | Pattern editor drawer: rows (note/automation), 1–64 steps, step rate 1/4–1/64 incl. triplet/dotted, per-row: velocity/gate/tie/repeat 1–8/probability/offset/loop range; pattern region round-trips to Logic (CMP-04) |
| Live Loops | Grid = tracks × scenes; cell states (empty/queued/playing/recording) with quantize-start (Off/Bar/Beat/Cell); performance recorder writes Arrangement regions; drag cell→arrange = new region |
| Smart Controls | Macro panel: screen controls (knob/slider/toggle/pad/XY), mapping list (param path, range, curve, invert, learn); imports Logic layouts 1:1 (CMP-05) |
| Browser | Loops (apple-loop tags incl. key/tempo), files, plug-ins, presets; audition in project key/tempo via MAE lane |
| Sample editor | Destructive-optional: pencil (sample redraw), time/pitch ops, strip-silence, reverse/normalize (create new file + region) |

---

## 8. Keymaps & Accessibility

- **Personalities (global pref):** `seven7 Hybrid (default)`, `Logic Pro`, `Pro Tools`. The *visible* toolbar labels follow the active personality ("R = Record" stays universal; zoom keys swap R/T ⇄ ⌘↑↓). Conflicts resolve against the mode table in §5.2.
- Transport core (all personalities): `Space` play/stop, `,`/`.` locate bar ∓1, `R` record, `L` loop, `Num-Enter` play-from-selection.
- **Accessibility:** full keyboard operability (VoiceOver: every control exposes label+value+unit); luminance contrast ≥ 4.5:1 (ink.1 on bg.panel = 13.4:1); color+glyph redundancy for M/S/R states and PDC badges; tracks identifiable by name in a flat list (screen-reader "track table" virtual view); marquee/smart-tool zones have keyboard equivalents (menu-driven trim/grab of selection edges).
- **Localization:** EN, DE, FR, ES, JA, KO, ZH-Hans at 1.0; dB/time units never localized (studio convention preserved).

---

## 9. Requirements Index (this document)

| ID | Requirement | Acceptance |
|---|---|---|
| UIW-01 | Tokenized design system, 3 densities | Snapshots for 24 components × 3 densities; contrast audit passes |
| UIW-02 | Dual-mode canvas, ⌃⌘M toggle | Mode switch ≤ 250 ms, no dropout; per-mode state round-trips (VSN-M4) |
| UIW-03 | Precision = Edit/Mix split, PT geometry | Layout metrics as §4; mixer always docked in Precision |
| UIW-06 | Smart Tool zone behavior + Marquee ⌘ contract | E2E tool tests: 12 gestures × 4 edit modes |
| UIW-07 | Snap system incl. samples + transient | Snap tolerance exactly 0 at sample setting |
| UIW-09 | Three keymap personalities, mode-aware | No unresolved conflicts; import/export of keymap files |
| UIW-A11Y | WCAG 2.2 AA interactions | VoiceOver script runs full record→edit→mix flow key-only |
