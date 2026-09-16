import { useState } from 'react'
import type { Track } from '../bridge/types'
import { commands } from '../lib/commands'
import { trackColor } from '../lib/palette'
import { Icon } from './Icon'
import { meterPos } from '../lib/units'
import { useStore } from '../state/store'

export function MSR({ track, large = false, showInput = true, showAutomation = false }: { track: Track; large?: boolean; showInput?: boolean; showAutomation?: boolean }) {
  const sendLive = useStore((s) => s.sendLive)
  const set = (param: string, value: unknown) => sendLive(`${track.id}:${param}`, { op: 'set_track', track: track.id, param, value })
  const canRecord = track.kind === 'audio' || track.kind === 'instrument'
  const isMaster = track.kind === 'master'
  return (
    <div className={`msr ${large ? 'large' : ''}`}>
      {!isMaster && (
        <button className={`m ${track.mute ? 'on' : ''}`} title="Mute (M)" onClick={() => set('mute', !track.mute)}>
          M
        </button>
      )}
      {!isMaster && (
        <button className={`s ${track.solo ? 'on' : ''}`} title="Solo (S)" onClick={() => set('solo', !track.solo)}>
          S
        </button>
      )}
      {canRecord && (
        <button className={`r ${track.arm ? 'on' : ''}`} title="Record arm" onClick={() => set('arm', !track.arm)}>
          R
        </button>
      )}
      {canRecord && showInput && (
        <button className={`i ${track.monitor ? 'on' : ''}`} title="Input monitor" onClick={() => set('monitor', !track.monitor)}>
          I
        </button>
      )}
      {showAutomation && !isMaster && (
        <button className={`a ${track.automation_mode !== 'read' && track.automation_mode !== 'off' ? 'on' : ''}`} title={`Automation: ${track.automation_mode}`}>
          A
        </button>
      )}
    </div>
  )
}

export function TrackHeader({ track, index, height, wide }: { track: Track; index: number; height: number; wide: boolean }) {
  const selected = useStore((s) => s.selection.tracks.includes(track.id))
  const select = useStore((s) => s.select)
  const status = useStore((s) => s.status)
  const project = useStore((s) => s.project)
  const [editing, setEditing] = useState(false)
  const [draft, setDraft] = useState(track.name)
  const meter = status?.meters[index]
  const peak = meter ? Math.max(meter[0], meter[1]) : 0
  const slot = project?.slots.find((s) => s.id === track.id)
  const latency = slot?.latency ?? 0
  const outName = track.output === 0 ? 'Out' : project?.tracks.find((t) => t.id === track.output)?.name ?? '—'
  const presets = project?.instrument_presets ?? []

  const commitName = () => {
    setEditing(false)
    if (draft.trim() && draft !== track.name) void commands().send({ op: 'set_track', track: track.id, param: 'name', value: draft.trim() })
  }

  const compact = height < 40
  return (
    <div
      className={`track-header ${selected ? 'selected' : ''}`}
      style={{ height }}
      onMouseDown={(e) => {
        if ((e.target as HTMLElement).tagName === 'BUTTON' || (e.target as HTMLElement).tagName === 'SELECT' || (e.target as HTMLElement).tagName === 'INPUT') return
        select({ tracks: e.shiftKey ? [...useStore.getState().selection.tracks, track.id] : [track.id], regions: e.shiftKey ? useStore.getState().selection.regions : [] })
      }}
      onDoubleClick={(e) => {
        if ((e.target as HTMLElement).tagName === 'BUTTON') return
        setDraft(track.name)
        setEditing(true)
      }}
    >
      <div className="swatch" style={{ background: trackColor(track.color) }} />
      <div className="main">
        <div className="row">
          <span className="num">{index + 1}</span>
          <span className="glyph" title={track.kind}>
            <Icon name={track.kind === 'audio' ? 'audio' : track.kind === 'instrument' ? 'midi' : track.kind === 'master' ? 'mixer' : track.kind === 'vca' ? 'automation' : 'loop'} size={11} />
          </span>
          {editing ? (
            <input
              className="name"
              autoFocus
              value={draft}
              onChange={(e) => setDraft(e.target.value)}
              onBlur={commitName}
              onKeyDown={(e) => {
                e.stopPropagation()
                if (e.key === 'Enter') commitName()
                if (e.key === 'Escape') setEditing(false)
              }}
            />
          ) : (
            <span className="name">{track.name}</span>
          )}
          {compact && <MSR track={track} showInput={false} />}
        </div>
        {!compact && (
          <div className="row">
            <MSR track={track} showAutomation />
            {track.kind === 'instrument' && wide && (
              <select
                className="select tiny"
                value={track.instrument}
                onChange={(e) => void commands().send({ op: 'set_track', track: track.id, param: 'instrument', value: Number(e.target.value) })}
                title="Instrument"
                style={{ maxWidth: 110 }}
              >
                {presets.map((p, i) => (
                  <option key={i} value={i}>
                    {p}
                  </option>
                ))}
              </select>
            )}
            {!wide && track.kind === 'instrument' && <span className="io">{presets[track.instrument]?.replace('S7 ', '')}</span>}
          </div>
        )}
        {wide && !compact && height >= 52 && (
          <div className="row">
            <span className="io">
              {track.kind === 'audio' && <span className="chip on">in {track.input_channel + 1}</span>}
              {track.kind !== 'master' && <span className="chip">→ {outName}</span>}
              {track.inserts.filter((i) => i.name).length > 0 && <span className="chip">{track.inserts.filter((i) => i.name).length} ins</span>}
              {track.sends.filter((s) => s.active).length > 0 && <span className="chip">{track.sends.filter((s) => s.active).length} snd</span>}
            </span>
            <span className="delay" title="Plug-in delay (MIX-07)">
              <span className="led" style={{ background: latency === 0 ? 'var(--acc-ok)' : latency < 4096 ? 'var(--acc-warn)' : 'var(--acc-record)' }} />
              {latency} smp
            </span>
          </div>
        )}
      </div>
      {track.kind !== 'vca' && (
        <div className="meter-v" title={`${(20 * Math.log10(Math.max(1e-6, peak))).toFixed(1)} dB`}>
          <i style={{ height: `${meterPos(peak) * 100}%`, ['--meter-h' as string]: `${height}px` }} />
        </div>
      )}
    </div>
  )
}
