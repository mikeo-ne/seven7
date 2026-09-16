// seven7 UI — application store (zustand).
//
// Project data lives in the engine; this store holds the last snapshot plus
// purely-presentational state. Every edit goes through `send()` which forwards
// to Controller::command and re-syncs when the engine's state version moves.

import { create } from 'zustand'
import { bridge, onHostEvent } from '../bridge'
import type { Command, CommandResult, ProjectState, Region, Status, Track } from '../bridge/types'

export type Mode = 'canvas' | 'precision'
export type RulerFormat = 'bars' | 'minsec' | 'samples' | 'timecode'
export type Tool = 'pointer' | 'smart' | 'marquee' | 'pencil' | 'scissors' | 'eraser' | 'zoom'
export type DragMode = 'overlap' | 'no-overlap' | 'xfade'
export type EditMode = 'slip' | 'grid' | 'shuffle' | 'spot'
export type SnapUnit = 'bar' | 'beat' | 'division' | 'samples' | 'seconds' | 'frames' | 'off'
export type Density = 'comfort' | 'default' | 'compact'
export type DrawerTab = 'smart' | 'piano' | 'step' | 'audio' | 'mixer' | 'loops'

export interface Snap {
  unit: SnapUnit
  division: number // for 'division': 4 = 1/4 note … 16 = 1/16
  samples: number // for 'samples'
}

export interface ModeView {
  pxPerSample: number
  scrollSample: number
  scrollY: number
  ruler: RulerFormat
  tool: Tool
  laneScale: number // vertical zoom multiplier
  snap: Snap
  // Canvas panes
  inspector: boolean
  library: boolean
  drawer: boolean
  drawerTab: DrawerTab
  dragMode: DragMode
  automation: boolean
  // Precision panes
  editList: boolean
  mixSplit: number // 0.2 … 0.8 of content height given to the mixer
  stripWide: boolean
  editMode: EditMode
  nudge: number // samples
  kbFocus: boolean
}

export interface Selection {
  regions: number[]
  tracks: number[]
  range: { start: number; end: number; tracks: number[] } | null
}

export interface LogEntry {
  t: number
  level: 'info' | 'warn' | 'error'
  text: string
}

export const LANE_HEIGHT: Record<Density, number> = { comfort: 52, default: 44, compact: 34 }
export const STRIP_WIDTH: Record<Density, number> = { comfort: 96, default: 80, compact: 64 }

const canvasDefaults: ModeView = {
  pxPerSample: 0.0014,
  scrollSample: 0,
  scrollY: 0,
  ruler: 'bars',
  tool: 'pointer',
  laneScale: 1.5,
  snap: { unit: 'beat', division: 16, samples: 1 },
  inspector: true,
  library: true,
  drawer: true,
  drawerTab: 'piano',
  dragMode: 'xfade',
  automation: false,
  editList: false,
  mixSplit: 0.4,
  stripWide: true,
  editMode: 'slip',
  nudge: 1,
  kbFocus: false,
}

const precisionDefaults: ModeView = {
  ...canvasDefaults,
  ruler: 'minsec',
  tool: 'smart',
  laneScale: 1.25,
  snap: { unit: 'samples', division: 16, samples: 1 },
  inspector: false,
  library: false,
  drawer: false,
  editList: true,
  editMode: 'grid',
  nudge: 1,
}

interface StoreState {
  connected: boolean
  hostKind: 'juce' | 'http'
  project: ProjectState | null
  status: Status | null
  mode: Mode
  modeSwitching: boolean
  density: Density
  views: Record<Mode, ModeView>
  selection: Selection
  log: LogEntry[]
  recordStart: number | null
  spotRegion: number | null

  // engine I/O
  send: (cmd: Command) => Promise<CommandResult>
  sendLive: (key: string, cmd: Command) => void
  refresh: () => Promise<void>
  pollStatus: () => Promise<void>
  /** Native menu item chosen in the desktop host (event "s7Menu"). */
  hostMenu: (action: string) => Promise<void>
  start: () => void

  // presentation
  setMode: (m: Mode) => void
  toggleMode: () => void
  setDensity: (d: Density) => void
  setView: (patch: Partial<ModeView>) => void
  view: () => ModeView
  select: (patch: Partial<Selection>, additive?: boolean) => void
  clearSelection: () => void
  pushLog: (level: LogEntry['level'], text: string) => void
  setSpotRegion: (id: number | null) => void

  // derived helpers
  track: (id: number) => Track | undefined
  region: (id: number) => Region | undefined
}

/** Applies a mixer move locally so faders track the pointer between status polls. */
function patchTrack(project: ProjectState, id: number, param: string, value: unknown): ProjectState {
  const tracks = project.tracks.map((t) => {
    if (t.id !== id) return t
    switch (param) {
      case 'fader':
        return { ...t, fader_pos: Number(value) }
      case 'pan':
        return { ...t, pan: Number(value) }
      case 'mute':
        return { ...t, mute: Boolean(value) }
      case 'solo':
        return { ...t, solo: Boolean(value) }
      case 'arm':
        return { ...t, arm: Boolean(value) }
      case 'monitor':
        return { ...t, monitor: Boolean(value) }
      default:
        return t
    }
  })
  return { ...project, tracks }
}

const liveQueue = new Map<string, Command>()
let liveScheduled = false

export const useStore = create<StoreState>()((set, get) => ({
  connected: false,
  hostKind: bridge().kind,
  project: null,
  status: null,
  mode: 'canvas',
  modeSwitching: false,
  density: 'default',
  views: { canvas: { ...canvasDefaults }, precision: { ...precisionDefaults } },
  selection: { regions: [], tracks: [], range: null },
  log: [],
  recordStart: null,
  spotRegion: null,

  async send(cmd) {
    const b = bridge()
    try {
      const res = await b.command(cmd)
      if (!res.ok) {
        get().pushLog('error', `${cmd.op}: ${res.error ?? 'failed'}`)
        return res
      }
      const v = get().project?.version
      if (res.version !== undefined && res.version !== v) await get().refresh()
      return res
    } catch (e) {
      get().pushLog('error', `${cmd.op}: ${(e as Error).message}`)
      set({ connected: false })
      return { ok: false, error: String(e) }
    }
  },

  sendLive(key, cmd) {
    // Optimistic local patch for fader/pan/switches; coalesce to one command per frame per key.
    const p = get().project
    if (p && cmd.op === 'set_track') set({ project: patchTrack(p, Number(cmd.track), String(cmd.param), cmd.value) })
    liveQueue.set(key, cmd)
    if (liveScheduled) return
    liveScheduled = true
    requestAnimationFrame(() => {
      liveScheduled = false
      const batch = [...liveQueue.values()]
      liveQueue.clear()
      const b = bridge()
      for (const c of batch) {
        b.command(c)
          .then((r) => {
            if (!r.ok) get().pushLog('error', `${c.op}: ${r.error}`)
            else if (r.version !== undefined && get().project) set({ project: { ...get().project!, version: r.version } })
          })
          .catch((e) => get().pushLog('error', String(e)))
      }
    })
  },

  async refresh() {
    try {
      const project = await bridge().getState()
      set({ project, connected: true })
      // prune selection of vanished regions/tracks
      const sel = get().selection
      const regions = sel.regions.filter((id) => project.regions.some((r) => r.id === id))
      const tracks = sel.tracks.filter((id) => project.tracks.some((t) => t.id === id))
      if (regions.length !== sel.regions.length || tracks.length !== sel.tracks.length) set({ selection: { ...sel, regions, tracks } })
    } catch (e) {
      set({ connected: false })
      get().pushLog('error', `state: ${(e as Error).message}`)
    }
  },

  async pollStatus() {
    try {
      const status = await bridge().getStatus()
      const prev = get().status
      const st: Partial<StoreState> = { status, connected: true }
      if (status.recording && !prev?.recording) st.recordStart = status.playhead
      if (!status.recording && prev?.recording) st.recordStart = null
      set(st)
      const p = get().project
      if (!p || status.version !== p.version) await get().refresh()
    } catch {
      set({ connected: false })
    }
  },

  async hostMenu(action) {
    const { commands } = await import('../lib/commands')
    const c = commands()
    switch (action) {
      case 'newProject': return c.newProject()
      case 'openProject': return c.openProject()
      case 'saveProject': return c.saveProject(false)
      case 'saveProjectAs': return c.saveProject(true)
      case 'importAudio': return c.importAudio()
      case 'undo': await c.send({ op: 'undo' }); return
      case 'redo': await c.send({ op: 'redo' }); return
      case 'toggleMode': get().toggleMode(); return
      default: get().pushLog('warn', `unknown host menu action: ${action}`)
    }
  },

  start() {
    const b = bridge()
    void get().refresh()
    if (b.kind === 'juce') {
      onHostEvent('s7Status', (payload) => {
        const status = (typeof payload === 'string' ? JSON.parse(payload) : payload) as Status
        const prev = get().status
        const st: Partial<StoreState> = { status, connected: true }
        if (status.recording && !prev?.recording) st.recordStart = status.playhead
        if (!status.recording && prev?.recording) st.recordStart = null
        set(st)
        const p = get().project
        if (!p || status.version !== p.version) void get().refresh()
      })
      onHostEvent('s7StateChanged', () => void get().refresh())
      onHostEvent('s7Log', (payload) => get().pushLog('info', String(payload)))
      onHostEvent('s7Menu', (payload) => void get().hostMenu(String(payload)))
    } else {
      let busy = false
      window.setInterval(() => {
        if (busy || document.hidden) return
        busy = true
        void get()
          .pollStatus()
          .finally(() => (busy = false))
      }, 33)
    }
  },

  setMode(mode) {
    if (mode === get().mode) return
    set({ modeSwitching: true })
    // UIW-02: 120 ms crossfade, no sliding panels. Audio never touches this path.
    window.setTimeout(() => set({ mode, modeSwitching: false }), 120)
    // Keep the engine's edit-mode in step with the mode's remembered edit mode.
    void get().send({ op: 'edit_mode', mode: get().views[mode].editMode })
  },
  toggleMode() {
    get().setMode(get().mode === 'canvas' ? 'precision' : 'canvas')
  },
  setDensity(density) {
    set({ density })
  },
  setView(patch) {
    const mode = get().mode
    set({ views: { ...get().views, [mode]: { ...get().views[mode], ...patch } } })
  },
  view() {
    return get().views[get().mode]
  },
  select(patch, additive = false) {
    const cur = get().selection
    const next: Selection = { ...cur, ...patch }
    if (additive && patch.regions) next.regions = Array.from(new Set([...cur.regions, ...patch.regions]))
    if (additive && patch.tracks) next.tracks = Array.from(new Set([...cur.tracks, ...patch.tracks]))
    set({ selection: next })
  },
  clearSelection() {
    set({ selection: { regions: [], tracks: [], range: null } })
  },
  pushLog(level, text) {
    set({ log: [...get().log.slice(-199), { t: Date.now(), level, text }] })
  },
  setSpotRegion(id) {
    set({ spotRegion: id })
  },

  track: (id) => get().project?.tracks.find((t) => t.id === id),
  region: (id) => get().project?.regions.find((r) => r.id === id),
}))

/** Convenience selectors */
export const useProject = () => useStore((s) => s.project)
export const useStatus = () => useStore((s) => s.status)
export const useView = () => useStore((s) => s.views[s.mode])
export const useMode = () => useStore((s) => s.mode)
