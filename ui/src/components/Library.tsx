import { useState } from 'react'
import { commands } from '../lib/commands'
import { formatMinSec, formatSamples } from '../lib/units'
import { useStore } from '../state/store'
import { Icon } from './Icon'

export function Library() {
  const project = useStore((s) => s.project)
  const status = useStore((s) => s.status)
  const [tab, setTab] = useState<'media' | 'instruments' | 'markers' | 'log'>('media')
  const log = useStore((s) => s.log)
  if (!project) return null
  const c = commands()

  return (
    <div className="panel right library">
      <div className="head">
        Library <span className="spacer" />
        <div className="segmented small">
          {(['media', 'instruments', 'markers', 'log'] as const).map((t) => (
            <button key={t} className={tab === t ? 'on' : ''} onClick={() => setTab(t)}>
              {t === 'instruments' ? 'Inst' : t[0].toUpperCase() + t.slice(1)}
            </button>
          ))}
        </div>
      </div>
      <div className="body">
        {tab === 'media' && (
          <div className="list">
            {project.sources.length === 0 && <div className="empty">No audio files yet. Import a WAV or record a take.</div>}
            {project.sources.map((s) => (
              <div
                key={s.id}
                className="item"
                title="Double-click: place at playhead on the selected audio track"
                onDoubleClick={() => {
                  const sel = useStore.getState().selection.tracks[0]
                  const track = project.tracks.find((t) => t.id === sel && t.kind === 'audio') ?? project.tracks.find((t) => t.kind === 'audio')
                  if (!track) return
                  const at = status?.playhead ?? 0
                  // Place: duplicate any region referencing the source then move it — or create via import path if known.
                  const existing = project.regions.find((r) => r.source === s.id)
                  if (existing) {
                    void c.send({ op: 'duplicate', regions: [existing.id] }).then((r) => {
                      const made = (r as { regions?: number[] }).regions
                      if (made?.length) void c.send({ op: 'move', moves: [{ region: made[0], start: at, track: track.id }] })
                    })
                  }
                }}
              >
                <span className="ic">
                  <Icon name="audio" size={12} />
                </span>
                <span className="nm">{s.name}</span>
                <span className="meta">
                  {s.channels}ch · {formatMinSec(s.length, s.sample_rate)}
                </span>
              </div>
            ))}
            <button className="btn" style={{ marginTop: 6 }} onClick={() => void c.importAudio()}>
              Import audio…
            </button>
          </div>
        )}
        {tab === 'instruments' && (
          <div className="list">
            {project.instrument_presets.map((p, i) => (
              <div key={p} className="item" title="Double-click: new instrument track with this preset" onDoubleClick={() => void c.addTrack('instrument', i)}>
                <span className="ic" style={{ background: 'var(--acc-autom)', color: '#fff' }}>
                  <Icon name="midi" size={12} />
                </span>
                <span className="nm">{p}</span>
                <span className="meta">S7 synth</span>
              </div>
            ))}
            <div className="empty">Double-click a preset to create an instrument track. Third-party AU/VST3 hosting arrives with ARC-PGN.</div>
          </div>
        )}
        {tab === 'markers' && (
          <div className="list">
            {project.markers.map((m) => (
              <div key={m.id} className="item" onDoubleClick={() => void c.send({ op: 'locate', sample: m.sample })} title="Double-click: locate">
                <span className="ic" style={{ background: 'var(--acc-solo)', color: '#2c2300' }}>
                  <Icon name="marker" size={12} />
                </span>
                <span className="nm">{m.name}</span>
                <span className="meta">{formatSamples(m.sample)}</span>
                <button className="btn tiny ghost" onClick={() => void c.send({ op: 'remove_marker', marker: m.id })} title="Remove" aria-label="Remove marker">
                  <Icon name="close" size={10} />
                </button>
              </div>
            ))}
            <button
              className="btn"
              style={{ marginTop: 6 }}
              onClick={() => {
                const name = window.prompt('Marker name', `Marker ${project.markers.length + 1}`)
                if (name) void c.send({ op: 'add_marker', name, sample: status?.playhead ?? 0 })
              }}
            >
              Add marker at playhead
            </button>
          </div>
        )}
        {tab === 'log' && (
          <div className="list mono" style={{ fontSize: 10 }}>
            {log.length === 0 && <div className="empty">Engine messages appear here.</div>}
            {[...log].reverse().map((l, i) => (
              <div key={i} style={{ color: l.level === 'error' ? 'var(--acc-record)' : 'var(--ink-2)', padding: '2px 4px' }}>
                {new Date(l.t).toLocaleTimeString()} {l.text}
              </div>
            ))}
          </div>
        )}
      </div>
    </div>
  )
}
