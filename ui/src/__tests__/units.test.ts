import { describe, expect, it } from 'vitest'
import { dbToFader, faderDb, formatBarsBeats, formatMinSec, formatTimecode, samplesPerBar } from '../lib/units'
import { snapLocal } from '../lib/commands'

const T = { num: 120, den: 1 }
const S = { num: 4, den: 4 }

describe('units', () => {
  it('bars·beats at 120 BPM / 48 kHz', () => {
    expect(samplesPerBar(48000, T, S)).toBe(96000)
    expect(formatBarsBeats(0, 48000, T, S)).toBe('  1 1 000')
    expect(formatBarsBeats(96000, 48000, T, S)).toBe('  2 1 000')
    expect(formatBarsBeats(24000 + 12000, 48000, T, S)).toBe('  1 2 480')
  })
  it('timecode & min:sec', () => {
    expect(formatTimecode(48000 * 61 + 1600, 48000)).toBe('0:01:01:01')
    expect(formatMinSec(48000 * 61 + 480, 48000)).toBe('1:01.010')
  })
  it('MIX-03 fader taper matches the engine', () => {
    expect(faderDb(1)).toBe(12)
    expect(faderDb(0.75)).toBe(0)
    expect(faderDb(0.15)).toBe(-60)
    expect(faderDb(0)).toBe(-Infinity)
    expect(faderDb(0.5)).toBeCloseTo(-25, 1)
    for (const db of [12, 6, 0, -6, -12, -30, -60, -100]) expect(faderDb(dbToFader(db))).toBeCloseTo(db, 0)
  })
  it('snap grid: samples has zero tolerance', () => {
    expect(snapLocal(12345.6, { unit: 'samples', division: 16, samples: 1 }, 48000, T, S)).toBe(12346)
    expect(snapLocal(12345, { unit: 'samples', division: 16, samples: 100 }, 48000, T, S)).toBe(12300)
    expect(snapLocal(25000, { unit: 'beat', division: 16, samples: 1 }, 48000, T, S)).toBe(24000)
    expect(snapLocal(100000, { unit: 'bar', division: 16, samples: 1 }, 48000, T, S)).toBe(96000)
    expect(snapLocal(6100, { unit: 'division', division: 16, samples: 1 }, 48000, T, S)).toBe(6000)
    expect(snapLocal(777, { unit: 'off', division: 16, samples: 1 }, 48000, T, S)).toBe(777)
  })
})
