// seven7 UI — units and readouts. Pure functions, all integer-sample based.

export interface Tempo {
  num: number
  den: number
}

/** Samples per beat (quarter note) as a rational-derived float; display only. */
export function samplesPerBeat(sr: number, tempo: Tempo): number {
  // bpm = num/den  →  seconds per beat = 60 * den / num
  return (sr * 60 * tempo.den) / tempo.num
}

export function samplesPerBar(sr: number, tempo: Tempo, sig: { num: number; den: number }): number {
  return samplesPerBeat(sr, tempo) * sig.num * (4 / sig.den)
}

/** "101 1 1 000" style bars|beats|div|ticks (Pro Tools counter) — Logic shows 1-based bars too. */
export function formatBarsBeats(sample: number, sr: number, tempo: Tempo, sig: { num: number; den: number }, ticksPerBeat = 960): string {
  const spb = samplesPerBeat(sr, tempo) * (4 / sig.den)
  const totalBeats = sample / spb
  const bar = Math.floor(totalBeats / sig.num)
  const beat = Math.floor(totalBeats - bar * sig.num)
  const tick = Math.floor((totalBeats - bar * sig.num - beat) * ticksPerBeat)
  return `${String(bar + 1).padStart(3, ' ')} ${beat + 1} ${String(tick).padStart(3, '0')}`
}

/** "h:mm:ss:ff" SMPTE-style with integer frame math (no float drift at frame edges). */
export function formatTimecode(sample: number, sr: number, fps = 30): string {
  const s = Math.max(0, Math.floor(sample))
  const whole = Math.floor(s / sr)
  const rem = s - whole * sr
  const frame = Math.floor((rem * fps) / sr)
  const h = Math.floor(whole / 3600)
  const m = Math.floor((whole % 3600) / 60)
  const sec = whole % 60
  return `${h}:${String(m).padStart(2, '0')}:${String(sec).padStart(2, '0')}:${String(frame).padStart(2, '0')}`
}

/** "m:ss.mmm" minutes:seconds.milliseconds */
export function formatMinSec(sample: number, sr: number): string {
  const s = Math.max(0, Math.floor(sample))
  const whole = Math.floor(s / sr)
  const ms = Math.floor(((s - whole * sr) * 1000) / sr)
  const m = Math.floor(whole / 60)
  const sec = whole % 60
  return `${m}:${String(sec).padStart(2, '0')}.${String(ms).padStart(3, '0')}`
}

export function formatSamples(sample: number): string {
  return Math.round(sample).toLocaleString('en-US')
}

/** MIX-03 fader taper (mirrors engine/src/engine/mix_math.h::fader_db exactly). */
export function faderDb(position: number): number {
  const pos = Math.min(1, Math.max(0, position))
  if (pos < 0.005) return -Infinity
  let db: number
  if (pos >= 0.75) db = (12 * (pos - 0.75)) / 0.25
  else if (pos >= 0.15) db = (-60 * (0.75 - pos)) / 0.6
  else db = -60 - (84 * (0.15 - pos)) / 0.145
  return Math.round(db * 10) / 10
}

/** Inverse of faderDb (for typing a dB value). */
export function dbToFader(db: number): number {
  if (!Number.isFinite(db) || db <= -144) return 0
  if (db >= 0) return Math.min(1, 0.75 + (db / 12) * 0.25)
  if (db >= -60) return 0.75 - (-db / 60) * 0.6
  return Math.max(0.005, 0.15 - ((-db - 60) / 84) * 0.145)
}

export function formatDb(db: number, digits = 1): string {
  if (!Number.isFinite(db)) return '-∞'
  const s = db.toFixed(digits)
  return db > 0 ? `+${s}` : s
}

export function gainToDb(gain: number): number {
  return gain <= 1e-7 ? -Infinity : 20 * Math.log10(gain)
}

/** Peak meter position (0..1) with the −60…+6 dB scale used on strips. */
export function meterPos(gain: number, floorDb = -60, ceilDb = 6): number {
  const db = gainToDb(gain)
  if (!Number.isFinite(db)) return 0
  return Math.min(1, Math.max(0, (db - floorDb) / (ceilDb - floorDb)))
}

export function panLabel(pan: number): string {
  const v = Math.round(pan * 100)
  if (v === 0) return 'C'
  return v < 0 ? `L${-v}` : `R${v}`
}

export function clamp(v: number, lo: number, hi: number): number {
  return Math.min(hi, Math.max(lo, v))
}
