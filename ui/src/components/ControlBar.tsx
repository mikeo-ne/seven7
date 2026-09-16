import { useState } from 'react'
import { useStore } from '../state/store'
import { useCommands } from '../lib/commands'
import { formatBarsBeats, formatMinSec, formatSamples, formatTimecode } from '../lib/units'

function Gauge({ k, value, warnAt = 0.6, hotAt = 0.85, text }: { k: string; value: number; warnAt?: number; hotAt?: number; text: string }) {
  const cls = value >= hotAt ? 'hot' : value >= warnAt ? 'warn' : ''
  return (
    <div className="row">
      <span className="k">{k}</span>
      <span className="bar">
        <i className={cls} style={{ width: `${Math.min(100, value * 100)}%` }} />
      </span>
      <span className="v mono">{text}</span>
    </div>
  )
}

export function ControlBar() {
  const project = useStore((s) => s.project)
  const status = useStore((s) => s.status)
  const mode = useStore((s) => s.mode)
  const cmd = useCommands()
  const [editing, setEditing] = useState<'tempo' | 'sig' | 'key' | null>(null)
  const [draft, setDraft] = useState('')

  if (!project) return <div className="controlbar" />
  const sr = project.sample_rate
  const ph = status?.playhead ?? 0
  const bpm = project.tempo.num / project.tempo.den

  const commit = () => {
    if (editing === 'tempo') {
      const v = parseFloat(draft)
      if (Number.isFinite(v) && v >= 20 && v <= 400) void cmd.send({ op: 'set_project', bpm: v })
    } else if (editing === 'sig') {
      const m = draft.match(/^(\d+)\s*\/\s*(\d+)$/)
      if (m) void cmd.send({ op: 'set_project', sig_num: Number(m[1]), sig_den: Number(m[2]) })
    } else if (editing === 'key') {
      if (draft.trim()) void cmd.send({ op: 'set_project', key: draft.trim() })
    }
    setEditing(null)
  }

  const cell = (id: 'tempo' | 'sig' | 'key', k: string, value: string, small = false) => (
    <div
      className="cell clickable"
      onDoubleClick={() => {
        setEditing(id)
        setDraft(value.replace('♩ ', ''))
      }}
      title="Double-click to edit"
    >
      <span className="k">{k}</span>
      {editing === id ? (
        <input
          className={`v mono ${small ? 'small' : ''}`}
          autoFocus
          value={draft}
          onChange={(e) => setDraft(e.target.value)}
          onBlur={commit}
          onKeyDown={(e) => {
            if (e.key === 'Enter') commit()
            if (e.key === 'Escape') setEditing(null)
            e.stopPropagation()
          }}
        />
      ) : (
        <span className={`v ${small ? 'small' : ''}`}>{value}</span>
      )}
    </div>
  )

  const cpu = status?.cpu ?? 0
  const xruns = status?.xruns ?? 0

  return (
    <div className="controlbar">
      <div className="lcd">
        <div className="cell" title="Bars | Beats | Ticks">
          <span className="k">Bars·Beats</span>
          <span className={`v ${status?.recording ? 'rec' : ''}`}>{formatBarsBeats(ph, sr, project.tempo, project.sig)}</span>
        </div>
        <div className="cell" title="Timecode (30 fps)">
          <span className="k">Timecode</span>
          <span className="v">{formatTimecode(ph, sr)}</span>
        </div>
        {mode === 'precision' && (
          <div className="cell" title="Min:Sec">
            <span className="k">Min:Sec</span>
            <span className="v">{formatMinSec(ph, sr)}</span>
          </div>
        )}
        <div className="cell" title="Samples — the ground truth (EDT-01)">
          <span className="k">Samples</span>
          <span className="v">{formatSamples(ph)}</span>
        </div>
        {cell('tempo', 'Tempo', `♩ ${bpm.toFixed(3)}`)}
        {cell('sig', 'Sig', `${project.sig.num}/${project.sig.den}`)}
        {cell('key', 'Key', project.key || '—')}
        {status?.loop && (
          <div className="cell" title="Cycle range">
            <span className="k">Cycle</span>
            <span className="v small">
              {formatBarsBeats(status.loop_start, sr, project.tempo, project.sig).trim()} → {formatBarsBeats(status.loop_end, sr, project.tempo, project.sig).trim()}
            </span>
          </div>
        )}
      </div>
      <div className="gauge">
        <Gauge k="CPU" value={cpu} text={`${(cpu * 100).toFixed(1)}%`} />
        <Gauge k="I/O" value={Math.min(1, xruns / 10)} warnAt={0.01} hotAt={0.3} text={xruns ? `${xruns} xrun` : 'ok'} />
      </div>
      <div className="gauge" style={{ minWidth: 150 }}>
        <div className="row">
          <span className="k">SR</span>
          <span className="v mono" style={{ width: 'auto' }}>
            {status?.sample_rate ?? sr} Hz · {status?.buffer ?? 256} smp
          </span>
        </div>
        <div className="row">
          <span className="k">Lanes</span>
          <span className="v mono" style={{ width: 'auto' }}>
            RT {status?.rt_tracks ?? 0} · MAE {status?.mae_tracks ?? 0}
          </span>
        </div>
      </div>
    </div>
  )
}
