# S7-GLO · Glossary

| Term | Meaning in seven7 |
|---|---|
| **ADC / PDC** | (Plug-in) Delay Compensation — per-path delay insertion so all signals reach a bus time-aligned (MIX-07) |
| **Aux** | Non-timeline strip fed by sends/inputs; FX returns & sub-mixes |
| **Bus / Bus-Master** | Named virtual audio path (1–12 ch); a Bus-Master is the fader strip exposed for a bus |
| **Canvas mode** | The Logic-style creative workspace of the dual-mode canvas (UIW-02) |
| **Clip / Region** | A reference into an immutable source with bounds, fades, clip gain, Flex map. "Clip" used in Precision UI, "Region" in Canvas — same object |
| **Clip gain** | Per-region pre-insert gain envelope with breakpoints (EDT-05) |
| **Clip group** | Pro Tools-style edit grouping linked with mix group attributes (EDT-04) |
| **Comp / comp_map** | The chosen lane segments forming a composite take (EDT-08) |
| **Domain (PDC)** | A set of paths sharing a compensation target at a summing junction (MIX-07) |
| **Flex map** | Non-destructive warp markers + algorithm state on a region (EDT-06) |
| **Hybrid buffer / lanes** | RT lane (armed tracks at hardware block size) + MAE lane (parked tracks rendered ahead in large blocks), stitched sample-aligned (ARC-ENG-01) |
| **LCD / Control bar** | Transport position/tempo readout strip (UIW §1.2) |
| **LLM (Low Latency Mode)** | Bypasses high-latency plug-ins on monitored paths above a user threshold (MIX §4) |
| **L_ahead** | The look-ahead offset of the MAE lane relative to the DAC clock |
| **MAE** | Mix-Ahead Engine — the background lane of the hybrid buffer |
| **MPE** | MIDI Polyphonic Expression (per-note channel data) |
| **Opaque store** | Byte-exact vault of uninterpreted `.logicx` chunks enabling zero-loss re-export (CMP-06) |
| **Playlist / lane** | Alternative takes/edit versions of one track; one lane active (EDT-01) |
| **PPQ** | Pulses per quarter note; seven7 uses 960 PPQ ticks |
| **Precision mode** | The Pro Tools-style Edit/Mix split workspace of the dual-mode canvas (UIW-02) |
| **RCU** | Read-Copy-Update publication of engine graph snapshots (ARC-RT-02) |
| **Smart Controls / Macro Panel** | Logic-style screen-control surface mapped to plug-in parameters (CMP-05) |
| **Smart Tool** | Zone-based Trim/Selector/Grabber compound tool (UIW-06) |
| **Stem-in** | Routing a bus back into a strip input to print sub-mixes (MIX §3) |
| **Strip** | Any mixer channel object (audio/instrument/aux/bus-master/VCA/master) |
| **Summing Stack** | Folder track whose members sub-mix through an auto-created Aux (Logic parity) |
| **VCA** | Control-only fader applying dB offsets to members, post-send coupling per PT semantics (MIX-09) |
| **7.1.2 / 7.1.4** | Immersive speaker layouts: 7 ear-level + LFE + 2/4 overhead channels |
