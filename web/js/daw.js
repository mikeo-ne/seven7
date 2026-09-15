/* ═══════════════════════════════════════════════════════════════════
   seven7 — Precision workspace UI (Pro Tools look)
   Layout per docs/02 §4 (UIW-03) · mixer stack per §6
   ═══════════════════════════════════════════════════════════════════ */
'use strict';

const S7 = new S7Engine();
const $ = (sel) => document.querySelector(sel);

const RULER_H = 28;
const LANE_H = 68;
const HEADER_W = 300;

const ui = {
  pxPerBar: 160,
  scrollX: 0,
  selectedTrackId: null,
  dragLoop: null,        /* 'in' | 'out' while dragging */
  scrubbing: false,
  masterAnalyser: null,
  meterState: new Map(), /* id → {disp, hold, holdT, arr} */
  toastT: null,
  menuOpen: null,
};

/* ─── toast ─────────────────────────────────────────────────────── */
function toast(msg, ms = 2600) {
  const el = $('#toast');
  el.textContent = msg;
  el.hidden = false;
  clearTimeout(ui.toastT);
  ui.toastT = setTimeout(() => { el.hidden = true; }, ms);
}

/* ─── selection ─────────────────────────────────────────────────── */
function selectTrack(id) {
  ui.selectedTrackId = id;
  S7.notify();
}
function selectedTrack() {
  return S7.tracks.find((t) => t.id === ui.selectedTrackId) || null;
}

/* ─── track headers ─────────────────────────────────────────────── */
function buildHeaders() {
  const box = $('#headers');
  box.innerHTML = '';
  const corner = document.createElement('div');
  corner.id = 'ruler-corner';
  corner.innerHTML = '<span>BAR.BEAT.FRAME</span>';
  box.appendChild(corner);

  for (const t of S7.tracks) {
    const el = document.createElement('div');
    el.className = 'track-head';
    el.style.setProperty('--tc', t.color);
    el.dataset.track = t.id;
    el.innerHTML = `
      <div class="th-color"></div>
      <div class="th-name">
        <span class="th-label"></span><span class="th-grp">${t.group}</span>
        <span class="th-input-dot" title="Input assigned"></span>
      </div>
      <div class="th-row2">
        <button class="th-btn m" title="Mute">M</button>
        <button class="th-btn s" title="Solo">S</button>
        <button class="th-btn r" title="Record-enable">R</button>
        <button class="th-btn i" title="Input / monitor">I</button>
        <button class="th-btn a" title="Auto monitor">A</button>
      </div>
      <div class="th-row3">
        <span class="th-io"></span>
        <span class="th-db"></span>
        <span class="th-pdc">● 0 smp</span>
      </div>`;
    el.querySelector('.th-label').textContent = t.name;

    /* interactions */
    el.addEventListener('pointerdown', () => selectTrack(t.id));
    el.addEventListener('contextmenu', (e) => {
      e.preventDefault();
      selectTrack(t.id);
      showCtxMenu(e.clientX, e.clientY, t);
    });
    el.querySelector('.th-label').addEventListener('dblclick', (e) => {
      e.stopPropagation();
      renameTrack(t);
    });
    el.querySelector('.th-btn.m').addEventListener('click', (e) => { e.stopPropagation(); S7.setMute(t, !t.muted); });
    el.querySelector('.th-btn.s').addEventListener('click', (e) => { e.stopPropagation(); S7.setSolo(t, !t.soloed); });
    el.querySelector('.th-btn.r').addEventListener('click', async (e) => {
      e.stopPropagation();
      const on = !t.armed;
      S7.setArmed(t, on);
      if (on && !t.hasInput) { await S7.ensureRunning(); await S7.enableInput(t); }
    });
    el.querySelector('.th-btn.i').addEventListener('click', async (e) => {
      e.stopPropagation();
      if (!t.hasInput) { await S7.ensureRunning(); await S7.enableInput(t); }
      else { t.hasInput = false; S7.buildTrackNodes(t); S7.notify(); }
    });
    el.querySelector('.th-btn.a').addEventListener('click', (e) => {
      e.stopPropagation();
      t.monitorOn = !t.monitorOn;
      S7.applyTrackGain(t); S7.notify();
    });
    box.appendChild(el);
  }

  const add = document.createElement('button');
  add.className = 'add-track';
  add.textContent = '＋ Add Track';
  add.addEventListener('click', () => { selectTrack(S7.addTrack().id); });
  box.appendChild(add);
  syncHeaders();
  sizeCanvas();
}

function syncHeaders() {
  for (const t of S7.tracks) {
    const el = document.querySelector(`.track-head[data-track="${t.id}"]`);
    if (!el) continue;
    el.classList.toggle('selected', t.id === ui.selectedTrackId);
    el.classList.toggle('has-input', t.hasInput);
    el.querySelector('.th-btn.m').classList.toggle('on', t.muted);
    el.querySelector('.th-btn.s').classList.toggle('on', t.soloed);
    const r = el.querySelector('.th-btn.r');
    r.classList.toggle('on', t.armed);
    r.classList.toggle('blink', t.armed && S7.recording);
    el.querySelector('.th-btn.i').classList.toggle('on', t.hasInput);
    el.querySelector('.th-btn.a').classList.toggle('on', t.monitorOn);
    el.querySelector('.th-io').textContent = 'In: ' + (t.hasInput ? 'Mic 1–2' : '—');
    el.querySelector('.th-db').textContent = isFinite(K.faderDb(t.faderPos)) ? K.faderDb(t.faderPos).toFixed(1) + ' dB' : 'MUTE';
  }
}

function renameTrack(t) {
  const el = document.querySelector(`.track-head[data-track="${t.id}"] .th-name`);
  const label = el.querySelector('.th-label');
  const input = document.createElement('input');
  input.value = t.name;
  label.replaceWith(input);
  input.focus(); input.select();
  const commit = () => {
    t.name = input.value.trim() || t.name;
    S7.notify();
  };
  input.addEventListener('keydown', (e) => { if (e.key === 'Enter') input.blur(); if (e.key === 'Escape') { input.value = t.name; input.blur(); } });
  input.addEventListener('blur', commit);
}

/* ─── mixer strips ──────────────────────────────────────────────── */
function buildStrips() {
  const box = $('#strips');
  box.innerHTML = '';
  for (const t of S7.tracks) box.appendChild(makeStrip(t, false));
  box.appendChild(makeStrip(null, true)); /* master */
  syncStrips();
}

function makeStrip(t, isMaster) {
  const el = document.createElement('div');
  el.className = 'strip' + (isMaster ? ' master' : '');
  if (t) el.dataset.track = t.id;

  if (isMaster) {
    el.innerHTML = `
      <div class="st-name"><i class="chip" style="background:#c0392b"></i>Master</div>
      <div class="st-inserts">
        <div class="slot"><span class="slot-letter">A</span><span class="dim small" style="flex:1;text-align:center">S7 Limiter</span></div>
        <div class="slot"><span class="slot-letter">B</span><span class="dim small" style="flex:1;text-align:center">—</span></div>
      </div>
      <div class="st-sends">S1&nbsp;—&nbsp;&nbsp;S2&nbsp;—</div>
      <div class="st-io"><span>—</span><span>OUT 1–2</span></div>
      <div class="st-panrow"></div>
      <div class="st-rsm"><button class="m" disabled>M</button><button class="s" disabled>S</button></div>
      <div class="st-faderrow"></div>
      <div class="st-db">0.0 dB</div>
      <div class="st-pdc">● 0 smp</div>`;
  } else {
    el.innerHTML = `
      <div class="st-name"><i class="chip" style="background:${t.color}"></i><span class="st-nm"></span><em>${t.group}</em></div>
      <div class="st-inserts">
        ${[0, 1].map((s) => `
        <div class="slot" data-slot="${s}">
          <span class="slot-letter">${s === 0 ? 'A' : 'B'}</span>
          <select class="ins-select">
            ${Object.entries(PLUGINS).map(([k, v]) => `<option value="${k}">${v.name}</option>`).join('')}
          </select>
          <button class="bypass" title="Bypass">·</button>
          <div class="knob-mini" title="Insert parameter"><div class="knob-body"></div><div class="knob-pointer"></div></div>
        </div>`).join('')}
      </div>
      <div class="st-sends">S1&nbsp;—&nbsp;&nbsp;S2&nbsp;—</div>
      <div class="st-io"><span class="io-in"></span><span>→ Mstr</span></div>
      <div class="st-panrow"><div class="knob" title="Pan (MIX-10 constant-power)"><div class="knob-body"></div><div class="knob-pointer"></div></div></div>
      <div class="st-rsm">
        <button class="r" title="Record-enable">R</button>
        <button class="s" title="Solo">S</button>
        <button class="m" title="Mute">M</button>
      </div>
      <div class="st-faderrow"></div>
      <div class="st-db">0.0 dB</div>
      <div class="st-pdc">● 0 smp</div>`;
  }

  /* fader + meter */
  const frow = el.querySelector('.st-faderrow');
  const fader = document.createElement('div');
  fader.className = 'fader';
  fader.innerHTML = `
    <div class="fader-scale">
      <span style="bottom:97%">+12</span><span class="detent" style="bottom:75%">0</span>
      <span style="bottom:63%">-12</span><span style="bottom:51%">-24</span>
      <span style="bottom:39%">-36</span><span style="bottom:27%">-48</span>
      <span style="bottom:15%">-60</span><span style="bottom:1%">-∞</span>
    </div>
    <div class="fader-groove"><div class="fader-thumb"></div></div>`;
  const meter = document.createElement('div');
  meter.className = 'meter';
  meter.innerHTML = '<div class="meter-fill"></div><div class="meter-peak"></div><div class="meter-ticks"></div>';
  frow.appendChild(fader);
  frow.appendChild(meter);
  el._fader = fader; el._thumb = fader.querySelector('.fader-thumb');
  el._meterFill = meter.querySelector('.meter-fill');
  el._meterPeak = meter.querySelector('.meter-peak');
  el._db = el.querySelector('.st-db');

  const faderPos = isMaster
    ? () => S7.masterPos
    : () => t.faderPos;
  const faderSet = (pos) => {
    if (isMaster) {
      S7.masterPos = Math.min(1, Math.max(0, pos));
      if (S7.masterNodes) S7.masterNodes.fader.gain.value = K.dbToGain(K.faderDb(S7.masterPos));
    } else S7.setFader(t, pos);
    S7.notify();
  };
  attachFader(fader, faderPos, faderSet);

  if (!isMaster) {
    el.querySelector('.st-nm').textContent = t.name;
    /* inserts */
    for (const slotEl of el.querySelectorAll('.slot')) {
      const s = Number(slotEl.dataset.slot);
      const sel = slotEl.querySelector('.ins-select');
      const byp = slotEl.querySelector('.bypass');
      sel.addEventListener('change', () => { S7.setInsert(t, s, sel.value); });
      byp.addEventListener('click', () => { S7.setInsertBypass(t, s, !t.inserts[s].bypass); });
      attachKnob(slotEl.querySelector('.knob-mini'),
        () => t.inserts[s].param,
        (v) => { S7.setInsertParam(t, s, v); }, 0, 1, 0.5);
    }
    /* pan */
    attachKnob(el.querySelector('.knob'), () => t.panVal, (v) => S7.setPan(t, v), -1, 1, 0);
    /* R S M */
    el.querySelector('.st-rsm .r').addEventListener('click', async () => {
      const on = !t.armed;
      S7.setArmed(t, on);
      if (on && !t.hasInput) { await S7.ensureRunning(); await S7.enableInput(t); }
    });
    el.querySelector('.st-rsm .s').addEventListener('click', () => S7.setSolo(t, !t.soloed));
    el.querySelector('.st-rsm .m').addEventListener('click', () => S7.setMute(t, !t.muted));
    el.addEventListener('pointerdown', () => selectTrack(t.id));
  }
  return el;
}

function syncStrips() {
  const box = $('#strips');
  for (const t of S7.tracks) {
    const el = box.querySelector(`.strip[data-track="${t.id}"]`);
    if (!el) continue;
    el._thumb.style.top = ((1 - t.faderPos) * 100).toFixed(2) + '%';
    el._db.textContent = isFinite(K.faderDb(t.faderPos)) ? K.faderDb(t.faderPos).toFixed(1) + ' dB' : 'MUTE';
    el.querySelector('.st-nm').textContent = t.name;
    el.querySelector('.st-io .io-in').textContent = t.hasInput ? 'Mic 1–2' : '—';
    el.querySelector('.st-rsm .r').classList.toggle('on', t.armed);
    el.querySelector('.st-rsm .s').classList.toggle('on', t.soloed);
    el.querySelector('.st-rsm .m').classList.toggle('on', t.muted);
    const soloActive = S7.tracks.some((x) => x.soloed);
    el.classList.toggle('dimmed', soloActive && !t.soloed);
    for (const slotEl of el.querySelectorAll('.slot')) {
      const s = Number(slotEl.dataset.slot);
      slotEl.querySelector('.ins-select').value = t.inserts[s].type;
      slotEl.querySelector('.bypass').classList.toggle('on', t.inserts[s].bypass);
      syncKnob(slotEl.querySelector('.knob-mini'), t.inserts[s].param, 0, 1);
    }
    syncKnob(el.querySelector('.knob'), t.panVal, -1, 1);
  }
  const m = box.querySelector('.strip.master');
  if (m) {
    m._thumb.style.top = ((1 - S7.masterPos) * 100).toFixed(2) + '%';
    m._db.textContent = isFinite(K.faderDb(S7.masterPos)) ? K.faderDb(S7.masterPos).toFixed(1) + ' dB' : 'MUTE';
  }
  /* group letters */
  const gl = $('#group-letters');
  gl.innerHTML = '';
  const letters = 'ab';
  for (const L of letters) {
    const s = document.createElement('span');
    s.textContent = L;
    s.classList.toggle('used', S7.tracks.some((t) => t.group === L));
    gl.appendChild(s);
  }
}

/* ─── fader + knob dragging ─────────────────────────────────────── */
function attachFader(el, getPos, setPos) {
  const groove = el.querySelector('.fader-groove');
  el.addEventListener('pointerdown', (e) => {
    e.preventDefault();
    el.setPointerCapture(e.pointerId);
    const move = (ev) => {
      const r = groove.getBoundingClientRect();
      const pos = 1 - (ev.clientY - r.top) / r.height;
      setPos(Math.min(1, Math.max(0, pos)));
    };
    move(e);
    const up = (ev) => {
      el.releasePointerCapture(ev.pointerId);
      el.removeEventListener('pointermove', move);
      el.removeEventListener('pointerup', up);
    };
    el.addEventListener('pointermove', move);
    el.addEventListener('pointerup', up);
  });
  el.addEventListener('dblclick', () => setPos(0.75)); /* 0 dB unity (MIX-03) */
}

function syncKnob(el, v, min, max) {
  const p = (v - min) / (max - min);
  el.querySelector('.knob-pointer').style.transform =
    `translate(-50%, -100%) rotate(${-135 + p * 270}deg)`;
}
function attachKnob(el, getVal, setVal, min, max, def) {
  el.addEventListener('pointerdown', (e) => {
    e.preventDefault();
    el.setPointerCapture(e.pointerId);
    const startV = getVal();
    const startY = e.clientY;
    const move = (ev) => {
      const dv = (startY - ev.clientY) / 120;
      setVal(Math.min(max, Math.max(min, startV + dv * (max - min))));
    };
    const up = (ev) => {
      el.releasePointerCapture(ev.pointerId);
      el.removeEventListener('pointermove', move);
      el.removeEventListener('pointerup', up);
    };
    el.addEventListener('pointermove', move);
    el.addEventListener('pointerup', up);
  });
  el.addEventListener('dblclick', () => setVal(def));
}

/* ─── canvas / timeline ─────────────────────────────────────────── */
const canvas = $('#timeline');
const ctx2d = canvas.getContext('2d');

function sizeCanvas() {
  const wrap = $('#timeline-wrap');
  const n = S7.tracks.length;
  const w = Math.max(400, wrap.clientWidth);
  const h = RULER_H + n * LANE_H + 34;
  const dpr = window.devicePixelRatio || 1;
  canvas.width = Math.round(w * dpr);
  canvas.height = Math.round(h * dpr);
  canvas.style.width = w + 'px';
  canvas.style.height = h + 'px';
  ctx2d.setTransform(dpr, 0, 0, dpr, 0, 0);
}

function xForSample(smp) {
  if (!S7.timeModel) return smp / 48; /* ~48000 smp/s before the engine boots */
  const tick = S7.timeModel.sampleToTick(smp);
  return -ui.scrollX + (tick / (960 * 4)) * ui.pxPerBar;
}
function sampleAtX(x) {
  if (!S7.timeModel) return 0;
  const tick = ((x + ui.scrollX) / ui.pxPerBar) * 960 * 4;
  return Math.max(0, S7.timeModel.tickToSample(tick));
}

function drawTimeline() {
  const w = canvas.clientWidth;
  const h = canvas.clientHeight;
  if (!S7.timeModel) return;
  const tm = S7.timeModel;
  const sr = S7.sr;

  ctx2d.clearRect(0, 0, w, h);
  /* background */
  ctx2d.fillStyle = '#202226';
  ctx2d.fillRect(0, 0, w, h);

  const loopInX = S7.loop.on ? xForSample(S7.loop.in) : null;
  const loopOutX = S7.loop.on ? xForSample(S7.loop.out) : null;

  /* loop region (full height) */
  if (loopInX !== null && loopOutX > loopInX) {
    ctx2d.fillStyle = 'rgba(255,140,26,0.06)';
    ctx2d.fillRect(Math.max(0, loopInX), 0, Math.min(w, loopOutX) - Math.max(0, loopInX), h);
    ctx2d.strokeStyle = 'rgba(255,140,26,0.55)';
    ctx2d.lineWidth = 1;
    ctx2d.beginPath();
    if (loopInX >= 0 && loopInX <= w) { ctx2d.moveTo(loopInX + 0.5, 0); ctx2d.lineTo(loopInX + 0.5, h); }
    if (loopOutX >= 0 && loopOutX <= w) { ctx2d.moveTo(loopOutX + 0.5, 0); ctx2d.lineTo(loopOutX + 0.5, h); }
    ctx2d.stroke();
  }

  /* lanes */
  const n = S7.tracks.length;
  for (let i = 0; i < n; i++) {
    const y0 = RULER_H + i * LANE_H;
    const t = S7.tracks[i];
    ctx2d.fillStyle = i % 2 === 0 ? '#222429' : '#202226';
    ctx2d.fillRect(0, y0, w, LANE_H);
    ctx2d.strokeStyle = 'rgba(0,0,0,0.5)';
    ctx2d.beginPath(); ctx2d.moveTo(0, y0 + 0.5); ctx2d.lineTo(w, y0 + 0.5); ctx2d.stroke();

    /* selection wash */
    if (t.id === ui.selectedTrackId) {
      ctx2d.fillStyle = 'rgba(49,67,94,0.25)';
      ctx2d.fillRect(0, y0, w, LANE_H);
    }

    /* recording punch region */
    if (S7.recording && t.armed) {
      const rx1 = xForSample(S7.recStartSample);
      const rx2 = xForSample(S7.posSamples);
      ctx2d.fillStyle = 'rgba(255,75,62,0.10)';
      ctx2d.fillRect(Math.max(0, Math.min(rx1, rx2)), y0, Math.abs(rx2 - rx1), LANE_H);
    }

    /* clips */
    for (const c of t.clips) {
      const x1 = xForSample(c.start);
      const x2 = xForSample(c.start + c.buffer.length);
      if (x2 < 0 || x1 > w) continue;
      const cx = Math.max(0, x1), cw = Math.min(w, x2) - cx;
      const cy = y0 + 6, chh = LANE_H - 12;
      /* body */
      ctx2d.fillStyle = t.color + '55';
      ctx2d.strokeStyle = t.color + 'aa';
      roundRect(ctx2d, cx, cy, cw, chh, 3);
      ctx2d.fill(); ctx2d.stroke();
      /* waveform */
      if (cw > 6 && c.peaks) {
        ctx2d.strokeStyle = 'rgba(255,255,255,0.55)';
        ctx2d.lineWidth = 1;
        ctx2d.beginPath();
        const nP = c.peaks.length / 2;
        for (let px = 0; px < cw; px += 2) {
          const idx = Math.min(nP - 1, Math.floor((px / cw) * nP));
          const mn = c.peaks[idx * 2], mx = c.peaks[idx * 2 + 1];
          const yMin = cy + chh / 2 - mn * (chh / 2 - 2);
          const yMax = cy + chh / 2 - mx * (chh / 2 - 2);
          const X = cx + px;
          ctx2d.moveTo(X + 0.5, Math.max(cy + 1, Math.min(yMin, yMax)));
          ctx2d.lineTo(X + 0.5, Math.min(cy + chh - 1, Math.max(yMin, yMax)));
        }
        ctx2d.stroke();
      }
      /* label */
      ctx2d.fillStyle = 'rgba(255,255,255,0.75)';
      ctx2d.font = '10px ' + 'monospace';
      ctx2d.fillText(c.name, cx + 5, cy + 11);
      /* fade ticks */
      ctx2d.strokeStyle = 'rgba(255,255,255,0.4)';
      ctx2d.beginPath();
      ctx2d.moveTo(cx + 0.5, cy + 2); ctx2d.lineTo(cx + 0.5, cy + chh - 2);
      ctx2d.moveTo(cx + cw - 0.5, cy + 2); ctx2d.lineTo(cx + cw - 0.5, cy + chh - 2);
      ctx2d.stroke();
    }

    /* armed indicator */
    if (t.armed) {
      ctx2d.fillStyle = S7.recording ? '#ff4b3e' : 'rgba(255,75,62,0.75)';
      ctx2d.beginPath();
      ctx2d.arc(8, y0 + 10, 3.5, 0, Math.PI * 2);
      ctx2d.fill();
      if (S7.recording) {
        ctx2d.font = 'bold 9px monospace';
        ctx2d.fillText('REC', 15, y0 + 13);
      }
    }
    if (t.hasInput) {
      ctx2d.fillStyle = '#3ddc84';
      ctx2d.beginPath(); ctx2d.arc(20, y0 + 10, 2.5, 0, Math.PI * 2); ctx2d.fill();
    }
  }

  /* ruler */
  ctx2d.fillStyle = '#1b1d20';
  ctx2d.fillRect(0, 0, w, RULER_H);
  ctx2d.strokeStyle = 'rgba(255,255,255,0.14)';
  ctx2d.lineWidth = 1;
  const beatPx = ui.pxPerBar / 4;
  const firstTick = Math.floor(ui.scrollX / beatPx) * 960;
  for (let tick = firstTick; tick / (960 * 4) * ui.pxPerBar - ui.scrollX < w + beatPx; tick += 960) {
    const x = -ui.scrollX + (tick / (960 * 4)) * ui.pxPerBar;
    const isBar = tick % (960 * 4) === 0;
    ctx2d.strokeStyle = isBar ? 'rgba(255,255,255,0.30)' : 'rgba(255,255,255,0.12)';
    ctx2d.beginPath();
    ctx2d.moveTo(x + 0.5, isBar ? 8 : 16);
    ctx2d.lineTo(x + 0.5, RULER_H);
    ctx2d.stroke();
    if (isBar && x > -40 && x < w) {
      ctx2d.fillStyle = 'rgba(255,255,255,0.65)';
      ctx2d.font = '10px monospace';
      ctx2d.fillText(String(tick / (960 * 4) + 1), x + 4, 11);
    }
  }
  /* loop handles */
  if (loopInX !== null) {
    drawLoopHandle(loopInX, loopOutX);
  }
  ctx2d.strokeStyle = 'rgba(0,0,0,0.8)';
  ctx2d.beginPath(); ctx2d.moveTo(0, RULER_H + 0.5); ctx2d.lineTo(w, RULER_H + 0.5); ctx2d.stroke();

  /* playhead */
  const px = xForSample(S7.posSamples);
  if (px >= -2 && px <= w + 2) {
    ctx2d.strokeStyle = '#ff5c33';
    ctx2d.lineWidth = 1;
    ctx2d.beginPath(); ctx2d.moveTo(px + 0.5, 0); ctx2d.lineTo(px + 0.5, h); ctx2d.stroke();
    ctx2d.fillStyle = '#ff5c33';
    ctx2d.beginPath();
    ctx2d.moveTo(px - 5, 0); ctx2d.lineTo(px + 6, 0); ctx2d.lineTo(px + 0.5, 8);
    ctx2d.closePath(); ctx2d.fill();
  }
}
function drawLoopHandle(xIn, xOut) {
  for (const x of [xIn, xOut]) {
    if (x < -20 || x > canvas.clientWidth + 20) continue;
    ctx2d.fillStyle = '#ff8c1a';
    ctx2d.fillRect(x - 3, 0, 6, RULER_H);
    ctx2d.fillStyle = '#1b1d20';
    ctx2d.font = 'bold 8px monospace';
    ctx2d.fillText(x === xIn ? 'IN' : 'OUT', x - 5, 12);
  }
}
function roundRect(c, x, y, w, h, r) {
  c.beginPath();
  c.moveTo(x + r, y);
  c.arcTo(x + w, y, x + w, y + h, r);
  c.arcTo(x + w, y + h, x, y + h, r);
  c.arcTo(x, y + h, x, y, r);
  c.arcTo(x, y, x + w, y, r);
  c.closePath();
}

/* ─── timeline interactions ─────────────────────────────────────── */
canvas.addEventListener('pointerdown', (e) => {
  const r = canvas.getBoundingClientRect();
  const x = e.clientX - r.left, y = e.clientY - r.top;
  canvas.setPointerCapture(e.pointerId);
  if (y < RULER_H) {
    /* loop handle grab? */
    if (S7.loop.on) {
      const inX = xForSample(S7.loop.in), outX = xForSample(S7.loop.out);
      if (Math.abs(x - inX) < 8) { ui.dragLoop = 'in'; return; }
      if (Math.abs(x - outX) < 8) { ui.dragLoop = 'out'; return; }
    }
    ui.scrubbing = true;
    S7.seek(sampleAtX(x));
  } else {
    const i = Math.floor((y - RULER_H) / LANE_H);
    const t = S7.tracks[i];
    if (t) selectTrack(t.id);
  }
});
canvas.addEventListener('pointermove', (e) => {
  const r = canvas.getBoundingClientRect();
  const x = e.clientX - r.left;
  if (ui.scrubbing) S7.seek(sampleAtX(x));
  else if (ui.dragLoop === 'in') {
    let s = sampleAtX(x);
    if (s >= S7.loop.out) s = S7.loop.out - S7.sr;
    S7.loop.in = Math.max(0, s);
    S7.notify();
  } else if (ui.dragLoop === 'out') {
    let s = sampleAtX(x);
    if (s <= S7.loop.in) s = S7.loop.in + S7.sr;
    S7.loop.out = s;
    S7.notify();
  } else {
    /* cursor hints */
    const inX = S7.loop.on ? xForSample(S7.loop.in) : -1;
    const outX = S7.loop.on ? xForSample(S7.loop.out) : -1;
    const y = e.clientY - r.top;
    if (y < RULER_H && S7.loop.on && (Math.abs(x - inX) < 8 || Math.abs(x - outX) < 8))
      canvas.style.cursor = 'ew-resize';
    else canvas.style.cursor = y < RULER_H ? 'text' : 'default';
  }
});
canvas.addEventListener('pointerup', (e) => {
  ui.scrubbing = false;
  ui.dragLoop = null;
  try { canvas.releasePointerCapture(e.pointerId); } catch (err) {}
});
canvas.addEventListener('wheel', (e) => {
  e.preventDefault();
  if (e.ctrlKey || e.metaKey) {
    const r = canvas.getBoundingClientRect();
    const x = e.clientX - r.left;
    const anchorSample = sampleAtX(x);
    const f = e.deltaY < 0 ? 1.2 : 1 / 1.2;
    ui.pxPerBar = Math.min(1600, Math.max(24, ui.pxPerBar * f));
    /* keep anchor under cursor */
    const newTick = S7.timeModel.sampleToTick(anchorSample);
    ui.scrollX = (newTick / (960 * 4)) * ui.pxPerBar - x;
    ui.scrollX = Math.max(-100, ui.scrollX);
  } else if (e.shiftKey || Math.abs(e.deltaX) > Math.abs(e.deltaY)) {
    ui.scrollX = Math.max(-100, ui.scrollX + (e.deltaX || e.deltaY));
  } else {
    $('#track-scroll').scrollTop += e.deltaY;
  }
  updateStatus();
}, { passive: false });

/* ─── LCD / status ──────────────────────────────────────────────── */
function posString(pos) {
  const tm = S7.timeModel;
  const tick = tm.sampleToTick(pos);
  const bar = Math.floor(tick / 3840) + 1;
  const beat = Math.floor((tick % 3840) / 960) + 1;
  const frame = Math.floor(((tick % 960) / 960) * 100);
  return String(bar).padStart(3, '0') + '  ' + beat + '  ' + String(frame).padStart(2, '0');
}
function updateLCD() {
  $('#lcd-pos').textContent = S7.timeModel ? posString(S7.posSamples) : '---  -  --';
  $('#lcd-smp').textContent = Math.round(S7.posSamples) + ' smp';
  $('#lcd-bpm').textContent = S7.bpm.toFixed(3);
  $('#lcd-dev').textContent = (S7.sr / 1000).toFixed(1) + ' kHz · ' + S7.bufferSize + ' smp';
}
function updateStatus() {
  $('#st-zoom').textContent = 'Zoom: 1 bar = ' + Math.round(ui.pxPerBar) + ' px';
  const rt = S7.tracks.some((t) => t.armed || (t.hasInput && t.monitorOn));
  $('#st-lane').textContent = 'Lane: ' + (rt ? 'RT' : 'MAE') + (S7.playing ? ' (playing)' : '');
  if (S7.ctx) {
    const lat = S7.latencyMs();
    $('#hw-summary').textContent = S7.sr + ' Hz · ' + S7.bufferSize + ' smp buf · out lat ' + lat.toFixed(1) + ' ms · ' + (S7.inputState === 'ready' ? 'mic ✓' : 'mic ' + S7.inputState);
  }
  const pdc = $('#st-pdc');
  pdc.textContent = 'PDC: ON · cap 32768 · worst 0 smp ●';
  pdc.className = 'st-ok';
  const ins = $('#input-status');
  ins.className = 'input-status ' + (S7.inputState === 'ready' ? 'ok' : S7.inputState === 'denied' ? 'err' : '');
  $('#input-state').textContent = S7.inputState === 'ready' ? 'Mic 1–2 (ready)' :
    S7.inputState === 'pending' ? 'requesting…' :
    S7.inputState === 'denied' ? 'DENIED' : 'idle';
}

/* ─── meters ────────────────────────────────────────────────────── */
function ensureMeterState(id, analyser) {
  let st = ui.meterState.get(id);
  if (!st) {
    st = { disp: 0, hold: 0, holdT: 0, arr: new Float32Array(analyser.fftSize) };
    ui.meterState.set(id, st);
  }
  st.analyser = analyser;
  return st;
}
function readPeak(analyser, arr) {
  analyser.getFloatTimeDomainData(arr);
  let p = 0;
  for (let i = 0; i < arr.length; i++) { const v = Math.abs(arr[i]); if (v > p) p = v; }
  return p;
}
function updateMeters() {
  const now = performance.now();
  const decay = 0.88;
  const update = (id, analyser, el) => {
    if (!el) return;
    const st = ensureMeterState(id, analyser);
    const p = analyser ? readPeak(analyser, st.arr) : 0;
    const db = p > 0.00003 ? 20 * Math.log10(p) : -120;
    st.disp = Math.max(K.dbToPct(db), st.disp * decay);
    if (p > st.hold) { st.hold = p; st.holdT = now; }
    const holdActive = now - st.holdT < 2000 && st.hold > 0;
    el._meterFill.style.height = (st.disp * 100).toFixed(1) + '%';
    el._meterPeak.style.bottom = (holdActive ? K.dbToPct(20 * Math.log10(Math.max(st.hold, 1e-7))) * 100 : 0).toFixed(1) + '%';
    el._meterPeak.style.opacity = holdActive ? 0.95 : 0;
    el._db.textContent = p > 0.00003 ? db.toFixed(1) + ' dB' : '−∞ dB';
  };
  for (const t of S7.tracks) {
    const el = document.querySelector(`.strip[data-track="${t.id}"]`);
    update(t.id, t.nodes ? t.nodes.analyser : null, el);
  }
  update('master', S7.masterNodes ? S7.masterNodes.analyser : null, document.querySelector('.strip.master'));
}

/* ─── transport / LCD actions ───────────────────────────────────── */
function syncTransport() {
  $('#btn-play').classList.toggle('on', S7.playing);
  $('#btn-rec').classList.toggle('on', S7.recording || S7.recordPending);
  $('#btn-loop').classList.toggle('on', S7.loop.on);
  $('#btn-count').classList.toggle('on', S7.countIn);
  $('#btn-stop').classList.toggle('on', false);
  /* toolbar edit-mode buttons keep their own state */
}

async function togglePlay() {
  if (S7.playing) await S7.stop();
  else S7.startPlay({});
}
async function toggleRecord() {
  if (S7.recording) {
    /* PT behaviour: stop recording, keep the take */
    S7.recording = false;
    await S7.finalizeRecording();
    S7.notify();
    toast('Take saved to timeline');
    return;
  }
  const armed = S7.tracks.filter((t) => t.armed);
  if (armed.length === 0) {
    toast('Arm a track first — press R on a track header (mic input is requested)');
    return;
  }
  if (S7.playing) {
    S7.recordPending = false;
    S7.recording = true;
    S7.recStartSample = S7.currentAbs();
    for (const t of armed) S7.recBuf.set(t.id, { chunks: [] });
    S7.onRecordingUI && S7.onRecordingUI(true);
    S7.notify();
  } else {
    await S7.ensureRunning();
    S7.startPlay({ record: true });
  }
}

/* ─── menus ─────────────────────────────────────────────────────── */
function initMenus() {
  const layer = $('#menu-layer');
  document.querySelectorAll('.menu-item').forEach((item) => {
    item.addEventListener('click', (e) => {
      e.stopPropagation();
      const name = item.dataset.menu;
      if (ui.menuOpen === name) { closeMenus(); return; }
      closeMenus();
      ui.menuOpen = name;
      item.classList.add('open');
      const menu = layer.querySelector(`.menu[data-menu="${name}"]`);
      layer.hidden = false;
      const r = item.getBoundingClientRect();
      menu.style.left = r.left + 'px';
      menu.style.top = r.bottom + 'px';
    });
  });
  document.addEventListener('click', closeMenus);
  layer.addEventListener('click', (e) => {
    const btn = e.target.closest('button[data-act]');
    if (!btn) return;
    const act = btn.dataset.act;
    closeMenus();
    doAction(act);
  });
  /* context menu */
  document.addEventListener('click', (e) => {
    if (!e.target.closest('#ctxmenu')) $('#ctxmenu').hidden = true;
  });
  $('#ctxmenu').addEventListener('click', (e) => {
    const btn = e.target.closest('button[data-act]');
    if (!btn) return;
    const act = btn.dataset.act;
    $('#ctxmenu').hidden = true;
    doAction(act, ui.ctxTarget);
  });
  $('#about-close').addEventListener('click', () => { $('#about-dlg').hidden = true; });
}
function closeMenus() {
  ui.menuOpen = null;
  $('#menu-layer').hidden = true;
  document.querySelectorAll('.menu-item.open').forEach((m) => m.classList.remove('open'));
}
function showCtxMenu(x, y, track) {
  ui.ctxTarget = track;
  const m = $('#ctxmenu');
  m.hidden = false;
  m.style.left = Math.min(x, window.innerWidth - 180) + 'px';
  m.style.top = Math.min(y, window.innerHeight - 140) + 'px';
}

async function doAction(act, track) {
  switch (act) {
    case 'new':
      if (confirm('Start a new session? All clips will be cleared.')) {
        for (const t of [...S7.tracks]) S7.removeTrack(t);
        S7.addTrack('Vocal'); S7.addTrack('Guitar'); S7.addTrack('Keys');
        S7.loop = { on: false, in: 0, out: 0 };
        S7.posSamples = 0;
        selectTrack(S7.tracks[0].id);
        toast('New session');
      }
      break;
    case 'demo':
      await S7.ensureRunning();
      S7.loadDemo();
      toast('Demo loop loaded — press Space');
      break;
    case 'bounce-loop':
      if (!S7.loop.on) { toast('Loop is off — use Bounce All'); return; }
      await doBounce(S7.loop.in, S7.loop.out - S7.loop.in);
      break;
    case 'bounce-all': {
      let end = 0;
      for (const t of S7.tracks) for (const c of t.clips) end = Math.max(end, c.start + c.buffer.length);
      if (end === 0) { toast('Nothing to bounce yet'); return; }
      await doBounce(0, end);
      break;
    }
    case 'add-track': selectTrack(S7.addTrack().id); break;
    case 'del-track': {
      const t = track || selectedTrack();
      if (t) { S7.removeTrack(t); selectTrack(null); }
      break;
    }
    case 'clear-clips': {
      const t = track || selectedTrack();
      if (t) { t.clips = []; S7.notify(); toast('Clips cleared'); }
      break;
    }
    case 'rename': {
      const t = track;
      if (t) {
        const el = document.querySelector(`.track-head[data-track="${t.id}"]`);
        if (el) el.querySelector('.th-label').dispatchEvent(new Event('dblclick'));
      }
      break;
    }
    case 'arm-all': for (const t of S7.tracks) { await S7.ensureRunning(); S7.setArmed(t, true); if (!t.hasInput) await S7.enableInput(t); } break;
    case 'disarm-all': for (const t of S7.tracks) S7.setArmed(t, false); break;
    case 'zoom-in': zoomBy(1.25); break;
    case 'zoom-out': zoomBy(0.8); break;
    case 'zoom-fit': {
      if (!S7.timeModel) return;
      let end = 0;
      for (const t of S7.tracks) for (const c of t.clips) end = Math.max(end, c.start + c.buffer.length);
      if (end > 0) {
        const beats = S7.timeModel.sampleToTick(end) / 960;
        ui.pxPerBar = Math.min(1600, Math.max(24, (canvas.clientWidth - 40) / (beats / 4)));
        ui.scrollX = 0;
        updateStatus();
      }
      break;
    }
    case 'buf-128': case 'buf-256': case 'buf-512': case 'buf-1024':
      await S7.setBufferSize(Number(act.slice(4)));
      toast('Buffer size → ' + S7.bufferSize + ' samples');
      break;
    case 'about': $('#about-dlg').hidden = false; break;
  }
}
function zoomBy(f) {
  ui.pxPerBar = Math.min(1600, Math.max(24, ui.pxPerBar * f));
  updateStatus();
}

/* ─── bounce ───────────────────────────────────────────────────── */
async function doBounce(start, len) {
  await S7.ensureRunning();
  toast('Bouncing… offline render');
  await new Promise((r) => setTimeout(r, 60));
  try {
    const rendered = await S7.bounce(start, len);
    const blob = encodeWav(rendered, 16);
    downloadBlob(blob, 'seven7-bounce-' + new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19) + '.wav');
    toast('Bounce complete ✓ (' + (len / S7.sr).toFixed(1) + ' s)');
  } catch (e) {
    toast('Bounce failed: ' + (e && e.message ? e.message : e));
  }
}

/* ─── transport buttons ─────────────────────────────────────────── */
function initTransport() {
  $('#btn-play').addEventListener('click', async () => { await S7.ensureRunning(); togglePlay(); });
  $('#btn-stop').addEventListener('click', () => S7.stop());
  $('#btn-rec').addEventListener('click', async () => { await S7.ensureRunning(); toggleRecord(); });
  $('#btn-loop').addEventListener('click', () => {
    S7.loop.on = !S7.loop.on;
    if (S7.loop.on && S7.loop.out <= S7.loop.in) {
      S7.loop.in = S7.posSamples;
      S7.loop.out = S7.posSamples + S7.barSamples();
    }
    S7.notify();
  });
  $('#btn-count').addEventListener('click', () => { S7.countIn = !S7.countIn; S7.notify(); });
  $('#btn-demo').addEventListener('click', async () => {
    await S7.ensureRunning();
    S7.loadDemo();
    toast('Demo loop loaded — press Space');
  });
  document.querySelectorAll('.tb-btn.mode').forEach((b) => b.addEventListener('click', () => {
    document.querySelectorAll('.tb-btn.mode').forEach((x) => x.classList.remove('on'));
    b.classList.add('on');
  }));
}

/* ─── keyboard ──────────────────────────────────────────────────── */
function initKeys() {
  window.addEventListener('keydown', async (e) => {
    if (e.target.closest('input, select, textarea, [contenteditable]')) return;
    switch (e.key) {
      case ' ':
        e.preventDefault();
        await S7.ensureRunning();
        togglePlay();
        break;
      case 'r': case 'R':
        await S7.ensureRunning();
        toggleRecord();
        break;
      case 'l': case 'L':
        S7.loop.on = !S7.loop.on;
        if (S7.loop.on && S7.loop.out <= S7.loop.in) {
          S7.loop.in = S7.posSamples;
          S7.loop.out = S7.posSamples + S7.barSamples();
        }
        S7.notify();
        break;
      case ',':
        S7.loop.in = S7.posSamples; S7.notify(); break;
      case '.':
        S7.loop.out = S7.posSamples; S7.notify(); break;
      case 'Home':
        await S7.stop(); break;
    }
  });
}

/* ─── main loop ─────────────────────────────────────────────────── */
function frame() {
  if (S7.ctx) {
    drawTimeline();
    updateLCD();
    updateMeters();
    if (S7.playing) updateStatus();
  }
  requestAnimationFrame(frame);
}

/* ─── resize / divider ──────────────────────────────────────────── */
function initResize() {
  window.addEventListener('resize', () => { sizeCanvas(); });
  const div = $('#divider');
  div.addEventListener('pointerdown', (e) => {
    e.preventDefault();
    div.setPointerCapture(e.pointerId);
    const move = (ev) => {
      const ws = $('#workspace').getBoundingClientRect();
      const mixerH = ws.bottom - ev.clientY;
      const m = $('#mixer');
      m.style.height = Math.max(140, Math.min(ws.height - 120, mixerH)) + 'px';
      m.style.flex = '0 0 ' + m.style.height;
      sizeCanvas();
    };
    const up = (ev) => {
      div.releasePointerCapture(ev.pointerId);
      div.removeEventListener('pointermove', move);
      div.removeEventListener('pointerup', up);
    };
    div.addEventListener('pointermove', move);
    div.addEventListener('pointerup', up);
  });
}

/* ─── BPM edit ──────────────────────────────────────────────────── */
function initBpm() {
  $('#lcd-bpm').addEventListener('click', () => {
    const el = $('#lcd-bpm');
    const input = document.createElement('input');
    input.value = S7.bpm;
    input.style.cssText = 'width:70px;background:#101113;color:#ff8c1a;border:1px solid #ff8c1a;border-radius:2px;font:13px monospace;text-align:center;outline:none;padding:2px;';
    el.replaceWith(input);
    input.focus(); input.select();
    const commit = () => {
      const v = parseFloat(input.value);
      if (isFinite(v)) S7.setBpm(v);
      S7.notify();
      buildBpmLcd();
    };
    input.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') input.blur();
      if (e.key === 'Escape') { input.value = S7.bpm; input.blur(); }
    });
    input.addEventListener('blur', commit);
  });
}
function buildBpmLcd() {
  let el = $('#lcd-bpm');
  if (!el) {
    el = document.createElement('div');
    el.className = 'lcd small bpm';
    el.id = 'lcd-bpm';
    el.title = 'Tempo — click to change';
    el.insertAdjacentElement('afterend', document.querySelector('.lcd-cap'));
    $('#controlbar').insertBefore(el, document.querySelector('.lcd-cap'));
    initBpm();
  }
  el.textContent = S7.bpm.toFixed(3);
}

/* ─── boot ─────────────────────────────────────────────────────── */
function notifyUI() {
  syncHeaders();
  syncStrips();
  syncTransport();
  updateLCD();
  updateStatus();
}
S7.onChange = () => {
  /* structure changes are detected by count/id diff */
  const known = document.querySelectorAll('.track-head').length;
  if (known !== S7.tracks.length) { buildHeaders(); buildStrips(); }
  notifyUI();
};
S7.onRecordingUI = () => notifyUI();

async function boot() {
  buildHeaders();
  buildStrips();
  initMenus();
  initTransport();
  initKeys();
  initResize();
  initBpm();
  notifyUI();
  frame();
  /* default session */
  S7.addTrack('Vocal');
  S7.addTrack('Guitar');
  S7.addTrack('Keys');
  selectTrack(S7.tracks[0].id);
  /* first gesture starts the audio engine */
  const starter = async () => {
    await S7.ensureRunning();
    if (S7.running) toast('Audio engine running — ' + S7.sr + ' Hz. Arm a track (R) to record, or load the demo loop.');
    window.removeEventListener('pointerdown', starter);
    window.removeEventListener('keydown', starter);
  };
  window.addEventListener('pointerdown', starter, { once: true });
  window.addEventListener('keydown', starter, { once: true });
}
boot();
