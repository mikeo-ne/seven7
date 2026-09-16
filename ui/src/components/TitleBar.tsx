import { useStore } from '../state/store'
import { useCommands } from '../lib/commands'
import { MenuButton } from './Menu'
import { bridge } from '../bridge'

export function TitleBar() {
  const project = useStore((s) => s.project)
  const mode = useStore((s) => s.mode)
  const setMode = useStore((s) => s.setMode)
  const connected = useStore((s) => s.connected)
  const hostKind = useStore((s) => s.hostKind)
  const density = useStore((s) => s.density)
  const setDensity = useStore((s) => s.setDensity)
  const isMac = /Mac|iPhone|iPad/.test(navigator.platform)
  const view = useStore((s) => s.views[s.mode])
  const cmd = useCommands()

  return (
    <div className="titlebar">
      <div className="brand">
        <span className="gem" />
        seven7
      </div>
      <div className="menubar">
        <MenuButton
          className="ghost"
          label="File"
          items={[
            { label: 'New Project…', shortcut: '⌘N', onSelect: () => void cmd.newProject() },
            { label: 'Open…', shortcut: '⌘O', onSelect: () => void cmd.openProject() },
            { separator: true, label: '' },
            { label: 'Save', shortcut: '⌘S', onSelect: () => void cmd.saveProject() },
            { label: 'Save As…', shortcut: '⇧⌘S', onSelect: () => void cmd.saveProject(true) },
            { separator: true, label: '' },
            { label: 'Import Audio…', onSelect: () => void cmd.importAudio() },
            { label: 'Load Demo “Midnight City”', onSelect: () => void cmd.send({ op: 'new_project', demo: true }) },
            ...(bridge().openAudioSettings
              ? [{ separator: true, label: '' }, { label: 'Audio Settings…', shortcut: '⌘,', onSelect: () => void bridge().openAudioSettings?.() }]
              : []),
          ]}
        />
        <MenuButton
          className="ghost"
          label="Edit"
          items={[
            { label: `Undo ${project?.undo_label ?? ''}`.trim(), shortcut: '⌘Z', disabled: !project?.can_undo, onSelect: () => void cmd.send({ op: 'undo' }) },
            { label: `Redo ${project?.redo_label ?? ''}`.trim(), shortcut: '⇧⌘Z', disabled: !project?.can_redo, onSelect: () => void cmd.send({ op: 'redo' }) },
            { separator: true, label: '' },
            { label: 'Select All', shortcut: '⌘A', onSelect: () => cmd.selectAll() },
            { label: 'Split at Playhead', shortcut: '⌘E', onSelect: () => cmd.splitAtPlayhead() },
            { label: 'Duplicate', shortcut: '⌘D', onSelect: () => cmd.duplicateSelection() },
            { label: 'Mute / Unmute Regions', onSelect: () => cmd.muteSelection() },
            { label: 'Delete', shortcut: '⌫', onSelect: () => cmd.deleteSelection() },
            { separator: true, label: '' },
            { label: 'Nudge Earlier', shortcut: 'Num −', onSelect: () => cmd.nudge(-view.nudge) },
            { label: 'Nudge Later', shortcut: 'Num +', onSelect: () => cmd.nudge(view.nudge) },
          ]}
        />
        <MenuButton
          className="ghost"
          label="Track"
          items={[
            { label: 'New Audio Track', onSelect: () => void cmd.addTrack('audio') },
            { label: 'New Instrument Track', onSelect: () => void cmd.addTrack('instrument', 1) },
            { label: 'New Aux (Return)', onSelect: () => void cmd.addTrack('aux') },
            { label: 'New Bus', onSelect: () => void cmd.addTrack('bus') },
            { label: 'New VCA', onSelect: () => void cmd.addTrack('vca') },
            { separator: true, label: '' },
            { label: 'Panic (all notes off)', onSelect: () => void cmd.send({ op: 'panic' }) },
          ]}
        />
      </div>
      <div className="project">
        <b>{project?.name ?? '—'}</b> ▸ Alt 000{project?.bundle_path ? '' : ' · unsaved'}
      </div>
      <div className="spacer" />
      <span className="hint">Workspace</span>
      <div className="segmented" role="tablist" aria-label="Workspace mode">
        <button role="tab" aria-selected={mode === 'canvas'} className={mode === 'canvas' ? 'on' : ''} onClick={() => setMode('canvas')}>
          Canvas
        </button>
        <button role="tab" aria-selected={mode === 'precision'} className={mode === 'precision' ? 'on precision' : ''} onClick={() => setMode('precision')}>
          Precision
        </button>
      </div>
      <span className="hint">
        <span className="kbd">{isMac ? '⌃⌘M' : 'Ctrl+Alt+M'}</span>
      </span>
      <div className="spacer" />
      <div className="segmented small" title="Density (UIW-01)">
        {(['comfort', 'default', 'compact'] as const).map((d) => (
          <button key={d} className={density === d ? 'on' : ''} onClick={() => setDensity(d)} title={d}>
            {d[0].toUpperCase()}
          </button>
        ))}
      </div>
      <div className={`conn ${connected ? '' : 'off'}`} title={hostKind === 'juce' ? 'Native engine' : 'Dev bridge (s7bridge)'}>
        <span className="dot" />
        {hostKind === 'juce' ? 'engine' : 'bridge'}
      </div>
    </div>
  )
}
