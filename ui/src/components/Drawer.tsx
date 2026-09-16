import { useStore, type DrawerTab } from '../state/store'
import { AudioEditor } from './AudioEditor'
import { Mixer } from './Mixer'
import { PianoRoll } from './PianoRoll'
import { commands } from '../lib/commands'
import { Icon } from './Icon'

const TABS: { id: DrawerTab; label: string }[] = [
  { id: 'smart', label: 'Smart Controls' },
  { id: 'piano', label: 'Piano Roll' },
  { id: 'step', label: 'Step' },
  { id: 'audio', label: 'Audio' },
  { id: 'mixer', label: 'Mixer' },
  { id: 'loops', label: 'Live Loops' },
]

function StepSequencer() {
  const project = useStore((s) => s.project)
  const selection = useStore((s) => s.selection)
  const region = project?.regions.find((r) => r.id === selection.regions[selection.regions.length - 1] && r.kind === 'midi')
  if (!project || !region) return <div className="empty">Select a MIDI region to see its steps.</div>
  const beat = (project.sample_rate * 60 * project.tempo.den) / project.tempo.num
  const step = beat / 4
  const steps = Math.min(64, Math.max(16, Math.round(region.length / step)))
  const rows = Array.from(new Set((region.notes ?? []).map((n) => n[4]))).sort((a, b) => b - a).slice(0, 12)
  const has = (pitch: number, i: number) => (region.notes ?? []).some((n) => n[4] === pitch && Math.floor(n[2] / step) === i)
  const toggle = (pitch: number, i: number) => {
    const notes = (region.notes ?? []).map((n) => ({ sample: n[2], length: n[3], pitch: n[4], velocity: n[5] }))
    const idx = notes.findIndex((n) => n.pitch === pitch && Math.floor(n.sample / step) === i)
    if (idx >= 0) notes.splice(idx, 1)
    else notes.push({ sample: Math.round(i * step), length: Math.round(step * 0.9), pitch, velocity: 100 })
    void commands().send({ op: 'set_notes', region: region.id, notes })
  }
  const NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
  return (
    <div style={{ padding: 8, overflow: 'auto', height: '100%' }}>
      <div style={{ display: 'grid', gridTemplateColumns: `48px repeat(${steps}, minmax(14px, 1fr))`, gap: 2, fontSize: 10 }}>
        {rows.map((pitch) => (
          <>
            <div key={`l${pitch}`} className="mono" style={{ color: 'var(--ink-2)', alignSelf: 'center' }}>
              {NAMES[pitch % 12]}
              {Math.floor(pitch / 12) - 2}
            </div>
            {Array.from({ length: steps }, (_, i) => (
              <button
                key={`${pitch}:${i}`}
                onClick={() => toggle(pitch, i)}
                style={{
                  height: 16,
                  borderRadius: 2,
                  background: has(pitch, i) ? 'var(--acc-primary)' : i % 4 === 0 ? 'rgba(255,255,255,0.08)' : 'rgba(255,255,255,0.04)',
                }}
              />
            ))}
          </>
        ))}
      </div>
      <div className="empty">1/16 grid · {steps} steps · click to toggle. Rows follow the pitches present in the region.</div>
    </div>
  )
}

export function Drawer() {
  const view = useStore((s) => s.views[s.mode])
  const setView = useStore((s) => s.setView)
  const project = useStore((s) => s.project)
  const selection = useStore((s) => s.selection)
  const region = project?.regions.find((r) => r.id === selection.regions[selection.regions.length - 1])
  const tab = view.drawerTab

  return (
    <div className="drawer">
      <div className="tabs">
        {TABS.map((t) => (
          <button key={t.id} className={tab === t.id ? 'on' : ''} onClick={() => setView({ drawerTab: t.id })}>
            {t.label}
          </button>
        ))}
        <span className="spacer" />
        <button className="btn tiny ghost" onClick={() => setView({ drawer: false })} title="Close (Y)" aria-label="Close editor">
          <Icon name="close" size={12} />
        </button>
      </div>
      <div className="content">
        {tab === 'piano' && (region?.kind === 'midi' ? <PianoRoll region={region} /> : <div className="empty">Select a MIDI region — or draw one with the Pencil tool on an instrument track.</div>)}
        {tab === 'audio' && (region?.kind === 'audio' ? <AudioEditor region={region} /> : <div className="empty">Select an audio region.</div>)}
        {tab === 'mixer' && <Mixer compact />}
        {tab === 'step' && <StepSequencer />}
        {tab === 'smart' && <div className="empty">Smart Controls live in the Inspector for the selected track (macro mapping UI lands with plug-in hosting, ARC-PGN).</div>}
        {tab === 'loops' && <div className="empty">Live Loops grid (tracks × scenes) — scheduled after the shell milestone (UIW §7).</div>}
      </div>
    </div>
  )
}
