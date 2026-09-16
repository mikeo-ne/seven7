import { useStore, type DragMode, type EditMode, type SnapUnit, type Tool } from '../state/store'
import { useCommands } from '../lib/commands'
import { Icon } from './Icon'

const CANVAS_TOOLS: { id: Tool; label: string; icon: string }[] = [
  { id: 'pointer', label: 'Pointer', icon: 'pointer' },
  { id: 'pencil', label: 'Pencil', icon: 'pencil' },
  { id: 'scissors', label: 'Scissors', icon: 'scissors' },
  { id: 'marquee', label: 'Marquee', icon: 'marquee' },
  { id: 'eraser', label: 'Eraser', icon: 'eraser' },
  { id: 'zoom', label: 'Zoom', icon: 'zoom' },
]
const PRECISION_TOOLS: { id: Tool; label: string; icon: string }[] = [
  { id: 'smart', label: 'Smart Tool', icon: 'smart' },
  { id: 'pointer', label: 'Grabber', icon: 'grabber' },
  { id: 'marquee', label: 'Selector', icon: 'selector' },
  { id: 'scissors', label: 'Separate', icon: 'scissors' },
  { id: 'pencil', label: 'Pencil', icon: 'pencil' },
  { id: 'zoom', label: 'Zoomer', icon: 'zoom' },
]

const SNAP_UNITS: { id: SnapUnit; label: string }[] = [
  { id: 'bar', label: 'Bar' },
  { id: 'beat', label: 'Beat' },
  { id: 'division', label: 'Division' },
  { id: 'samples', label: 'Samples' },
  { id: 'seconds', label: 'Min:Sec' },
  { id: 'frames', label: 'Frames' },
  { id: 'off', label: 'Off' },
]

const NUDGE_VALUES = [1, 10, 100, 1000, 4800, 48000]

export function Toolbar() {
  const mode = useStore((s) => s.mode)
  const view = useStore((s) => s.views[s.mode])
  const setView = useStore((s) => s.setView)
  const status = useStore((s) => s.status)
  const project = useStore((s) => s.project)
  const cmd = useCommands()
  const tools = mode === 'canvas' ? CANVAS_TOOLS : PRECISION_TOOLS

  const setEditMode = (m: EditMode) => {
    setView({ editMode: m })
    void cmd.send({ op: 'edit_mode', mode: m })
  }

  return (
    <div className="toolbar" role="toolbar">
      {/* Tool cluster */}
      <div className="group">
        {tools.map((t) => (
          <button key={t.id} className={`btn icon ${view.tool === t.id ? 'on' : ''}`} title={t.label} aria-label={t.label} onClick={() => setView({ tool: t.id })}>
            <Icon name={t.icon} />
          </button>
        ))}
      </div>

      {/* Mode cluster — swaps contents but keeps screen position (UIW-02) */}
      <div className="group">
        {mode === 'canvas' ? (
          <>
            <span className="label">Drag</span>
            <select className="select" value={view.dragMode} onChange={(e) => setView({ dragMode: e.target.value as DragMode })}>
              <option value="overlap">Overlap</option>
              <option value="no-overlap">No Overlap</option>
              <option value="xfade">X-Fade</option>
            </select>
          </>
        ) : (
          <>
            <span className="label">Edit</span>
            <div className="segmented">
              {(['slip', 'grid', 'shuffle', 'spot'] as EditMode[]).map((m) => (
                <button key={m} className={view.editMode === m ? 'on precision' : ''} onClick={() => setEditMode(m)}>
                  {m === 'shuffle' ? 'Shfl' : m[0].toUpperCase() + m.slice(1)}
                </button>
              ))}
            </div>
          </>
        )}
      </div>

      <div className="group">
        <span className="label">Snap</span>
        <select className="select" value={view.snap.unit} onChange={(e) => setView({ snap: { ...view.snap, unit: e.target.value as SnapUnit } })}>
          {SNAP_UNITS.map((u) => (
            <option key={u.id} value={u.id}>
              {u.label}
            </option>
          ))}
        </select>
        {view.snap.unit === 'division' && (
          <select className="select" value={view.snap.division} onChange={(e) => setView({ snap: { ...view.snap, division: Number(e.target.value) } })}>
            {[2, 4, 8, 12, 16, 24, 32, 64].map((d) => (
              <option key={d} value={d}>
                1/{d}
              </option>
            ))}
          </select>
        )}
        {view.snap.unit === 'samples' && (
          <input
            className="numfield"
            type="number"
            min={1}
            value={view.snap.samples}
            onChange={(e) => setView({ snap: { ...view.snap, samples: Math.max(1, Number(e.target.value) || 1) } })}
            title="Snap grid in samples (tolerance exactly 0 — UIW-07)"
          />
        )}
      </div>

      {mode === 'precision' && (
        <div className="group">
          <span className="label">Nudge</span>
          <select className="select" value={view.nudge} onChange={(e) => setView({ nudge: Number(e.target.value) })}>
            {NUDGE_VALUES.map((n) => (
              <option key={n} value={n}>
                {n >= 48000 ? `${n / 48000} s` : n >= 4800 ? `${n / 48} ms` : `${n} smp`}
              </option>
            ))}
          </select>
          <button className="btn icon" title="Nudge earlier (−)" aria-label="Nudge earlier" onClick={() => cmd.nudge(-view.nudge)}>
            <Icon name="nudgeL" />
          </button>
          <button className="btn icon" title="Nudge later (+)" aria-label="Nudge later" onClick={() => cmd.nudge(view.nudge)}>
            <Icon name="nudgeR" />
          </button>
        </div>
      )}

      {/* Transport */}
      <div className="group">
        <div className="transport">
          <button className="btn" title="Return to zero (Return)" aria-label="Return to zero" onClick={() => cmd.send({ op: 'return_to_zero' })}>
            <Icon name="rtz" />
          </button>
          <button className="btn" title="Rewind a bar (,)" aria-label="Rewind" onClick={() => cmd.locateBars(-1)}>
            <Icon name="rew" />
          </button>
          <button className={`btn play ${status?.playing ? 'on' : ''}`} title="Play/Stop (Space)" aria-label="Play" onClick={() => cmd.send({ op: 'toggle_play' })}>
            <Icon name={status?.playing ? 'pause' : 'play'} />
          </button>
          <button className="btn" title="Stop (Space / 0)" aria-label="Stop" onClick={() => cmd.send({ op: 'stop' })}>
            <Icon name="stop" />
          </button>
          <button className={`btn rec ${status?.recording ? 'on' : ''}`} title="Record (R)" aria-label="Record" onClick={() => cmd.send({ op: 'record', on: !status?.recording })}>
            <Icon name="record" />
          </button>
          <button className="btn" title="Forward a bar (.)" aria-label="Forward" onClick={() => cmd.locateBars(1)}>
            <Icon name="ffw" />
          </button>
        </div>
      </div>

      <div className="group">
        <button className={`btn ${status?.loop ? 'on' : ''}`} title="Cycle (L)" onClick={() => cmd.toggleLoop()}>
          <Icon name="loop" /> Cycle
        </button>
        <button className={`btn ${status?.metronome ? 'on' : ''}`} title="Metronome (K)" onClick={() => cmd.send({ op: 'metronome', on: !status?.metronome })}>
          <Icon name="metronome" /> Click
        </button>
      </div>

      <div className="group">
        <button className="btn icon" disabled={!project?.can_undo} title={`Undo ${project?.undo_label ?? ''} (⌘Z)`} aria-label="Undo" onClick={() => cmd.send({ op: 'undo' })}>
          <Icon name="undo" />
        </button>
        <button className="btn icon" disabled={!project?.can_redo} title={`Redo ${project?.redo_label ?? ''} (⇧⌘Z)`} aria-label="Redo" onClick={() => cmd.send({ op: 'redo' })}>
          <Icon name="redo" />
        </button>
      </div>

      <div className="spacer" />

      {mode === 'canvas' ? (
        <div className="group">
          <button className={`btn ${view.inspector ? 'on' : ''}`} title="Inspector (I)" onClick={() => setView({ inspector: !view.inspector })}>
            <Icon name="inspector" /> Inspector
          </button>
          <button className={`btn ${view.drawer ? 'on' : ''}`} title="Editor drawer (Y)" onClick={() => setView({ drawer: !view.drawer })}>
            <Icon name="editor" /> Editor
          </button>
          <button className={`btn ${view.library ? 'on' : ''}`} title="Library (⌥L)" onClick={() => setView({ library: !view.library })}>
            <Icon name="library" /> Library
          </button>
          <button className={`btn icon ${view.automation ? 'on' : ''}`} title="Automation overlay (A)" aria-label="Automation" onClick={() => setView({ automation: !view.automation })}>
            <Icon name="automation" />
          </button>
        </div>
      ) : (
        <div className="group">
          <button className={`btn ${view.editList ? 'on' : ''}`} title="Edit list (⌥=)" onClick={() => setView({ editList: !view.editList })}>
            <Icon name="list" /> List
          </button>
          <button className={`btn ${view.kbFocus ? 'on primary' : ''}`} title="Keyboard focus — single-key commands (⌥⌘K)" onClick={() => setView({ kbFocus: !view.kbFocus })}>
            <Icon name="focus" /> KB
          </button>
          <button className={`btn ${view.stripWide ? '' : 'on'}`} title="Narrow strips" onClick={() => setView({ stripWide: !view.stripWide })}>
            {view.stripWide ? 'Wide' : 'Narrow'}
          </button>
        </div>
      )}

    </div>
  )
}
