import { useEffect, useRef, useState } from 'react'
import type { Track } from '../bridge/types'
import { commands } from '../lib/commands'
import { trackColor } from '../lib/palette'
import { dbToFader, faderDb, formatDb, gainToDb, meterPos, panLabel } from '../lib/units'
import { STRIP_WIDTH, useStore } from '../state/store'
import { MSR } from './TrackHeader'

const SCALE_MARKS: [number, string][] = [
  [12, '+12'],
  [6, '+6'],
  [0, '0'],
  [-6, '-6'],
  [-12, '-12'],
  [-24, '-24'],
  [-40, '-40'],
  [-60, '-60'],
]

function Fader({ track, travel }: { track: Track; travel: number }) {
  const sendLive = useStore((s) => s.sendLive)
  const ref = useRef<HTMLDivElement>(null)
  const set = (pos: number) => sendLive(`${track.id}:fader`, { op: 'set_track', track: track.id, param: 'fader', value: Math.min(1, Math.max(0, pos)) })

  const onDown = (e: React.MouseEvent) => {
    e.preventDefault()
    if (e.altKey) return set(0.75) // ⌥-click = unity (Logic + PT)
    const start = e.clientY
    const startPos = track.fader_pos
    const move = (ev: MouseEvent) => {
      const fine = ev.shiftKey ? 0.2 : 1
      set(startPos + ((start - ev.clientY) / travel) * fine)
    }
    const up = () => {
      window.removeEventListener('mousemove', move)
      window.removeEventListener('mouseup', up)
    }
    window.addEventListener('mousemove', move)
    window.addEventListener('mouseup', up)
  }
  const capTop = (1 - track.fader_pos) * travel
  return (
    <div
      className="fader"
      ref={ref}
      onMouseDown={onDown}
      onDoubleClick={() => set(0.75)}
      onWheel={(e) => {
        e.preventDefault()
        set(track.fader_pos - e.deltaY * 0.0008)
      }}
      title={`${formatDb(faderDb(track.fader_pos))} dB  ·  ⌥-click / double-click: unity`}
    >
      <div className="track" />
      {SCALE_MARKS.map(([db, label]) => (
        <span key={db} className="scale" style={{ top: (1 - dbToFader(db)) * travel }}>
          {label}
        </span>
      ))}
      <div className="detent" style={{ top: 0.25 * travel }} />
      <div className="cap" style={{ top: capTop }} />
    </div>
  )
}

function Pan({ track, compact = false }: { track: Track; compact?: boolean }) {
  const sendLive = useStore((s) => s.sendLive)
  const set = (pan: number) => sendLive(`${track.id}:pan`, { op: 'set_track', track: track.id, param: 'pan', value: Math.min(1, Math.max(-1, pan)) })
  const onDown = (e: React.MouseEvent) => {
    e.preventDefault()
    if (e.altKey) return set(0)
    const start = e.clientX
    const startPan = track.pan
    const move = (ev: MouseEvent) => set(startPan + ((ev.clientX - start) / 60) * (ev.shiftKey ? 0.25 : 1))
    const up = () => {
      window.removeEventListener('mousemove', move)
      window.removeEventListener('mouseup', up)
    }
    window.addEventListener('mousemove', move)
    window.addEventListener('mouseup', up)
  }
  return (
    <div className={`pan ${compact ? 'compact' : ''}`}>
      <div className="puck" onMouseDown={onDown} onDoubleClick={() => set(0)} title={`Pan ${panLabel(track.pan)} — constant-power (MIX-10)`}>
        <i style={{ left: `${(track.pan + 1) * 50}%` }} />
      </div>
      {!compact && <span className="v">{panLabel(track.pan)}</span>}
    </div>
  )
}

interface StripGeom {
  travel: number
  insertRows: number
  sendRows: number
  automode: boolean
  delay: boolean
  panFull: boolean
}

function Strip({ track, index, geom }: { track: Track; index: number; geom: StripGeom }) {
  const { travel, insertRows, sendRows } = geom
  const status = useStore((s) => s.status)
  const project = useStore((s) => s.project)!
  const selected = useStore((s) => s.selection.tracks.includes(track.id))
  const select = useStore((s) => s.select)
  const wide = useStore((s) => s.views.precision.stripWide)
  const density = useStore((s) => s.density)
  const meter = status?.meters[index]
  const l = meter?.[0] ?? 0
  const r = meter?.[1] ?? 0
  const clipped = meter?.[2] ?? false
  const db = faderDb(track.fader_pos)
  const c = commands()
  const busTargets = project.tracks.filter((t) => t.kind === 'bus' || t.kind === 'aux' || t.kind === 'master')
  const slot = project.slots.find((s) => s.id === track.id)
  const latency = slot?.latency ?? 0
  const isVca = track.kind === 'vca'
  const stripW = wide ? STRIP_WIDTH[density] + (density === 'compact' ? 16 : 16) : 64
  const showRows = track.kind !== 'vca'
  const insertsShown = Math.min(insertRows, wide ? 5 : 3)
  const sendsShown = Math.min(sendRows, wide ? 4 : 2)

  return (
    <div
      className={`strip ${selected ? 'selected' : ''} ${track.kind} ${wide ? '' : 'narrow'}`}
      style={{ ['--strip-w' as string]: `${stripW}px`, ['--track' as string]: trackColor(track.color) }}
      onMouseDown={(e) => {
        const tag = (e.target as HTMLElement).tagName
        if (tag === 'BUTTON' || tag === 'SELECT') return
        select({ tracks: [track.id] })
      }}
    >
      <div className="badge" title={track.name}>
        <span className="nm">{track.name}</span>
        {track.kind === 'vca' && <span style={{ color: 'var(--ink-3)' }}>VCA</span>}
      </div>
      {showRows && insertRows > 0 && (
        <div className="slots" title="Inserts A–E (click to name, ⌥-click bypass)">
          {track.inserts.slice(0, insertsShown).map((ins, i) => (
            <div
              key={i}
              className={`slot ${ins.name ? '' : 'empty'} ${ins.bypassed ? 'byp' : ''}`}
              onClick={(e) => {
                if (e.altKey && ins.name) return void c.send({ op: 'set_insert', track: track.id, index: i, bypassed: !ins.bypassed })
                const name = window.prompt(`Insert ${String.fromCharCode(65 + i)} plug-in name (empty = remove). Declared latency samples after a comma, e.g. "S7 Linear EQ, 1152"`, ins.name ? `${ins.name}${ins.latency ? `, ${ins.latency}` : ''}` : '')
                if (name === null) return
                const [nm, lat] = name.split(',').map((s) => s.trim())
                void c.send({ op: 'set_insert', track: track.id, index: i, name: nm, latency: lat ? Number(lat) || 0 : 0, bypassed: false })
              }}
            >
              <span className="ltr">{String.fromCharCode(65 + i)}</span>
              <span className="nm">{ins.name || '—'}</span>
            </div>
          ))}
        </div>
      )}
      {showRows && sendRows > 0 && (
        <div className="slots" title="Sends 1–4 (destination · level; right-click pre/post)">
          {(track.kind === 'master' ? [] : track.sends.slice(0, sendsShown)).map((snd, i) => (
            <div
              key={i}
              className={`slot send ${snd.active ? '' : 'empty'} ${snd.pre ? 'pre' : ''} ${snd.muted ? 'byp' : ''}`}
              onContextMenu={(e) => {
                e.preventDefault()
                if (snd.active) void c.send({ op: 'set_send', track: track.id, index: i, pre: !snd.pre })
              }}
              onWheel={(e) => {
                if (!snd.active) return
                e.preventDefault()
                void c.send({ op: 'set_send', track: track.id, index: i, level_db: Math.max(-60, Math.min(12, snd.level_db - Math.sign(e.deltaY) * 1)) })
              }}
            >
              <span className="ltr">{i + 1}</span>
              <select
                value={snd.active ? snd.dest : -1}
                onChange={(e) => {
                  const v = Number(e.target.value)
                  if (v < 0) void c.send({ op: 'set_send', track: track.id, index: i, dest: 0 })
                  else void c.send({ op: 'set_send', track: track.id, index: i, dest: v, level_db: snd.active ? snd.level_db : -6 })
                }}
                title="Send destination"
              >
                <option value={-1}>—</option>
                {busTargets
                  .filter((t) => t.id !== track.id && t.kind !== 'master')
                  .map((t) => (
                    <option key={t.id} value={t.id}>
                      {t.name}
                    </option>
                  ))}
              </select>
              {snd.active && <span className="lvl">{formatDb(snd.level_db, 0)}</span>}
            </div>
          ))}
          {track.kind === 'master' &&
            Array.from({ length: sendsShown }, (_, i) => (
              <div key={i} className="slot empty">
                <span className="ltr">{i + 1}</span>
                <span className="nm">—</span>
              </div>
            ))}
        </div>
      )}
      {showRows && (
        <div className="slots" title="I/O">
          <div className="slot io">
            <span className="ltr">in</span>
            {track.kind === 'audio' ? (
              <select value={track.input_channel} onChange={(e) => void c.send({ op: 'set_track', track: track.id, param: 'input', value: Number(e.target.value) })}>
                {[0, 1, 2, 3, 4, 5, 6, 7].map((i) => (
                  <option key={i} value={i}>
                    In {i + 1}
                  </option>
                ))}
              </select>
            ) : (
              <span className="nm" style={{ color: 'var(--ink-3)' }}>
                {track.kind === 'instrument' ? 'MIDI All' : track.kind === 'master' ? 'mix bus' : 'bus'}
              </span>
            )}
          </div>
          <div className="slot io">
            <span className="ltr">out</span>
            {track.kind !== 'master' ? (
              <select value={track.output} onChange={(e) => void c.send({ op: 'set_track', track: track.id, param: 'output', value: Number(e.target.value) })}>
                <option value={0}>Master</option>
                {busTargets
                  .filter((t) => t.id !== track.id && t.kind !== 'master')
                  .map((t) => (
                    <option key={t.id} value={t.id}>
                      {t.name}
                    </option>
                  ))}
              </select>
            ) : (
              <span className="nm">Out 1-2</span>
            )}
          </div>
        </div>
      )}
      {geom.automode && !isVca && track.kind !== 'master' && (
        <div className="automode">
          <select value={track.automation_mode} onChange={(e) => void c.send({ op: 'set_track', track: track.id, param: 'automation_mode', value: e.target.value })} title="Automation mode">
            {['off', 'read', 'touch', 'latch', 'write', 'trim'].map((m) => (
              <option key={m} value={m}>
                {m}
              </option>
            ))}
          </select>
        </div>
      )}
      {!isVca && <Pan track={track} compact={!geom.panFull} />}
      <div className="rsm">
        <MSR track={track} large showInput={false} />
      </div>
      <div className="fader-area" style={{ height: travel + 14 }}>
        <Fader track={track} travel={travel} />
        {!isVca && (
          <div className="meter">
            <div className="ch">
              <i style={{ height: `${meterPos(l) * 100}%` }} />
            </div>
            <div className="ch">
              <i style={{ height: `${meterPos(r) * 100}%` }} />
            </div>
          </div>
        )}
      </div>
      <div className="readout">
        <span>{formatDb(db)}</span>
        <span className={`clip ${clipped ? 'on' : ''}`} title="Clip indicator">
          {clipped ? 'CLIP' : formatDb(gainToDb(Math.max(l, r)), 0)}
        </span>
      </div>
      {geom.delay && (
        <div className="delaybadge" title="Plug-in delay (MIX-07)">
          <span className={`led ${latency ? 'amber' : ''}`} />
          {latency} smp
        </div>
      )}
    </div>
  )
}

function computeGeom(avail: number): StripGeom {
  const g: StripGeom = { travel: 240, insertRows: 5, sendRows: 4, automode: true, delay: true, panFull: true }
  const cost = () => 18 + (g.insertRows ? g.insertRows * 21 + 7 : 0) + (g.sendRows ? g.sendRows * 21 + 7 : 0) + 49 + (g.automode ? 18 : 0) + (g.panFull ? 44 : 22) + 22 + 18 + (g.delay ? 16 : 0) + 12
  const steps: (() => void)[] = [
    () => (g.insertRows = 3),
    () => (g.sendRows = 2),
    () => (g.insertRows = 2),
    () => (g.delay = false),
    () => (g.sendRows = 1),
    () => (g.automode = false),
    () => (g.panFull = false),
    () => (g.insertRows = 1),
    () => (g.sendRows = 0),
  ]
  const minTravel = 120
  for (const step of steps) {
    if (cost() + minTravel <= avail) break
    step()
  }
  g.travel = Math.max(72, Math.min(240, avail - cost()))
  return g
}

export function Mixer({ compact = false }: { compact?: boolean }) {
  const project = useStore((s) => s.project)
  const status = useStore((s) => s.status)
  const setView = useStore((s) => s.setView)
  const wide = useStore((s) => s.views.precision.stripWide)
  const stripsRef = useRef<HTMLDivElement>(null)
  const [height, setHeight] = useState(600)
  useEffect(() => {
    const el = stripsRef.current
    if (!el) return
    const ro = new ResizeObserver(() => setHeight(el.clientHeight))
    ro.observe(el)
    setHeight(el.clientHeight)
    return () => ro.disconnect()
  }, [])
  if (!project) return null
  // Adaptive strip: full PT geometry (5 inserts, 4 sends, 240 px fader) when the pane is tall;
  // rows are shed in priority order when docked small — the fader always stays live and visible.
  const geom = computeGeom(height - 10)
  const c = commands()
  const master = project.tracks.find((t) => t.kind === 'master')
  const others = project.tracks.filter((t) => t.kind !== 'master')
  const vcas = others.filter((t) => t.kind === 'vca')
  const regular = others.filter((t) => t.kind !== 'vca')
  const masterIdx = project.tracks.findIndex((t) => t.kind === 'master')
  const mL = status?.master[0] ?? 0
  const mR = status?.master[1] ?? 0

  return (
    <div className="mixer">
      <div className="mixbar">
        <b style={{ color: 'var(--ink-2)' }}>MIX</b>
        <span>{regular.length} strips</span>
        <span className="spacer" />
        <button className="btn tiny" onClick={() => void c.addTrack('audio')}>
          + Audio
        </button>
        <button className="btn tiny" onClick={() => void c.addTrack('instrument')}>
          + Inst
        </button>
        <button className="btn tiny" onClick={() => void c.addTrack('aux')}>
          + Aux
        </button>
        <button className="btn tiny" onClick={() => void c.addTrack('bus')}>
          + Bus
        </button>
        <button className="btn tiny" onClick={() => void c.addTrack('vca')}>
          + VCA
        </button>
        {!compact && (
          <button className="btn tiny" onClick={() => setView({ stripWide: !wide })}>
            {wide ? 'Narrow' : 'Wide'}
          </button>
        )}
        <span className="mono" title="Master peak" style={{ whiteSpace: 'nowrap' }}>
          Mstr {formatDb(gainToDb(Math.max(mL, mR)), 1)} dB
        </span>
      </div>
      <div className="strips" ref={stripsRef} style={{ ['--fader-travel' as string]: `${geom.travel}px` }}>
        {vcas.map((t) => (
          <Strip key={t.id} track={t} index={project.tracks.indexOf(t)} geom={geom} />
        ))}
        {regular.map((t) => (
          <Strip key={t.id} track={t} index={project.tracks.indexOf(t)} geom={geom} />
        ))}
        {master && <Strip key={master.id} track={master} index={masterIdx} geom={geom} />}
      </div>
    </div>
  )
}
