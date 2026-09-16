// seven7 UI — types mirroring the engine protocol (docs/07-ui-bridge-protocol.md).
// All timeline values are integer samples (EDT-01). Never convert to float time
// except for display.

export type TrackKind = 'audio' | 'instrument' | 'aux' | 'bus' | 'vca' | 'master'
export type RegionKind = 'audio' | 'midi'

export interface Insert {
  name: string
  bypassed: boolean
  latency: number
}

export interface Send {
  dest: number
  level_db: number
  pre: boolean
  muted: boolean
  active: boolean
}

export interface Track {
  id: number
  name: string
  kind: TrackKind
  color: number
  mute: boolean
  solo: boolean
  arm: boolean
  monitor: boolean
  fader_pos: number
  pan: number
  input_channel: number
  output: number
  automation_mode: string
  instrument: number
  inserts: Insert[]
  sends: Send[]
}

/** [tick, length_ticks, sample, length_samples, pitch, velocity] */
export type NoteTuple = [number, number, number, number, number, number]

export interface Region {
  id: number
  track: number
  kind: RegionKind
  source: number
  name: string
  color: number
  start: number
  offset: number
  length: number
  gain_db: number
  fade_in: number
  fade_out: number
  muted: boolean
  notes?: NoteTuple[]
}

export interface Source {
  id: number
  name: string
  length: number
  channels: number
  sample_rate: number
}

export interface Marker {
  id: number
  name: string
  sample: number
}

export interface ProjectState {
  version: number
  schema_rev: number
  name: string
  sample_rate: number
  tempo: { num: number; den: number }
  sig: { num: number; den: number }
  key: string
  tracks: Track[]
  regions: Region[]
  sources: Source[]
  markers: Marker[]
  bundle_path: string
  can_undo: boolean
  can_redo: boolean
  undo_label: string
  redo_label: string
  edit_mode: number
  content_end: number
  slots: { id: number; slot: number; latency: number }[]
  instrument_presets: string[]
}

export interface Status {
  playing: boolean
  recording: boolean
  loop: boolean
  metronome: boolean
  playhead: number
  loop_start: number
  loop_end: number
  cpu: number
  xruns: number
  rt_tracks: number
  mae_tracks: number
  pdc_worst: number
  sample_rate: number
  buffer: number
  version: number
  /** per track (project order): [peakL, peakR, clipped] */
  meters: [number, number, boolean][]
  master: [number, number]
}

export interface CommandResult {
  ok: boolean
  error?: string
  version?: number
  [k: string]: unknown
}

export type Command = { op: string } & Record<string, unknown>

/** The one seam between UI and engine. Implemented by JuceBridge (desktop) and HttpBridge (dev). */
export interface EngineBridge {
  readonly kind: 'juce' | 'http'
  command(cmd: Command): Promise<CommandResult>
  getState(): Promise<ProjectState>
  getStatus(): Promise<Status>
  getPeaks(source: number, from: number, to: number, buckets: number): Promise<number[]>
  /** URL of an offline render for browser audition (HTTP bridge only). */
  renderUrl?(start: number, end: number): string
  /** Native file dialogs (desktop only). Resolve with a path or null. */
  chooseFile?(kind: 'open-project' | 'save-project' | 'import-audio'): Promise<string | null>
  /** Native audio device dialog (desktop only). */
  openAudioSettings?(): Promise<void>
}
