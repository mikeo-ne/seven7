// Spot mode dialog (UIW-07 / EDT): type an exact position for a region in samples,
// bars·beats, min:sec or timecode.
import { useState } from 'react'
import { commands } from '../lib/commands'
import { formatBarsBeats, formatMinSec, formatSamples, formatTimecode, samplesPerBeat } from '../lib/units'
import { useStore } from '../state/store'

export function SpotDialog() {
  const id = useStore((s) => s.spotRegion)
  const setSpot = useStore((s) => s.setSpotRegion)
  const project = useStore((s) => s.project)
  const region = project?.regions.find((r) => r.id === id)
  const [unit, setUnit] = useState<'samples' | 'bars' | 'minsec' | 'timecode'>('samples')
  const [text, setText] = useState('')
  const [anchor, setAnchor] = useState<'start' | 'end'>('start')
  if (!project || !region) return null
  const sr = project.sample_rate

  const parse = (): number | null => {
    const t = text.trim()
    if (!t) return null
    if (unit === 'samples') return Number.isFinite(Number(t)) ? Math.round(Number(t)) : null
    if (unit === 'bars') {
      const m = t.match(/^(\d+)(?:[\s.|]+(\d+))?(?:[\s.|]+(\d+))?$/)
      if (!m) return null
      const beat = samplesPerBeat(sr, project.tempo) * (4 / project.sig.den)
      const bars = Number(m[1]) - 1
      const beats = (Number(m[2] ?? 1) - 1)
      const ticks = Number(m[3] ?? 0)
      return Math.round(bars * project.sig.num * beat + beats * beat + (ticks / 960) * beat)
    }
    if (unit === 'minsec') {
      const m = t.match(/^(?:(\d+):)?(\d+)(?:\.(\d{1,3}))?$/)
      if (!m) return null
      const ms = Number((m[3] ?? '0').padEnd(3, '0'))
      return Math.round((Number(m[1] ?? 0) * 60 + Number(m[2])) * sr + (ms / 1000) * sr)
    }
    const m = t.match(/^(\d+):(\d+):(\d+)(?::(\d+))?$/)
    if (!m) return null
    return Math.round((Number(m[1]) * 3600 + Number(m[2]) * 60 + Number(m[3])) * sr + (Number(m[4] ?? 0) / 30) * sr)
  }
  const value = parse()
  const apply = () => {
    if (value === null) return
    const start = anchor === 'start' ? value : value - region.length
    void commands().send({ op: 'move', moves: [{ region: region.id, start: Math.max(0, start) }] })
    setSpot(null)
  }
  const current = anchor === 'start' ? region.start : region.start + region.length
  return (
    <div className="modal-backdrop" onMouseDown={() => setSpot(null)}>
      <div className="modal" onMouseDown={(e) => e.stopPropagation()}>
        <h3>Spot “{region.name}”</h3>
        <div className="kv">
          <span className="k">Anchor</span>
          <div className="segmented small">
            <button className={anchor === 'start' ? 'on' : ''} onClick={() => setAnchor('start')}>
              Start
            </button>
            <button className={anchor === 'end' ? 'on' : ''} onClick={() => setAnchor('end')}>
              End
            </button>
          </div>
          <span className="k">Current</span>
          <span className="v mono">
            {formatSamples(current)} · {formatBarsBeats(current, sr, project.tempo, project.sig).trim()} · {formatMinSec(current, sr)} · {formatTimecode(current, sr)}
          </span>
          <span className="k">Units</span>
          <select className="select" value={unit} onChange={(e) => setUnit(e.target.value as typeof unit)}>
            <option value="samples">Samples</option>
            <option value="bars">Bars · Beats · Ticks</option>
            <option value="minsec">Min:Sec.ms</option>
            <option value="timecode">Timecode h:m:s:f</option>
          </select>
          <span className="k">Position</span>
          <input
            className="numfield"
            style={{ width: '100%', textAlign: 'left' }}
            autoFocus
            value={text}
            placeholder={unit === 'samples' ? '96000' : unit === 'bars' ? '5 1 000' : unit === 'minsec' ? '0:02.000' : '0:00:02:00'}
            onChange={(e) => setText(e.target.value)}
            onKeyDown={(e) => {
              e.stopPropagation()
              if (e.key === 'Enter') apply()
              if (e.key === 'Escape') setSpot(null)
            }}
          />
          <span className="k">= samples</span>
          <span className="v mono">{value === null ? '—' : formatSamples(value)}</span>
        </div>
        <div className="actions">
          <button className="btn" onClick={() => setSpot(null)}>
            Cancel
          </button>
          <button className="btn on" disabled={value === null} onClick={apply}>
            Spot
          </button>
        </div>
      </div>
    </div>
  )
}
