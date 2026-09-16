// seven7 UI — user-level commands shared by toolbar, menus and keyboard.
// Everything resolves to Controller ops; nothing here mutates project data locally.

import { useMemo } from 'react'
import { bridge } from '../bridge'
import type { Command } from '../bridge/types'
import { useStore, type Snap } from '../state/store'
import { samplesPerBar, samplesPerBeat } from './units'

export function snapCommand(snap: Snap, sample: number): Command {
  return { op: 'snap', unit: snap.unit, division: snap.division, samples: snap.samples, fps: 30, sample }
}

/** Client-side snap for drag feedback (the engine's `snap` op is authoritative for commits). */
export function snapLocal(sample: number, snap: Snap, sr: number, tempo: { num: number; den: number }, sig: { num: number; den: number }, fps = 30): number {
  let step: number
  switch (snap.unit) {
    case 'bar':
      step = samplesPerBar(sr, tempo, sig)
      break
    case 'beat':
      step = samplesPerBeat(sr, tempo) * (4 / sig.den)
      break
    case 'division':
      step = (samplesPerBeat(sr, tempo) * 4) / snap.division
      break
    case 'samples':
      step = Math.max(1, snap.samples)
      break
    case 'seconds':
      step = sr
      break
    case 'frames':
      step = sr / fps
      break
    default:
      return Math.round(sample)
  }
  return Math.max(0, Math.round(Math.round(sample / step) * step))
}

function makeCommands() {
  const s = useStore.getState
  const send = (c: Command) => s().send(c)

  const selectedRegions = () => s().selection.regions
  const project = () => s().project
  const bar = () => {
    const p = project()
    return p ? samplesPerBar(p.sample_rate, p.tempo, p.sig) : 96000
  }

  return {
    send,

    locateBars(delta: number) {
      const st = s().status
      if (!st) return
      const b = bar()
      const cur = Math.round(st.playhead / b)
      void send({ op: 'locate', sample: Math.max(0, Math.round((cur + delta) * b)) })
    },

    toggleLoop() {
      const st = s().status
      const p = project()
      if (!st || !p) return
      if (st.loop) {
        void send({ op: 'loop', on: false })
        return
      }
      const range = s().selection.range
      let start = st.loop_start
      let end = st.loop_end
      if (range && range.end > range.start) {
        start = range.start
        end = range.end
      } else if (end <= start) {
        const regs = selectedRegions().map((id) => s().region(id)).filter(Boolean)
        if (regs.length) {
          start = Math.min(...regs.map((r) => r!.start))
          end = Math.max(...regs.map((r) => r!.start + r!.length))
        } else {
          start = 0
          end = Math.max(p.content_end, Math.round(bar()))
        }
      }
      void send({ op: 'loop', on: true, start, end })
    },

    nudge(delta: number) {
      const ids = selectedRegions()
      if (!ids.length) return
      void send({ op: 'nudge', regions: ids, delta })
    },

    splitAtPlayhead() {
      const st = s().status
      const p = project()
      if (!st || !p) return
      let ids = selectedRegions()
      if (!ids.length) ids = p.regions.filter((r) => r.start < st.playhead && r.start + r.length > st.playhead).map((r) => r.id)
      if (!ids.length) return
      void send({ op: 'split_at_playhead', regions: ids }).then((r) => {
        const made = (r as { regions?: number[] }).regions
        if (made?.length) s().select({ regions: made })
      })
    },

    deleteSelection() {
      const ids = selectedRegions()
      if (!ids.length) return
      void send({ op: 'delete', regions: ids })
      s().select({ regions: [] })
    },

    duplicateSelection() {
      const ids = selectedRegions()
      if (!ids.length) return
      void send({ op: 'duplicate', regions: ids }).then((r) => {
        const made = (r as { regions?: number[] }).regions
        if (made?.length) s().select({ regions: made })
      })
    },

    muteSelection() {
      const ids = selectedRegions()
      const p = project()
      if (!ids.length || !p) return
      const anyUnmuted = ids.some((id) => !p.regions.find((r) => r.id === id)?.muted)
      for (const id of ids) void send({ op: 'set_region', region: id, muted: anyUnmuted })
    },

    selectAll() {
      const p = project()
      if (p) s().select({ regions: p.regions.map((r) => r.id) })
    },

    async addTrack(kind: string, instrument = 1) {
      const p = project()
      const n = p ? p.tracks.length : 0
      const name = kind === 'audio' ? `Audio ${n}` : kind === 'instrument' ? `Inst ${n}` : kind === 'aux' ? `Aux ${n}` : kind === 'bus' ? `Bus ${n}` : `VCA ${n}`
      const r = await send({ op: 'add_track', kind, name, color: (n * 5) % 24, instrument })
      const id = (r as { track?: number }).track
      if (id !== undefined) s().select({ tracks: [id], regions: [] })
    },

    async newProject() {
      if (!window.confirm('Start a new project? Unsaved changes are lost.')) return
      await send({ op: 'new_project', demo: false })
      s().clearSelection()
    },

    async openProject() {
      const b = bridge()
      let path: string | null = null
      if (b.chooseFile) path = await b.chooseFile('open-project')
      else path = window.prompt('Path to a .s7proj bundle on the engine host:', project()?.bundle_path || '/tmp/Midnight City.s7proj')
      if (!path) return
      await send({ op: 'open', path })
      s().clearSelection()
    },

    async saveProject(saveAs = false) {
      const p = project()
      if (!p) return
      let path: string | undefined = p.bundle_path || undefined
      if (!path || saveAs) {
        const b = bridge()
        if (b.chooseFile) path = (await b.chooseFile('save-project')) ?? undefined
        else path = window.prompt('Save bundle as (directory path on the engine host):', `/tmp/${p.name}.s7proj`) ?? undefined
        if (!path) return
      }
      await send({ op: 'save', path })
    },

    async importAudio() {
      const b = bridge()
      const p = project()
      let path: string | null = null
      if (b.chooseFile) path = await b.chooseFile('import-audio')
      else path = window.prompt('Path to a WAV/BWF file on the engine host:', '/tmp/s7_ctrl_import.wav')
      if (!path || !p) return
      const trackSel = s().selection.tracks[0]
      const track = p.tracks.find((t) => t.id === trackSel && t.kind === 'audio')?.id ?? 0
      const at = s().status?.playhead ?? 0
      await send({ op: 'import_audio', path, track, sample: at })
    },
  }
}

let cached: ReturnType<typeof makeCommands> | null = null
export function commands() {
  if (!cached) cached = makeCommands()
  return cached
}
export function useCommands() {
  return useMemo(() => commands(), [])
}
