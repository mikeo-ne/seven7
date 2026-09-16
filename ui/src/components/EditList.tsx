// Precision mode — PT-style edit list: selection geometry in samples, editable.
import { commands } from '../lib/commands'
import { formatBarsBeats, formatMinSec, formatSamples, formatTimecode } from '../lib/units'
import { useStore } from '../state/store'

function Field({ value, onCommit }: { value: number; onCommit: (v: number) => void }) {
  return (
    <input
      className="numfield"
      key={value}
      defaultValue={value}
      onKeyDown={(e) => {
        e.stopPropagation()
        if (e.key === 'Enter') {
          const v = Number((e.target as HTMLInputElement).value)
          if (Number.isFinite(v) && v !== value) onCommit(Math.round(v))
          ;(e.target as HTMLInputElement).blur()
        }
      }}
      onBlur={(e) => {
        const v = Number(e.target.value)
        if (Number.isFinite(v) && v !== value) onCommit(Math.round(v))
      }}
    />
  )
}

export function EditList() {
  const project = useStore((s) => s.project)
  const status = useStore((s) => s.status)
  const selection = useStore((s) => s.selection)
  const select = useStore((s) => s.select)
  if (!project) return null
  const sr = project.sample_rate
  const c = commands()
  const region = project.regions.find((r) => r.id === selection.regions[selection.regions.length - 1])
  const range = selection.range
  const start = range ? range.start : region ? region.start : (status?.playhead ?? 0)
  const end = range ? range.end : region ? region.start + region.length : (status?.playhead ?? 0)
  const bb = (s: number) => formatBarsBeats(s, sr, project.tempo, project.sig).trim()

  const setStart = (v: number) => {
    if (region && !range) void c.send({ op: 'move', moves: [{ region: region.id, start: Math.max(0, v) }] })
    else select({ range: { start: Math.max(0, v), end: Math.max(v + 1, end), tracks: range?.tracks ?? [] } })
  }
  const setEnd = (v: number) => {
    if (region && !range) void c.send({ op: 'trim', region: region.id, end: Math.max(region.start + 1, v) })
    else select({ range: { start, end: Math.max(start + 1, v), tracks: range?.tracks ?? [] } })
  }

  return (
    <div className="panel right editlist">
      <div className="head">Edit Selection</div>
      <div className="body">
        <table>
          <tbody>
            <tr>
              <td className="k">Kind</td>
              <td>{range ? 'Time range' : region ? `${region.kind} region` : 'Playhead'}</td>
            </tr>
            <tr>
              <td className="k">Start</td>
              <td>
                <Field value={start} onCommit={setStart} />
              </td>
            </tr>
            <tr>
              <td className="k"></td>
              <td style={{ color: 'var(--ink-3)' }}>{bb(start)}</td>
            </tr>
            <tr>
              <td className="k"></td>
              <td style={{ color: 'var(--ink-3)' }}>{formatTimecode(start, sr)}</td>
            </tr>
            <tr>
              <td className="k">End</td>
              <td>
                <Field value={end} onCommit={setEnd} />
              </td>
            </tr>
            <tr>
              <td className="k"></td>
              <td style={{ color: 'var(--ink-3)' }}>{bb(end)}</td>
            </tr>
            <tr>
              <td className="k">Length</td>
              <td>{formatSamples(end - start)} smp</td>
            </tr>
            <tr>
              <td className="k"></td>
              <td style={{ color: 'var(--ink-3)' }}>{formatMinSec(end - start, sr)}</td>
            </tr>
            {region && (
              <>
                <tr>
                  <td className="k">Sync pt</td>
                  <td>{formatSamples(region.start)}</td>
                </tr>
                <tr>
                  <td className="k">Offset</td>
                  <td>{formatSamples(region.offset)}</td>
                </tr>
                <tr>
                  <td className="k">Gain</td>
                  <td>{region.gain_db.toFixed(1)} dB</td>
                </tr>
                <tr>
                  <td className="k">Fades</td>
                  <td>
                    {formatSamples(region.fade_in)} / {formatSamples(region.fade_out)}
                  </td>
                </tr>
              </>
            )}
            <tr>
              <td className="k">Pre/Post</td>
              <td>03:00 / 03:00</td>
            </tr>
          </tbody>
        </table>
        <div className="section">
          <div className="title">Regions</div>
          <table>
            <tbody>
              {project.regions
                .slice()
                .sort((a, b) => a.start - b.start || a.track - b.track)
                .map((r) => (
                  <tr key={r.id} className={selection.regions.includes(r.id) ? 'sel' : ''} onClick={(e) => select({ regions: [r.id], tracks: [r.track], range: null }, e.shiftKey)}>
                    <td>{r.name}</td>
                    <td style={{ textAlign: 'right' }}>{formatSamples(r.start)}</td>
                  </tr>
                ))}
            </tbody>
          </table>
        </div>
      </div>
    </div>
  )
}
