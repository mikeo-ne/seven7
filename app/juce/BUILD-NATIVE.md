# Building the native seven7 shell (JUCE)

`app/juce/` is a **working vertical slice of the seven7 DAW as a native desktop
app**. It is not a mock: it opens a real audio device, monitors your input
through the repository's engine kernels, records takes, and saves them as WAV —
with the position LCD driven by the spec's time model.

What the audio callback actually runs (from `engine/src`, header-only):

| Kernel | Spec | Role in the shell |
|---|---|---|
| `s7::engine::fader_db` / `db_to_gain` | MIX-03 | Strip fader taper (0 dB unity at 75 % travel, +12 dB top) |
| `s7::engine::constant_power_pan` | MIX-10 | Panner (−3 dB center, sin/cos) |
| `s7::engine::accumulate_bus` / `finalize_bus` | ARC-T03 | 64-bit master summing, single round at output |
| `s7::domain::TempoMap` | ARC-TIME | Bar.beat.frame position (960 PPQ, sample-exact) — GCC/Clang builds |

ARC-RT-01 is honoured: the callback does no heap allocation, no locking, no
I/O — the take buffer is pre-allocated in `prepareToPlay` and control state is
atomics.

## Prerequisites

- **CMake ≥ 3.22**
- A **C++20** toolchain:
  - macOS: Xcode 15+ (`xcode-select --install`)
  - Linux: GCC 11+ or Clang 14+
  - Windows: Visual Studio 2022 (C++20) or clang-cl
- **JUCE 8** — you have this installed. Point the build at your checkout with
  `-DS7_JUCE_ROOT=<path>`. If you skip the flag, CMake clones JUCE 8 for you
  (first configure needs network).

> **MSVC note:** the ARC-TIME `TempoMap` kernel requires `__int128`
> (GCC/Clang). On MSVC the shell compiles with an exact *single-tempo*
> fallback for the position LCD; multi-tempo maps activate automatically on
> GCC/Clang builds.

## Build

```bash
# macOS / Linux
cmake -B build -S app/juce -DCMAKE_BUILD_TYPE=Release -DS7_JUCE_ROOT=/path/to/JUCE
cmake --build build -j 8

# Windows (Developer PowerShell)
cmake -B build -S app/juce -DCMAKE_BUILD_TYPE=Release -DS7_JUCE_ROOT=C:\path\to\JUCE
cmake --build build --config Release -j
```

The JUCE CMake preset `MacOSX`/`windows` toolchain flags are not required;
the defaults build a standalone app.

## Run

| OS | Binary |
|---|---|
| macOS | `build/seven7_artefacts/Release/seven7.app` |
| Linux | `build/seven7` |
| Windows | `build/Release/seven7.exe` (or `build/Debug`) |

First run:

1. If no device is selected, open **⚙ Settings** and pick input + output,
   then press **Apply** (audio restarts automatically).
2. **Monitor** — your input is audible through the **Fader** (MIX-03 taper:
   double-check 0 dB sits at 75 % travel), **Pan** (MIX-10) and **Input trim**.
   No mic handy? Toggle **Test tone**.
3. **▶** starts the position counter (ARC-TIME). Change the BPM and press
   **BPM** — the LCD re-derives the tempo map sample-exactly (tempo change
   mid-playback appends a tempo-map segment, PT-style).
4. **●** records the input (post-trim, pre-fader, PT convention). Press **●**
   again to stop, then **Save take → WAV…** (24-bit WAV).

## Roadmap toward the full DAW (what's next in `app/juce/`)

The web app in [`web/`](../../web) already demonstrates the Precision
workspace (UIW-03): multi-track timeline, clips with waveforms, loop,
count-in, inserts, bounce. Porting that feature set to the native shell is
the planned sequence:

1. **Multi-track model** — replace the single strip with a `juce::AudioBuffer`-backed track list; each strip keeps its own kernel chain (MIX-01).
2. **Clip playback** — lookahead scheduler (the web engine's 1.1 s look-ahead pattern) driving `AudioBufferSource`-style playback from the take files.
3. **Precision UI** — the web app's Pro Tools look (track headers, ruler, docked mixer) as JUCE components.
4. **Routing matrix + PDC** — `s7::engine::DspGraph` (MIX-05/07) is already tested in `engine/` and drops straight into the graph layer.
5. **`.s7proj` codec + Logic import** — per `docs/05`.

See [`docs/`](../../docs) for the full requirements set (00–06).
