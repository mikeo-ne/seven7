import { useStore } from '../state/store'
import { formatSamples } from '../lib/units'

export function StatusBar() {
  const status = useStore((s) => s.status)
  const project = useStore((s) => s.project)
  const log = useStore((s) => s.log)
  const view = useStore((s) => s.views[s.mode])
  const setView = useStore((s) => s.setView)
  const selection = useStore((s) => s.selection)
  const last = log[log.length - 1]
  const pdc = status?.pdc_worst ?? 0
  const pdcLed = pdc === 0 ? '' : pdc < 4096 ? '' : pdc < 32768 ? 'amber' : 'red'
  // Zoom slider: log scale from 0.00005 (whole song at ~4 px/s) to 4 px/sample.
  const zMin = Math.log(0.00002)
  const zMax = Math.log(4)
  const z = (Math.log(view.pxPerSample) - zMin) / (zMax - zMin)

  return (
    <div className="statusbar">
      <span className="pill" title="Plug-in delay compensation (MIX-07)">
        <span className={`led ${pdcLed}`} />
        PDC: ON · cap 32768 · worst {formatSamples(pdc)} smp
      </span>
      <span className="pill">
        Lanes: RT {status?.rt_tracks ?? 0} · MAE {status?.mae_tracks ?? 0}
      </span>
      <span className="pill">
        Sel: {selection.regions.length} region{selection.regions.length === 1 ? '' : 's'}
        {selection.range ? ` · ${formatSamples(selection.range.end - selection.range.start)} smp range` : ''}
      </span>
      <span className="pill">
        Edit: {view.editMode} · Snap: {view.snap.unit === 'samples' ? `${view.snap.samples} smp` : view.snap.unit === 'division' ? `1/${view.snap.division}` : view.snap.unit}
      </span>
      <span className="spacer" />
      {last && <span className={`msg ${last.level}`}>{last.text}</span>}
      <span className="zoom" title="Horizontal zoom (⌘+ / ⌘−)">
        H
        <input type="range" min={0} max={1} step={0.001} value={z} onChange={(e) => setView({ pxPerSample: Math.exp(zMin + Number(e.target.value) * (zMax - zMin)) })} />
      </span>
      <span className="zoom" title="Vertical zoom">
        V
        <input type="range" min={0.6} max={4} step={0.05} value={view.laneScale} onChange={(e) => setView({ laneScale: Number(e.target.value) })} />
      </span>
      <span className="pill">{project?.bundle_path ? '✓ saved' : '● not saved'}</span>
    </div>
  )
}
