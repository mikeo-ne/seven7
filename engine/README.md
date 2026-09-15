# seven7 engine

C++20 implementation skeleton of the [S7 Audio Engine](../docs/01-system-architecture.md). This tree is the
first vertical slice: the **decision logic and numeric kernels of the engine, proven by tests**, without yet
binding to real audio hardware. Every unit maps to requirement IDs from the specs.

## Layout → spec traceability

```
engine/
├── src/
│   ├── domain/
│   │   └── time_model.h          ARC-TIME  · PPQ-960 ⇄ sample rational conversion (nearest-even, drift-free)
│   ├── engine/
│   │   ├── lane_scheduler.h      ARC-ENG-01…03 · hybrid buffer lanes (RT / MAE), L_ahead, transitions
│   │   ├── dsp_graph.{h,cpp}     MIX-05, MIX-07 · cycle-safe routing commits, per-junction delay compensation
│   │   └── mix_math.h            MIX-03, MIX-10, ARC-T03 · fader taper, pan law, 64-bit summing
│   └── pal/
│       └── audio_device.h        ARC-C01 · HAL interface + NullAudioDevice (hardware-free pump loop)
├── apps/
│   └── s7engine/main.cpp         docs/01 §9 · headless engine: graph + PDC + deterministic render + checksum
└── tests/                        acceptance suites (zero-dependency harness, CTest)
```

## Build & test

```bash
cmake -B build -S engine
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/s7engine --sample-rate 48000 --seconds 1 --buffer 64
```

Latest local run: **4 suites, ARC-TIME-A1 verifies 72 000 tick⇄sample round-trip identities**
across randomized tempo maps (48 maps × up to 24 segments, 20–500 BPM, 44.1/48/96 kHz).

CI: [`.github/workflows/engine-ci.yml`](../.github/workflows/engine-ci.yml) builds and tests on
Ubuntu (GCC) and macOS (Clang) on every change under `engine/`.

## Roadmap (from skeleton → engine)

- [ ] RT-safe publication of graph snapshots (RCU swap, ARC-RT-02) + lock-free event rings (ARC-RT-03)
- [ ] SIMD kernels for `mix_math.h` (AVX2/NEON) behind a dispatch layer
- [ ] Real PAL backends: CoreAudio (macOS), ASIO / WASAPI-exclusive (Windows)
- [ ] Pull-based executor driving `DspGraph` order with per-stem parallelism
- [ ] MAE lane render-ahead loop + stitch buffer (uses `TransitionPlan` already proven here)
- [ ] `s7engine` bounce mode writing BWF for bit-exact CI diffs
