---
layout: home

hero:
  name: seven7
  text: Logic Pro creativity. Pro Tools precision.
  tagline: The authoritative design blueprint for a next-generation DAW — hybrid-buffer engine, dual-mode canvas, lossless Logic Pro interchange.
  image:
    src: /logo.png
    alt: seven7
  actions:
    - theme: brand
      text: Read the blueprint
      link: /00-vision-positioning-scope
    - theme: alt
      text: System architecture
      link: /01-system-architecture
    - theme: alt
      text: GitHub
      link: https://github.com/mikeo-ne/seven7

features:
  - icon: 🏗️
    title: 01 · System Architecture
    details: Layer/component breakdown, the hybrid-buffer audio engine (RT lane + Mix-Ahead lane), real-time-safe threading, immutable project model, performance budgets.
    link: /01-system-architecture
  - icon: 🎛️
    title: 02 · UI/UX & Main Window
    details: Design tokens, the Dual-Mode Canvas (Canvas ⇄ Precision), pixel-specced wireframes, Smart Tool + Marquee contracts, three keymap personalities.
    link: /02-ui-ux-main-window
  - icon: 🔀
    title: 03 · Mix Engine & Signal Flow
    details: Channel-strip signal flow, named-bus routing matrix, per-junction delay compensation with exact readouts, VCAs, 7.1.4 immersive, metering, stock plug-in suite.
    link: /03-mix-engine-signal-flow
  - icon: ✂️
    title: 04 · Editing, Sequencing & Automation
    details: Sample-accurate editing, micro-fades, clip gain, playlists & comping, Flex Time/Pitch, pattern sequencing, Live Loops, dual-mode automation engine.
    link: /04-editing-sequencing-automation
  - icon: 🔄
    title: 05 · Logic Pro Compatibility
    details: .logicx codec architecture, zero-loss round-trips via opaque-chunk retention, object mapping tables, Smart Controls / Track Stacks / EXS24, conformance program.
    link: /05-logic-pro-compatibility
  - icon: 📖
    title: 06 · Glossary
    details: Shared terminology used across all seven7 specification documents.
    link: /06-glossary
---

## The one-paragraph version

seven7 is engineered around one invariant: **a Logic Pro session and a Pro Tools session are two views over the same project model.**
Compose in the Canvas workspace — fluid arrange, Smart Controls, Live Loops — then press `⌃⌘M` and mix in the Precision workspace with sample-accurate editing, explicit buses, VCAs, and delay-compensation readouts. Nothing is converted; nothing is lost. And because `.logicx` is a first-class file type with a conformance suite, projects move between seven7 and Logic Pro with **zero metadata loss by construction**.

Start with [00 · Vision, Positioning & Scope](./00-vision-positioning-scope.md), or jump straight into the [hybrid-buffer engine](./01-system-architecture.md).
