import { useEffect, useRef, useState } from 'react'
import { useStore } from '../state/store'
import { Arrange } from './Arrange'
import { Drawer } from './Drawer'
import { EditList } from './EditList'
import { Inspector } from './Inspector'
import { Library } from './Library'
import { Mixer } from './Mixer'
import { Icon } from './Icon'

function CanvasWorkspace() {
  const view = useStore((s) => s.views.canvas)
  const setView = useStore((s) => s.setView)
  return (
    <div className={`canvas-layout ${view.inspector ? 'has-inspector' : ''} ${view.library ? 'has-library' : ''}`}>
      <div className="rail" title="Browser strip">
        <button className={`btn ${view.inspector ? 'on' : ''}`} title="Inspector (I)" aria-label="Inspector" onClick={() => setView({ inspector: !view.inspector })}>
          <Icon name="inspector" />
        </button>
        <button className={`btn ${view.library ? 'on' : ''}`} title="Library (⌥L)" aria-label="Library" onClick={() => setView({ library: !view.library })}>
          <Icon name="library" />
        </button>
        <button className={`btn ${view.drawer ? 'on' : ''}`} title="Editors (Y)" aria-label="Editors" onClick={() => setView({ drawer: !view.drawer })}>
          <Icon name="editor" />
        </button>
        <button className={`btn ${view.drawer && view.drawerTab === 'mixer' ? 'on' : ''}`} title="Mixer (X)" aria-label="Mixer" onClick={() => setView({ drawer: !(view.drawer && view.drawerTab === 'mixer'), drawerTab: 'mixer' })}>
          <Icon name="mixer" />
        </button>
        <button className={`btn ${view.automation ? 'on' : ''}`} title="Automation (A)" aria-label="Automation" onClick={() => setView({ automation: !view.automation })}>
          <Icon name="automation" />
        </button>
      </div>
      {view.inspector && <Inspector />}
      <div className="center">
        <Arrange headerWidth={260} />
      </div>
      {view.library && <Library />}
      {view.drawer && <Drawer />}
    </div>
  )
}

function PrecisionWorkspace() {
  const view = useStore((s) => s.views.precision)
  const setView = useStore((s) => s.setView)
  const ref = useRef<HTMLDivElement>(null)
  const [dragging, setDragging] = useState(false)

  const onDividerDown = (e: React.MouseEvent) => {
    e.preventDefault()
    setDragging(true)
    const move = (ev: MouseEvent) => {
      const rect = ref.current!.getBoundingClientRect()
      const frac = 1 - (ev.clientY - rect.top) / rect.height
      setView({ mixSplit: Math.min(0.8, Math.max(0.2, frac)) })
    }
    const up = () => {
      setDragging(false)
      window.removeEventListener('mousemove', move)
      window.removeEventListener('mouseup', up)
    }
    window.addEventListener('mousemove', move)
    window.addEventListener('mouseup', up)
  }

  return (
    <div className="precision-layout" ref={ref} style={{ gridTemplateRows: `minmax(0, ${1 - view.mixSplit}fr) 6px minmax(0, ${view.mixSplit}fr)`, cursor: dragging ? 'row-resize' : undefined }}>
      <div className="edit-row">
        <div className="center">
          <Arrange headerWidth={360} />
        </div>
        {view.editList && <EditList />}
      </div>
      <div className="divider-h" onMouseDown={onDividerDown} title="Drag: edit/mix split (20–80%)" />
      <div style={{ minHeight: 0 }}>
        <Mixer />
      </div>
    </div>
  )
}

export function Workspace() {
  const mode = useStore((s) => s.mode)
  const switching = useStore((s) => s.modeSwitching)
  const connected = useStore((s) => s.connected)
  const hostKind = useStore((s) => s.hostKind)
  const project = useStore((s) => s.project)
  const [t0] = useState(() => performance.now())
  const [slow, setSlow] = useState(false)
  useEffect(() => {
    const id = window.setInterval(() => setSlow(!useStore.getState().connected && performance.now() - t0 > 1500), 500)
    return () => window.clearInterval(id)
  }, [t0])

  return (
    <div className={`workspace ${switching ? 'switching' : ''}`}>
      {mode === 'canvas' ? <CanvasWorkspace /> : <PrecisionWorkspace />}
      {!connected && slow && !project && (
        <div className="disconnected">
          <div>Waiting for the seven7 engine…</div>
          {hostKind === 'http' && (
            <div style={{ fontSize: 12, color: 'var(--ink-3)' }}>
              Start the dev bridge: <code>./build/s7bridge --port 8787</code> (Vite proxies <code>/api</code> to it)
            </div>
          )}
        </div>
      )}
    </div>
  )
}
