import { useEffect, useState } from 'react'
import type { Region, Track } from '../bridge/types'
import { commands } from '../lib/commands'
import { TRACK_PALETTE, trackColor } from '../lib/palette'
import { faderDb, formatBarsBeats, formatDb, formatMinSec, formatSamples, panLabel } from '../lib/units'
import { useStore } from '../state/store'
import { MSR } from './TrackHeader'

function NumField({ value, onCommit, step = 1, min, max, title }: { value: number; onCommit: (v: number) => void; step?: number; min?: number; max?: number; title?: string }) {
  const [draft, setDraft] = useState(String(value))
  useEffect(() => setDraft(String(value)), [value])
  const commit = () => {
    let v = Number(draft)
    if (!Number.isFinite(v)) return setDraft(String(value))
    if (min !== undefined) v = Math.max(min, v)
    if (max !== undefined) v = Math.min(max, v)
    if (v !== value) onCommit(v)
  }
  return (
    <input
      className="numfield"
      value={draft}
      step={step}
      title={title}
      onChange={(e) => setDraft(e.target.value)}
      onBlur={commit}
      onKeyDown={(e) => {
        e.stopPropagation()
        if (e.key === 'Enter') commit()
        if (e.key === 'ArrowUp') onCommit(value + step * (e.shiftKey ? 10 : 1))
        if (e.key === 'ArrowDown') onCommit(value - step * (e.shiftKey ? 10 : 1))
      }}
    />
  )
}

function RegionSection({ region }: { region: Region }) {
  const project = useStore((s) => s.project)!
  const c = commands()
  const sr = project.sample_rate
  const bb = (s: number) => formatBarsBeats(s, sr, project.tempo, project.sig).trim()
  const set = (patch: Record<string, unknown>) => void c.send({ op: 'set_region', region: region.id, ...patch })
  return (
    <div className="section">
      <div className="title">
        Region <span className="spacer" />
        <span style={{ color: 'var(--ink-3)', fontWeight: 400 }}>{region.kind.toUpperCase()}</span>
      </div>
      <div className="kv">
        <span className="k">Name</span>
        <input className="text" defaultValue={region.name} key={region.id + region.name} onBlur={(e) => e.target.value !== region.name && set({ name: e.target.value })} onKeyDown={(e) => e.key === 'Enter' && (e.target as HTMLInputElement).blur()} />
        <span className="k">Start</span>
        <NumField value={region.start} onCommit={(v) => void c.send({ op: 'move', moves: [{ region: region.id, start: Math.max(0, Math.round(v)) }] })} title="Start (samples)" />
        <span className="k"></span>
        <span className="v mono" style={{ color: 'var(--ink-3)' }}>
          {bb(region.start)} · {formatMinSec(region.start, sr)}
        </span>
        <span className="k">End</span>
        <NumField value={region.start + region.length} onCommit={(v) => void c.send({ op: 'trim', region: region.id, end: Math.max(region.start + 1, Math.round(v)) })} title="End (samples)" />
        <span className="k">Length</span>
        <NumField value={region.length} min={1} onCommit={(v) => void c.send({ op: 'trim', region: region.id, end: region.start + Math.max(1, Math.round(v)) })} title="Length (samples)" />
        {region.kind === 'audio' && (
          <>
            <span className="k">Offset</span>
            <span className="v mono">{formatSamples(region.offset)}</span>
            <span className="k">Gain</span>
            <NumField value={region.gain_db} step={0.1} min={-60} max={24} onCommit={(v) => set({ gain_db: Math.round(v * 10) / 10 })} title="Clip gain (dB)" />
            <span className="k">Fade in</span>
            <NumField value={region.fade_in} min={0} max={region.length} onCommit={(v) => set({ fade_in: Math.round(v) })} title="Fade in (samples)" />
            <span className="k"></span>
            <input className="slider fade" type="range" min={0} max={Math.min(region.length, sr * 4)} value={region.fade_in} onChange={(e) => set({ fade_in: Number(e.target.value) })} />
            <span className="k">Fade out</span>
            <NumField value={region.fade_out} min={0} max={region.length} onCommit={(v) => set({ fade_out: Math.round(v) })} title="Fade out (samples)" />
            <span className="k"></span>
            <input className="slider fade" type="range" min={0} max={Math.min(region.length, sr * 4)} value={region.fade_out} onChange={(e) => set({ fade_out: Number(e.target.value) })} />
          </>
        )}
        {region.kind === 'midi' && (
          <>
            <span className="k">Notes</span>
            <span className="v mono">{region.notes?.length ?? 0}</span>
          </>
        )}
        <span className="k">Mute</span>
        <span className="v">
          <button className={`btn tiny ${region.muted ? 'on' : ''}`} onClick={() => set({ muted: !region.muted })}>
            {region.muted ? 'Muted' : 'Active'}
          </button>
        </span>
        <span className="k">Color</span>
        <span className="v" style={{ display: 'flex', gap: 2, flexWrap: 'wrap' }}>
          <button className="btn tiny" style={{ width: 16, height: 16, padding: 0, background: region.color < 0 ? 'transparent' : trackColor(region.color) }} title="Inherit track color" onClick={() => set({ color: -1 })}>
            {region.color < 0 ? '·' : ''}
          </button>
          {TRACK_PALETTE.slice(0, 12).map((hex, i) => (
            <button key={hex} style={{ width: 14, height: 14, borderRadius: 3, background: hex, opacity: region.color === i ? 1 : 0.55 }} onClick={() => set({ color: i })} />
          ))}
        </span>
      </div>
    </div>
  )
}

function TrackSection({ track }: { track: Track }) {
  const project = useStore((s) => s.project)!
  const sendLive = useStore((s) => s.sendLive)
  const c = commands()
  const outName = track.output === 0 ? 'Master' : project.tracks.find((t) => t.id === track.output)?.name ?? '—'
  return (
    <div className="section">
      <div className="title">
        Track <span className="spacer" />
        <span style={{ color: 'var(--ink-3)', fontWeight: 400 }}>{track.kind}</span>
      </div>
      <div className="kv">
        <span className="k">Name</span>
        <input className="text" key={track.id + track.name} defaultValue={track.name} onBlur={(e) => e.target.value !== track.name && void c.send({ op: 'set_track', track: track.id, param: 'name', value: e.target.value })} onKeyDown={(e) => e.key === 'Enter' && (e.target as HTMLInputElement).blur()} />
        <span className="k">Color</span>
        <span className="v" style={{ display: 'flex', gap: 2, flexWrap: 'wrap' }}>
          {TRACK_PALETTE.map((hex, i) => (
            <button key={hex} style={{ width: 12, height: 12, borderRadius: 3, background: hex, outline: track.color === i ? '1px solid #fff' : 'none' }} onClick={() => void c.send({ op: 'set_track', track: track.id, param: 'color', value: i })} />
          ))}
        </span>
        {track.kind === 'instrument' && (
          <>
            <span className="k">Instrument</span>
            <select className="select" value={track.instrument} onChange={(e) => void c.send({ op: 'set_track', track: track.id, param: 'instrument', value: Number(e.target.value) })}>
              {project.instrument_presets.map((p, i) => (
                <option key={i} value={i}>
                  {p}
                </option>
              ))}
            </select>
          </>
        )}
        <span className="k">Volume</span>
        <span className="v mono">{formatDb(faderDb(track.fader_pos))} dB</span>
        <span className="k"></span>
        <input className="slider" type="range" min={0} max={1} step={0.001} value={track.fader_pos} onChange={(e) => sendLive(`${track.id}:fader`, { op: 'set_track', track: track.id, param: 'fader', value: Number(e.target.value) })} />
        <span className="k">Pan</span>
        <span className="v mono">{panLabel(track.pan)}</span>
        <span className="k"></span>
        <input className="slider" type="range" min={-1} max={1} step={0.01} value={track.pan} onChange={(e) => sendLive(`${track.id}:pan`, { op: 'set_track', track: track.id, param: 'pan', value: Number(e.target.value) })} />
        <span className="k">Output</span>
        <span className="v">{outName}</span>
        <span className="k">State</span>
        <span className="v">
          <MSR track={track} showAutomation />
        </span>
        <span className="k">Inserts</span>
        <span className="v">{track.inserts.filter((i) => i.name).map((i) => i.name).join(', ') || '—'}</span>
        <span className="k">Sends</span>
        <span className="v">
          {track.sends
            .filter((s) => s.active)
            .map((s) => `${project.tracks.find((t) => t.id === s.dest)?.name ?? '?'} ${formatDb(s.level_db, 0)}`)
            .join(', ') || '—'}
        </span>
      </div>
      {track.kind !== 'master' && (
        <div style={{ display: 'flex', gap: 4, marginTop: 8 }}>
          <button className="btn tiny warn" onClick={() => window.confirm(`Delete track "${track.name}" and its regions?`) && void c.send({ op: 'remove_track', track: track.id })}>
            Delete track
          </button>
        </div>
      )}
    </div>
  )
}

function SmartControls({ track }: { track: Track }) {
  // Placeholder macro panel: 8 screen controls wired to visible track params for now (UIW §7).
  const knobs = [
    { name: 'Volume', v: track.fader_pos, text: formatDb(faderDb(track.fader_pos)) },
    { name: 'Pan', v: (track.pan + 1) / 2, text: panLabel(track.pan) },
    { name: 'Send 1', v: track.sends[0]?.active ? (track.sends[0].level_db + 60) / 72 : 0, text: track.sends[0]?.active ? formatDb(track.sends[0].level_db, 0) : '—' },
    { name: 'Send 2', v: track.sends[1]?.active ? (track.sends[1].level_db + 60) / 72 : 0, text: track.sends[1]?.active ? formatDb(track.sends[1].level_db, 0) : '—' },
    { name: 'Cutoff', v: 0.7, text: '70%' },
    { name: 'Reso', v: 0.2, text: '20%' },
    { name: 'Attack', v: 0.1, text: '10 ms' },
    { name: 'Release', v: 0.4, text: '400 ms' },
  ]
  return (
    <div className="section">
      <div className="title">Smart Controls</div>
      <div className="knob-grid">
        {knobs.map((k) => (
          <div className="knob" key={k.name} title={k.name}>
            <div className="ring" style={{ ['--deg' as string]: `${Math.round(k.v * 270 - 135 + 135)}deg` }} />
            <span>{k.name}</span>
            <span className="val">{k.text}</span>
          </div>
        ))}
      </div>
    </div>
  )
}

export function Inspector() {
  const project = useStore((s) => s.project)
  const selection = useStore((s) => s.selection)
  const region = project?.regions.find((r) => r.id === selection.regions[selection.regions.length - 1])
  const track = project?.tracks.find((t) => t.id === (region ? region.track : selection.tracks[0]))
  return (
    <div className="panel inspector">
      <div className="head">Inspector</div>
      <div className="body">
        {region ? (
          <RegionSection region={region} />
        ) : selection.regions.length > 1 ? null : (
          <div className="empty">Select a region to edit its parameters.</div>
        )}
        {track ? <TrackSection track={track} /> : <div className="empty">Select a track.</div>}
        {track && <SmartControls track={track} />}
        {project && (
          <div className="section">
            <div className="title">Project</div>
            <div className="kv">
              <span className="k">Tempo</span>
              <span className="v mono">{(project.tempo.num / project.tempo.den).toFixed(3)} BPM</span>
              <span className="k">Signature</span>
              <span className="v mono">
                {project.sig.num}/{project.sig.den}
              </span>
              <span className="k">Key</span>
              <span className="v">{project.key || '—'}</span>
              <span className="k">Rate</span>
              <span className="v mono">{project.sample_rate} Hz</span>
              <span className="k">End</span>
              <span className="v mono">{formatSamples(project.content_end)} smp</span>
              <span className="k">Media</span>
              <span className="v">{project.sources.length} files</span>
            </div>
          </div>
        )}
      </div>
    </div>
  )
}
