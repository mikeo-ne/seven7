/* ═══════════════════════════════════════════════════════════════════
   seven7 — live audio engine (Web Audio)
   Faithful ports of the repository kernels:
     · K.faderDb / K.dbToGain  ← engine/src/engine/mix_math.h  (MIX-03)
     · K.panLR                 ← constant_power_pan             (MIX-10)
     · TempoModel              ← engine/src/domain/time_model.h (ARC-TIME, 960 PPQ)
   Channel chain (MIX-01): input → trim → inserts → pan → fader → meter → master
   ═══════════════════════════════════════════════════════════════════ */
'use strict';

/* ─── Engine kernels (JS ports) ─────────────────────────────────── */
const K = {
  faderFloorDb: -144.0,
  /* MIX-03 fader taper: 1.0→+12 dB, 0.75→0 dB, 0.15→−60 dB, floor −144 dB, 0.1 dB steps */
  faderDb(pos) {
    const p = Math.min(1, Math.max(0, pos));
    if (p < 0.005) return -Infinity;
    let db;
    if (p >= 0.75) db = 12.0 * (p - 0.75) / 0.25;
    else if (p >= 0.15) db = -60.0 * (0.75 - p) / 0.60;
    else db = -60.0 - 84.0 * (0.15 - p) / 0.145;
    return Math.round(db * 10) / 10;
  },
  dbToGain(db) {
    if (!isFinite(db) || db <= K.faderFloorDb) return 0;
    return Math.pow(10, db / 20);
  },
  /* MIX-10 constant-power pan law (−3 dB center) */
  panLR(pan) {
    const p = Math.min(1, Math.max(-1, pan));
    const a = (p + 1) * Math.PI / 4;
    return [Math.cos(a), Math.sin(a)];
  },
  dbToPct(db) {
    if (!isFinite(db) || db <= -60) return 0;
    return Math.min(1, (db + 60) / 60);
  },
};

/* ─── Time model (ARC-TIME port, 960 PPQ) ───────────────────────── */
class TempoModel {
  constructor(sampleRate, bpm) {
    this.sr = sampleRate;
    this.segments = [{ tick: 0, sample: 0, bpm }];
  }
  appendSegment(startTick, bpm) {
    const last = this.segments[this.segments.length - 1];
    if (startTick <= last.tick) return last.sample;
    const dt = startTick - last.tick;
    const n = dt * 60 * this.sr;
    const d = 960 * last.bpm;
    this.segments.push({ tick: startTick, sample: last.sample + Math.round(n / d), bpm });
    return this.segments[this.segments.length - 1].sample;
  }
  tickToSample(tick) {
    let s = this.segments[0];
    for (const seg of this.segments) if (seg.tick <= tick) s = seg; else break;
    const dt = tick - s.tick;
    return s.sample + Math.round((dt * 60 * this.sr) / (960 * s.bpm));
  }
  sampleToTick(sample) {
    let s = this.segments[0];
    for (const seg of this.segments) if (seg.sample <= sample) s = seg; else break;
    const ds = sample - s.sample;
    return s.tick + Math.round((ds * 960 * s.bpm) / (60 * this.sr));
  }
  get beatSamples() { return this.tickToSample(960) - this.tickToSample(0); }
}

/* ─── Stock plug-in suite (MIX-12, Web Audio vertical slice) ────── */
const PLUGINS = {
  none:      { name: '— (dry)',       param: null },
  roomworks: { name: 'S7 RoomWorks',  param: 'Mix' },
  echo:      { name: 'S7 Echo',       param: 'Time' },
  comp:      { name: 'S7 Comp',       param: 'Mix' },
  drivesat:  { name: 'S7 TapeSat',    param: 'Drive' },
};

function makeImpulse(ctx, seconds, decay) {
  const len = Math.max(1, Math.floor(ctx.sampleRate * seconds));
  const buf = ctx.createBuffer(2, len, ctx.sampleRate);
  for (let c = 0; c < 2; c++) {
    const d = buf.getChannelData(c);
    for (let i = 0; i < len; i++)
      d[i] = (Math.random() * 2 - 1) * Math.pow(1 - i / len, decay) * (c === 0 ? 1 : 0.97);
  }
  return buf;
}

function driveCurve(ctx, amount) {
  const n = 1024, curve = new Float32Array(n);
  for (let i = 0; i < n; i++) {
    const x = (i / (n - 1)) * 2 - 1;
    curve[i] = Math.tanh(amount * x) / Math.tanh(amount);
  }
  return curve;
}

/** Build one insert slot (parallel dry/wet, PT-style mix). Returns {in, out, setParam, setBypass, setMix}. */
function buildInsert(ctx, type, param01, reverbBuf) {
  const o = { in: null, out: null };
  if (!type || type === 'none') {
    o.in = o.out = ctx.createGain();
    o.setParam = () => {};
    o.setBypass = () => {};
    o.setMix = () => {};
    return o;
  }
  const dry = ctx.createGain(), wet = ctx.createGain(), out = ctx.createGain();
  o.in = ctx.createGain();
  o.in.connect(dry); dry.connect(out);
  o.out = out;

  if (type === 'roomworks') {
    const predelay = ctx.createDelay(0.1); predelay.delayTime.value = 0.012;
    const conv = ctx.createConvolver();
    conv.buffer = reverbBuf || makeImpulse(ctx, 2.4, 3.2);
    o.in.connect(predelay); predelay.connect(conv); conv.connect(wet); wet.connect(out);
    o.setParam = (v) => o.setMix(v);
    o.setMix = (v) => {
      const p = Math.min(1, Math.max(0, v));
      wet.gain.value = Math.sin(p * Math.PI / 2);
      dry.gain.value = Math.cos(p * Math.PI / 2);
    };
  } else if (type === 'echo') {
    const delay = ctx.createDelay(2.0);
    const lp = ctx.createBiquadFilter(); lp.type = 'lowpass'; lp.frequency.value = 3200;
    const fb = ctx.createGain(); fb.gain.value = 0.38;
    o.in.connect(delay); delay.connect(lp); lp.connect(fb); fb.connect(delay);
    delay.connect(wet); wet.connect(out);
    o.setParam = (v) => {
      delay.delayTime.setTargetAtTime(0.02 + Math.min(1, Math.max(0, v)) * 1.1, ctx.currentTime, 0.03);
      wet.gain.value = 1;
    };
    o.setMix = (v) => { wet.gain.value = Math.min(1, Math.max(0, v)); };
  } else if (type === 'comp') {
    const c = ctx.createDynamicsCompressor();
    c.threshold.value = -24; c.knee.value = 12; c.ratio.value = 4;
    c.attack.value = 0.01; c.release.value = 0.18;
    o.in.connect(c); c.connect(wet); wet.connect(out);
    o.setParam = (v) => o.setMix(v);
    o.setMix = (v) => {
      const p = Math.min(1, Math.max(0, v));
      wet.gain.value = Math.sin(p * Math.PI / 2);
      dry.gain.value = Math.cos(p * Math.PI / 2);
    };
  } else if (type === 'drivesat') {
    const ws = ctx.createWaveShaper();
    ws.curve = driveCurve(ctx, 1 + 6 * Math.min(1, Math.max(0, param01)));
    ws.oversample = '2x';
    o.in.connect(ws); ws.connect(wet); wet.connect(out);
    o.setParam = (v) => { ws.curve = driveCurve(ctx, 1 + 6 * Math.min(1, Math.max(0, v))); wet.gain.value = 1; };
    o.setMix = (v) => { wet.gain.value = Math.min(1, Math.max(0, v)); };
  }
  o.setBypass = (b) => {
    if (b) { dry.gain.value = 1; wet.gain.value = 0; }
    else o.setParam(param01);
  };
  if (type === 'echo') o.setParam(param01);
  else if (type !== 'roomworks' && type !== 'comp') o.setParam(param01);
  else o.setMix(param01);
  return o;
}

/* ─── AudioWorklet capture processor (record tap, post-trim) ────── */
const CAPTURE_WORKLET_SRC = `
class S7Capture extends AudioWorkletProcessor {
  constructor() {
    super();
    this.acc = new Float32Array(8192);
    this.n = 0;
    this.port.onmessage = (e) => { if (e.data === 'flush' && this.n > 0) this.emit(this.n); };
  }
  emit(len) {
    const copy = new Float32Array(len);
    copy.set(this.acc.subarray(0, len));
    this.port.postMessage({ len: len, data: copy.buffer }, [copy.buffer]);
    this.acc = new Float32Array(8192);
    this.n = 0;
  }
  process(inputs) {
    const ch = inputs[0] && inputs[0][0];
    if (ch && ch.length) {
      if (this.n + ch.length <= this.acc.length) {
        this.acc.set(ch, this.n); this.n += ch.length;
      } else {
        this.emit(this.acc.length);
        this.acc.set(ch, 0); this.n = ch.length;
      }
    }
    return true;
  }
}
registerProcessor('s7-capture', S7Capture);
`;

/* ─── Tracks ────────────────────────────────────────────────────── */
let trackSeq = 1;
const TRACK_COLORS = ['#5b8dd6', '#d67fb2', '#67c587', '#e0a34e', '#9b7fd6', '#5bc8c8', '#d66f6f', '#8f9bb3'];

class Track {
  constructor(name, color, group) {
    this.id = trackSeq++;
    this.name = name;
    this.color = color;
    this.group = group || '';
    this.faderPos = 0.75;      /* 0 dB unity (MIX-03) */
    this.panVal = 0;
    this.trimDb = 0;           /* ±24 dB input trim (MIX-01) */
    this.muted = false;
    this.soloed = false;
    this.armed = false;
    this.hasInput = false;     /* mic assigned */
    this.monitorOn = false;    /* PT "I/O" manual; auto = monitor when stopped+armed */
    this.clips = [];
    this.inserts = [
      { type: 'none', param: 0.5, bypass: false },
      { type: 'none', param: 0.5, bypass: false },
    ];
    this.recCounter = 0;
    this.clipSeq = 1;
    this.nodes = null;
  }
}

/* ─── The engine ────────────────────────────────────────────────── */
class S7Engine {
  constructor() {
    this.ctx = null;
    this.bufferSize = 256;
    this.tracks = [];
    this.masterPos = 0.75;
    this.masterNodes = null;
    this.inputState = 'off';   /* off | pending | ready | denied */
    this.inputStream = null;
    this.workletReady = false;
    this._workletUrl = null;

    /* transport */
    this.playing = false;
    this.posSamples = 0;       /* stored/display position (wrapped by loop) */
    this.startPosSamples = 0;
    this.startCtxTime = 0;
    this.bpm = 120;
    this.beatsPerBar = 4;
    this.timeModel = null;
    this.loop = { on: false, in: 0, out: 0 };
    this.countIn = false;
    this.metronome = false;
    this.recordPending = false;
    this.recording = false;
    this.recStartSample = 0;
    this.recBuf = new Map();   /* trackId → {chunks:[Float32Array]} */

    /* scheduler state */
    this.schedTimer = null;
    this.sources = new Map();  /* src → {key, endAbs, startAbs, track} */
    this.lastOcc = new Map();
    this.clickBufs = {};

    this.onChange = null;      /* UI callback */
    this.onRecordingUI = null;
  }

  notify() { if (this.onChange) this.onChange(); }

  async init(bufferSize) {
    this.bufferSize = bufferSize || 256;
    this.ctx = new AudioContext({ latencyHint: 'interactive', bufferHint: this.bufferSize });
    try {
      if (!this._workletUrl) {
        this._workletUrl = URL.createObjectURL(
          new Blob([CAPTURE_WORKLET_SRC], { type: 'application/javascript' }));
      }
      await this.ctx.audioWorklet.addModule(this._workletUrl);
      this.workletReady = true;
    } catch (e) { this.workletReady = false; }
    this.timeModel = new TempoModel(this.ctx.sampleRate, this.bpm);
    this.buildMaster();
    this.makeClicks();
    for (const t of this.tracks) this.buildTrackNodes(t);
    return this.ctx;
  }

  get sr() { return this.ctx ? this.ctx.sampleRate : 48000; }
  get running() { return !!(this.ctx && this.ctx.state === 'running'); }

  async ensureRunning() {
    if (!this.ctx) { await this.init(this.bufferSize); }
    if (this.ctx.state !== 'running') {
      try { await this.ctx.resume(); } catch (e) { /* gesture required */ }
    }
    return this.running;
  }

  async setBufferSize(n) {
    if (!this.ctx || n === this.bufferSize) return;
    const wasPlaying = this.playing;
    const pos = this.posSamples;
    if (this.playing) this.stopSync();
    this.bufferSize = n;
    try { await this.ctx.close(); } catch (e) {}
    await this.init(n);
    this.posSamples = pos;
    if (wasPlaying) this.startPlay({});
  }

  /* ── master chain (MIX-01 tail: master fader → protected limiter) ── */
  buildMaster() {
    const ctx = this.ctx;
    const fader = ctx.createGain();
    fader.gain.value = K.dbToGain(K.faderDb(this.masterPos));
    const lim = ctx.createDynamicsCompressor();
    lim.threshold.value = -1; lim.knee.value = 0; lim.ratio.value = 16;
    lim.attack.value = 0.001; lim.release.value = 0.06;
    const analyser = ctx.createAnalyser();
    analyser.fftSize = 2048; analyser.smoothingTimeConstant = 0;
    fader.connect(lim); lim.connect(analyser); analyser.connect(ctx.destination);
    this.clickGain = ctx.createGain(); this.clickGain.gain.value = 0.5;
    this.clickGain.connect(lim);
    this.masterNodes = { fader, lim, analyser };
  }

  makeClicks() {
    const sr = this.sr, n = Math.floor(sr * 0.035);
    for (const [k, f] of [['hi', 1174], ['lo', 830]]) {
      const b = this.ctx.createBuffer(1, n, sr);
      const d = b.getChannelData(0);
      for (let i = 0; i < n; i++) d[i] = Math.sin(2 * Math.PI * f * i / sr) * Math.exp(-i / (n * 0.18));
      this.clickBufs[k] = b;
    }
  }

  /* ── per-track channel strip (MIX-01) ── */
  buildTrackNodes(t) {
    if (!this.ctx || !this.tracks.includes(t)) return;
    const ctx = this.ctx;
    if (t.nodes) for (const key of Object.keys(t.nodes)) { try { t.nodes[key].disconnect(); } catch (e) {} }
    const n = {};
    n.trim = ctx.createGain();
    n.monGate = ctx.createGain();
    n.merge = ctx.createGain();
    n.insA = buildInsert(ctx, t.inserts[0].type, t.inserts[0].param);
    n.insB = buildInsert(ctx, t.inserts[1].type, t.inserts[1].param);
    n.pan = ctx.createStereoPanner();
    n.fader = ctx.createGain();
    n.sm = ctx.createGain();
    n.analyser = ctx.createAnalyser();
    n.analyser.fftSize = 2048; n.analyser.smoothingTimeConstant = 0;
    if (t.hasInput && this.inputStream) {
      n.mic = ctx.createMediaStreamSource(this.inputStream);
      n.mic.connect(n.trim);
    }
    n.trim.connect(n.monGate); n.monGate.connect(n.merge);
    n.merge.connect(n.insA.in); n.insA.out.connect(n.insB.in);
    n.insB.out.connect(n.pan); n.pan.connect(n.fader); n.fader.connect(n.sm);
    n.sm.connect(n.analyser); n.analyser.connect(this.masterNodes.fader);
    if (this.workletReady) {
      n.cap = new AudioWorkletNode(ctx, 's7-capture',
        { numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [1] });
      n.cap.port.onmessage = (e) => this.onCapture(t.id, e.data);
      n.zero = ctx.createGain(); n.zero.gain.value = 0;
      n.trim.connect(n.cap); n.cap.connect(n.zero); n.zero.connect(this.masterNodes.fader);
    }
    t.nodes = n;
    n.insA.setBypass(t.inserts[0].bypass);
    n.insB.setBypass(t.inserts[1].bypass);
    this.applyTrackGain(t);
  }

  applyTrackGain(t) {
    if (!t.nodes) return;
    const n = t.nodes;
    n.trim.gain.value = K.dbToGain(t.trimDb);
    n.pan.panValue = t.panVal;
    n.fader.gain.value = K.dbToGain(K.faderDb(t.faderPos));
    const soloActive = this.tracks.some((x) => x.soloed);
    n.sm.gain.value = (t.muted || (soloActive && !t.soloed)) ? 0 : 1;
    const autoMon = t.armed && !this.playing;
    n.monGate.gain.value = (t.hasInput && (t.monitorOn || autoMon)) ? 1 : 0;
  }
  applyAllGains() { for (const t of this.tracks) this.applyTrackGain(t); }

  setFader(t, pos) { t.faderPos = Math.min(1, Math.max(0, pos)); if (t.nodes) t.nodes.fader.gain.value = K.dbToGain(K.faderDb(t.faderPos)); }
  setPan(t, v) { t.panVal = Math.min(1, Math.max(-1, v)); if (t.nodes) t.nodes.pan.panValue = t.panVal; }
  setTrim(t, db) { t.trimDb = Math.min(24, Math.max(-24, db)); if (t.nodes) t.nodes.trim.gain.value = K.dbToGain(t.trimDb); }
  setMute(t, m) { t.muted = m; this.applyTrackGain(t); this.notify(); }
  setSolo(t, s) { t.soloed = s; this.applyAllGains(); this.notify(); }
  setArmed(t, a) { t.armed = a; this.applyTrackGain(t); this.notify(); }

  /* ── input (mic) ── */
  async enableInput(t) {
    if (this.inputState !== 'ready') {
      this.inputState = 'pending'; this.notify();
      try {
        this.inputStream = await navigator.mediaDevices.getUserMedia({
          audio: { echoCancellation: false, noiseSuppression: false, autoGainControl: false },
        });
        this.inputState = 'ready';
      } catch (e) { this.inputState = 'denied'; }
      this.notify();
    }
    if (this.inputState === 'ready' && !t.hasInput) {
      t.hasInput = true;
      this.buildTrackNodes(t);
      this.notify();
    }
  }

  /* ── inserts ── */
  setInsert(t, slot, type) {
    t.inserts[slot].type = type;
    const old = t.nodes['ins' + (slot === 0 ? 'A' : 'B')];
    if (t.nodes) {
      const ni = buildInsert(this.ctx, type, t.inserts[slot].param);
      const prev = t.nodes.merge;
      const nextIn = slot === 0 ? t.nodes.insB.in : t.nodes.pan;
      if (slot === 0) { t.nodes.monGate.disconnect(); t.nodes.monGate.connect(ni.in); t.nodes.insA = ni; }
      else { t.nodes.insA.out.disconnect(); t.nodes.insA.out.connect(ni.in); t.nodes.insB = ni; }
      ni.out.connect(nextIn);
      ni.setBypass(t.inserts[slot].bypass);
    }
    this.notify();
  }
  setInsertParam(t, slot, v01) {
    t.inserts[slot].param = v01;
    const n = t.nodes && t.nodes['ins' + (slot === 0 ? 'A' : 'B')];
    if (n && !t.inserts[slot].bypass) n.setParam(v01);
  }
  setInsertBypass(t, slot, b) {
    t.inserts[slot].bypass = b;
    const n = t.nodes && t.nodes['ins' + (slot === 0 ? 'A' : 'B')];
    if (n) n.setBypass(b);
    this.notify();
  }

  /* ── transport ── */
  barSamples() {
    if (!this.timeModel) return Math.round(this.sr * 60 / this.bpm * this.beatsPerBar);
    return this.timeModel.tickToSample(960 * this.beatsPerBar) - this.timeModel.tickToSample(0);
  }

  setBpm(bpm) {
    bpm = Math.min(300, Math.max(20, Math.round(bpm * 1000) / 1000));
    const wasBpm = this.bpm;
    this.bpm = bpm;
    if (this.timeModel) {
      if (this.posSamples <= 0 && this.posSamples >= 0) this.timeModel = new TempoModel(this.sr, bpm);
      else this.timeModel.appendSegment(this.timeModel.sampleToTick(Math.max(0, this.posSamples)), bpm);
    }
    if (this.loop.out > 0 && this.loop.in === 0) {
      /* keep loop length in bars: recompute out if it was bar-aligned from zero */
    }
    this.notify();
  }

  startPlay(opts = {}) {
    if (!this.ctx || this.playing) return;
    const ctx = this.ctx;
    this.ensureRunning();
    const basePos = this.posSamples;
    let playFrom = basePos;
    const counting = this.countIn;
    const countEnd = counting ? basePos : null;
    if (counting) playFrom = Math.max(0, basePos - this.barSamples());

    this.recordPending = !!opts.record;
    this.recording = false;
    if (opts.record) {
      this.recStartSample = this.loop.on && basePos >= this.loop.in && basePos < this.loop.out
        ? this.loop.in
        : (countEnd !== null ? basePos : basePos);
      for (const t of this.tracks) if (t.armed) this.recBuf.set(t.id, { chunks: [] });
    }
    this.countEnd = countEnd;
    this.countStart = counting ? playFrom : null;

    this.startPosSamples = playFrom;
    this.startCtxTime = ctx.currentTime + 0.08;
    this.playing = true;
    this.lastOcc.clear();
    this.sources.clear();
    this.applyAllGains();
    this.schedTimer = setInterval(() => this.tick(), 100);
    this.notify();
    this.tick();
  }

  stopSync() {
    if (!this.ctx) return;
    if (this.schedTimer) { clearInterval(this.schedTimer); this.schedTimer = null; }
    this.playing = false;
    this.recordPending = false;
    this.killSources();
    this.applyAllGains();
    this.notify();
  }

  async stop() {
    if (!this.playing && !this.recording) return;
    const wasRec = this.recording;
    this.stopSync();
    if (wasRec) await this.finalizeRecording();
    this.recording = false;
    this.notify();
  }

  currentAbs() {
    if (!this.playing) return this.posSamples;
    let p = this.startPosSamples + (this.ctx.currentTime - this.startCtxTime) * this.sr;
    return Math.max(0, p);
  }
  displayPos(abs) {
    if (this.loop.on && abs >= this.loop.out) {
      const span = this.loop.out - this.loop.in;
      if (span > 0) return this.loop.in + (abs - this.loop.in) % span;
    }
    return abs;
  }

  async seek(sample) {
    if (!this.ctx) return;
    sample = Math.max(0, sample);
    this.posSamples = sample;
    if (this.playing) {
      this.killSources();
      this.startPosSamples = sample;
      this.startCtxTime = this.ctx.currentTime + 0.02;
      this.lastOcc.clear();
      if (this.recording) this.abortRecording();
    }
    this.notify();
  }

  killSources() {
    for (const [src, meta] of this.sources) { try { src.stop(); } catch (e) {} }
    this.sources.clear();
  }

  abortRecording() {
    this.recording = false;
    this.recBuf.clear();
  }

  /* ── scheduler tick (look-ahead ≈ 1.1 s, covers background-tab timer throttling) ── */
  tick() {
    if (!this.playing) return;
    const ctx = this.ctx;
    const abs = this.currentAbs();
    if (abs < this.startPosSamples - 1) return; /* not started yet */
    this.posSamples = this.displayPos(abs);

    if (this.recordPending && !this.recording && abs >= this.recStartSample) {
      this.recordPending = false;
      this.recording = true;
      if (this.onRecordingUI) this.onRecordingUI(true);
      this.notify();
    }

    const span = this.loop.on ? this.loop.out - this.loop.in : Infinity;
    const tUntil = abs + 1.1 * this.sr;

    /* loop wrap: cut tails of clips crossing the loop-out */
    if (span < Infinity && this.prevAbs !== undefined && abs >= this.loop.out && this.prevAbs < this.loop.out) {
      for (const [src, meta] of this.sources) {
        if (meta.startAbs < this.loop.out && meta.endAbs > this.loop.out) {
          try { src.stop(); } catch (e) {}
        }
      }
    }
    this.prevAbs = abs;

    /* clips */
    for (const t of this.tracks) {
      for (const c of t.clips) {
        const base = c.start;
        const key = t.id + ':' + c.id;
        if (span < Infinity) {
          let k = Math.max(0, Math.ceil((abs - base) / span - 1e-9));
          const last = this.lastOcc.get(key);
          if (last !== undefined && k <= last) k = last + 1;
          while (base + k * span < tUntil) {
            this.scheduleSource(t, c, base + k * span, abs);
            this.lastOcc.set(key, k);
            k++;
          }
        } else {
          const last = this.lastOcc.get(key);
          if (last !== 0 && base >= abs - 1 && base < tUntil) {
            this.scheduleSource(t, c, base, abs);
            this.lastOcc.set(key, 0);
          }
        }
      }
    }

    /* metronome / count-in */
    const counting = this.countStart !== null && abs < this.countEnd;
    if (this.metronome || counting) this.scheduleClicks(abs, tUntil, counting, countEnd ? this.countEnd : null);

    /* stop at loop-out when no loop? (PT keeps playing) — keep playing. */
  }

  scheduleSource(t, c, absAt, absNow) {
    const ctx = this.ctx;
    const t0 = this.startCtxTime + (absAt - this.startPosSamples) / this.sr;
    if (t0 < ctx.currentTime - 0.01) return; /* too late */
    const src = ctx.createBufferSource();
    src.buffer = c.buffer;
    src.connect(t.nodes ? t.nodes.merge : this.masterNodes.fader);
    const endAbs = absAt + c.buffer.length;
    src.start(t0);
    src.stop(t0 + c.buffer.duration);
    const meta = { key: t.id + ':' + c.id, endAbs, startAbs: absAt, track: t };
    this.sources.set(src, meta);
    src.onended = () => { if (this.sources.get(src) === meta) this.sources.delete(src); };
  }

  scheduleClicks(abs, tUntil, counting, hardEnd) {
    const ctx = this.ctx;
    const tm = this.timeModel;
    const barTick = 960 * this.beatsPerBar;
    let firstTick = Math.ceil(abs / (tm.tickToSample(960))) * 960;
    for (let tick = firstTick; ; tick += 960) {
      const smp = tm.tickToSample(tick);
      if (smp >= tUntil) break;
      if (smp < abs - 1) continue;
      if (hardEnd !== null && smp >= hardEnd) break;
      const t0 = this.startCtxTime + (smp - this.startPosSamples) / this.sr;
      if (t0 < ctx.currentTime) continue;
      const accent = (tick % barTick) === 0;
      const src = ctx.createBufferSource();
      src.buffer = this.clickBufs[accent ? 'hi' : 'lo'];
      src.connect(this.clickGain);
      src.start(t0);
    }
  }

  /* ── recording ─ */
  onCapture(trackId, msg) {
    const rec = this.recBuf.get(trackId);
    if (this.recording && rec) {
      const arr = new Float32Array(msg.data);
      if (arr.length > 0) rec.chunks.push(arr);
    }
  }

  async finalizeRecording() {
    /* flush worklet buffers */
    for (const t of this.tracks) {
      if (t.nodes && t.nodes.cap) { try { t.nodes.cap.port.postMessage('flush'); } catch (e) {} }
    }
    await new Promise((r) => setTimeout(r, 180));

    for (const t of this.tracks) {
      const rec = this.recBuf.get(t.id);
      if (!t.hasInput || !rec || rec.chunks.length === 0) continue;
      let total = 0;
      for (const ch of rec.chunks) total += ch.length;
      const data = new Float32Array(total);
      let off = 0;
      for (const ch of rec.chunks) { data.set(ch, off); off += ch.length; }
      const buf = this.ctx.createBuffer(1, Math.max(1, total), this.sr);
      buf.copyToChannel(data, 0);
      const clip = {
        id: t.clipSeq++,
        name: 'Take ' + (++t.recCounter),
        start: this.recStartSample,
        buffer: buf,
        peaks: computePeaks(buf, 1024),
      };
      t.clips.push(clip);
      t.clips.sort((a, b) => a.start - b.start);
    }
    this.recBuf.clear();
    this.notify();
  }

  /* ── demo loop (synthesised) ── */
  loadDemo() {
    if (!this.ctx) return;
    const sr = this.sr;
    const spb = 60 / this.bpm;
    const bars = 4;
    const toBuf = (s) => {
      const b = this.ctx.createBuffer(1, s.length, this.sr);
      const dd = b.getChannelData(0);
      for (let i = 0; i < s.length; i++) dd[i] = s.data[i] * s.gain;
      return b;
    };
    const drumBuf = toBuf(synthDrums(sr, bars, spb));
    const bassBuf = toBuf(synthBass(sr, bars, spb));
    const need = Math.max(1, this.tracks.length);
    while (this.tracks.length < 2) this.addTrack();
    const [t0, t1] = this.tracks;
    t0.name = 'Drums'; t0.clips = [{ id: t0.clipSeq++, name: 'Drums 1–4', start: 0, buffer: drumBuf, peaks: computePeaks(drumBuf, 1024) }];
    t1.name = 'Bass';   t1.clips = [{ id: t1.clipSeq++, name: 'Bass 1–4', start: 0, buffer: bassBuf, peaks: computePeaks(bassBuf, 1024) }];
    this.loop = { on: true, in: 0, out: Math.round(bars * this.beatsPerBar * spb * sr) };
    this.posSamples = 0;
    this.notify();
  }

  /* ── tracks ─ */
  addTrack(name) {
    const t = new Track(
      name || 'Track ' + (this.tracks.length + 1),
      TRACK_COLORS[this.tracks.length % TRACK_COLORS.length],
      'abcdefghijklmnopqrstuvwxyz'[this.tracks.length % 26] || ''
    );
    this.tracks.push(t);
    if (this.ctx) this.buildTrackNodes(t);
    this.notify();
    return t;
  }
  removeTrack(t) {
    const i = this.tracks.indexOf(t);
    if (i < 0) return;
    if (t.nodes) for (const k of Object.keys(t.nodes)) { try { t.nodes[k].disconnect(); } catch (e) {} }
    this.tracks.splice(i, 1);
    this.recBuf.delete(t.id);
    this.applyAllGains();
    this.notify();
  }

  /* ── bounce (offline render, disk clips only) ── */
  async bounce(rangeStart, rangeLen) {
    const sr = 48000;
    const off = new OfflineAudioContext(2, Math.max(sr, Math.ceil(rangeLen)), sr);
    const mFader = off.createGain();
    mFader.gain.value = K.dbToGain(K.faderDb(this.masterPos));
    const lim = off.createDynamicsCompressor();
    lim.threshold.value = -1; lim.knee.value = 0; lim.ratio.value = 16;
    lim.attack.value = 0.001; lim.release.value = 0.06;
    mFader.connect(lim); lim.connect(off.destination);
    const soloActive = this.tracks.some((x) => x.soloed);
    for (const t of this.tracks) {
      const trim = off.createGain(); trim.gain.value = K.dbToGain(t.trimDb);
      const a = buildInsert(off, t.inserts[0].type, t.inserts[0].param);
      const b = buildInsert(off, t.inserts[1].type, t.inserts[1].param);
      a.setBypass(t.inserts[0].bypass); b.setBypass(t.inserts[1].bypass);
      const pan = off.createStereoPanner(); pan.panValue = t.panVal;
      const fad = off.createGain(); fad.gain.value = K.dbToGain(K.faderDb(t.faderPos));
      const sm = off.createGain();
      sm.gain.value = (t.muted || (soloActive && !t.soloed)) ? 0 : 1;
      trim.connect(a.in); a.out.connect(b.in); b.out.connect(pan);
      pan.connect(fad); fad.connect(sm); sm.connect(mFader);
      for (const c of t.clips) {
        const cEnd = c.start + c.buffer.length;
        if (cEnd <= rangeStart || c.start >= rangeStart + rangeLen) continue;
        const src = off.createBufferSource();
        src.buffer = c.buffer;
        src.connect(trim);
        const t0 = (c.start - rangeStart) / sr;
        const offset = c.start > rangeStart ? (c.start - rangeStart) / c.buffer.sampleRate : 0;
        src.start(t0, offset);
        src.stop(t0 + (cEnd - rangeStart) / sr);
      }
    }
    return off.startRendering();
  }

  latencyMs() {
    if (!this.ctx) return 0;
    const b = this.ctx.baseLatency || 0;
    const o = this.ctx.outputLatency || 0;
    return (b + o) * 1000;
  }
}

/* ─── Peaks + WAV ───────────────────────────────────────────────── */
function computePeaks(buffer, nBuckets) {
  const n = buffer.length;
  const out = new Float32Array(nBuckets * 2);
  const ch = [];
  for (let c = 0; c < buffer.numberOfChannels; c++) ch.push(buffer.getChannelData(c));
  const per = n / nBuckets;
  for (let b = 0; b < nBuckets; b++) {
    let mn = 1, mx = -1;
    const s = Math.floor(b * per), e = Math.min(n, Math.floor((b + 1) * per) + 1);
    for (let i = s; i < e; i++) {
      for (let c = 0; c < ch.length; c++) {
        const v = ch[c][i];
        if (v < mn) mn = v;
        if (v > mx) mx = v;
      }
    }
    if (mn > 0) mn = 0;
    if (mx < 0) mx = 0;
    out[b * 2] = mn;
    out[b * 2 + 1] = mx;
  }
  return out;
}

function encodeWav(buffer, bits = 16) {
  const numCh = Math.min(2, buffer.numberOfChannels);
  const len = buffer.length;
  const bytesPerSample = bits / 8;
  const blockAlign = numCh * bytesPerSample;
  const dataSize = len * blockAlign;
  const ab = new ArrayBuffer(44 + dataSize);
  const dv = new DataView(ab);
  const ws = (o, s) => { for (let i = 0; i < s.length; i++) dv.setUint8(o + i, s.charCodeAt(i)); };
  ws(0, 'RIFF'); dv.setUint32(4, 36 + dataSize, true); ws(8, 'WAVE');
  ws(12, 'fmt '); dv.setUint32(16, 16, true); dv.setUint16(20, 1, true);
  dv.setUint16(22, numCh, true); dv.setUint32(24, buffer.sampleRate, true);
  dv.setUint32(28, buffer.sampleRate * blockAlign, true);
  dv.setUint16(32, blockAlign, true); dv.setUint16(34, bits, true);
  ws(36, 'data'); dv.setUint32(40, dataSize, true);
  const chans = [];
  for (let c = 0; c < numCh; c++) chans.push(buffer.getChannelData(c));
  let o = 44;
  for (let i = 0; i < len; i++) {
    for (let c = 0; c < numCh; c++) {
      const s = Math.max(-1, Math.min(1, chans[c][i]));
      dv.setInt16(o, s < 0 ? s * 0x8000 : s * 0x7fff, true);
      o += 2;
    }
  }
  return new Blob([ab], { type: 'audio/wav' });
}

function downloadBlob(blob, name) {
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url; a.download = name;
  document.body.appendChild(a);
  a.click();
  setTimeout(() => { URL.revokeObjectURL(url); a.remove(); }, 4000);
}

/* ─── Demo synthesis ────────────────────────────────────────────── */
function synthDrums(sr, bars, spb) {
  const len = Math.floor(bars * 4 * spb * sr);
  const d = new Float32Array(len);
  const total = bars * 4;
  /* kick on every beat */
  for (let b = 0; b < total; b++) {
    const t0 = Math.floor(b * spb * sr);
    const n = Math.floor(0.3 * sr);
    let phase = 0;
    for (let i = 0; i < n && t0 + i < len; i++) {
      const t = i / sr;
      const f = 45 + (150 - 45) * Math.exp(-t / 0.045);
      phase += 2 * Math.PI * f / sr;
      const amp = Math.exp(-t / 0.075) * 0.95;
      d[t0 + i] += amp * Math.sin(phase);
    }
  }
  /* snare on beats 2 & 4 */
  const noise = new Float32Array(4096);
  for (let i = 0; i < noise.length; i++) noise[i] = Math.random() * 2 - 1;
  for (let b = 0; b < total; b++) {
    if (b % 4 !== 1 && b % 4 !== 3) continue;
    const t0 = Math.floor(b * spb * sr);
    const n = Math.floor(0.18 * sr);
    let pn = 0;
    for (let i = 0; i < n && t0 + i < len; i++) {
      const t = i / sr;
      pn += noise[i & 4095];
      const hp = pn - pn * 0.98; /* crude highpass */
      d[t0 + i] += Math.exp(-t / 0.045) * hp * 0.55;
      d[t0 + i] += Math.exp(-t / 0.06) * Math.sin(2 * Math.PI * 190 * t) * 0.3;
    }
  }
  /* hats on 8ths */
  for (let e = 0; e < total * 2; e++) {
    const t0 = Math.floor(e * (spb / 2) * sr);
    const n = Math.floor(0.05 * sr);
    let pn = 0;
    const accent = e % 2 === 1;
    for (let i = 0; i < n && t0 + i < len; i++) {
      const t = i / sr;
      pn += noise[(i * 3) & 4095];
      const hp = pn - pn * 0.9;
      d[t0 + i] += Math.exp(-t / 0.009) * hp * (accent ? 0.3 : 0.18);
    }
  }
  let peak = 0;
  for (let i = 0; i < len; i++) peak = Math.max(peak, Math.abs(d[i]));
  const g = peak > 0 ? 0.92 / peak : 1;
  return { data: d, gain: g, length: len };
}

function synthBass(sr, bars, spb) {
  const len = Math.floor(bars * 4 * spb * sr);
  const d = new Float32Array(len);
  const root = 55; /* A1 */
  const pattern = [0, 0, 0, 0, 3, 0, 5, 7]; /* per bar, 8ths */
  for (let bar = 0; bar < bars; bar++) {
    for (let e = 0; e < 8; e++) {
      const semis = pattern[e];
      if (semis === null) continue;
      const f = root * Math.pow(2, semis / 12);
      const t0 = Math.floor((bar * 8 + e) * (spb / 2) * sr);
      const dur = Math.floor(spb / 2 * 0.88 * sr);
      let phase = 0, lp = 0;
      for (let i = 0; i < dur && t0 + i < len; i++) {
        const t = i / sr;
        phase += f / sr;
        const x = 2 * (phase % 1) - 1;
        lp += 0.25 * (x - lp);
        const env = Math.min(1, t / 0.005) * Math.exp(-t / (spb * 0.32));
        d[t0 + i] += lp * env * 0.75;
      }
    }
  }
  let peak = 0;
  for (let i = 0; i < len; i++) peak = Math.max(peak, Math.abs(d[i]));
  const g = peak > 0 ? 0.9 / peak : 1;
  return { data: d, gain: g, length: len };
}
