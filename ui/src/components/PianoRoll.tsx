// seven7 UI — Piano roll (drawer editor). Note positions are region-relative samples;
// edits are committed as a whole note list via `set_notes` (the engine re-derives ticks).

import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import type { NoteTuple, Region } from '../bridge/types'
import { commands, snapLocal } from '../lib/commands'
import { clamp, samplesPerBeat } from '../lib/units'
import { useStore } from '../state/store'

const NOTE_H = 10
const NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
const isBlack = (p: number) => [1, 3, 6, 8, 10].includes(p % 12)

interface EditNote {
  sample: number
  length: number
  pitch: number
  velocity: number
}

export function PianoRoll({ region }: { region: Region }) {
  const project = useStore((s) => s.project)!
  const status = useStore((s) => s.status)
  const view = useStore((s) => s.views[s.mode])
  const gridRef = useRef<HTMLDivElement>(null)
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const keysRef = useRef<HTMLCanvasElement>(null)
  const [size, setSize] = useState({ w: 600, h: 200 })
  const [scrollPitch, setScrollPitch] = useState(0) // top pitch offset (0 = 127 at top)
  const [selected, setSelected] = useState<number[]>([])
  const [drag, setDrag] = useState<{ kind: 'move' | 'resize' | 'create' | 'velocity'; idx: number[]; notes: EditNote[]; anchor: { s: number; p: number }; orig: EditNote[] } | null>(null)
  const [pxPerSample, setPxPerSample] = useState(0)
  const sr = project.sample_rate

  const notes: EditNote[] = useMemo(() => (region.notes ?? []).map((n: NoteTuple) => ({ sample: n[2], length: n[3], pitch: n[4], velocity: n[5] })), [region.notes])
  const shown = drag ? drag.notes : notes

  useEffect(() => {
    const el = gridRef.current
    if (!el) return
    const ro = new ResizeObserver(() => setSize({ w: el.clientWidth, h: el.clientHeight }))
    ro.observe(el)
    setSize({ w: el.clientWidth, h: el.clientHeight })
    return () => ro.disconnect()
  }, [])

  // Fit region horizontally on first show / region change; center on the notes vertically.
  useEffect(() => {
    setPxPerSample(size.w / Math.max(1, region.length))
    const pitches = notes.map((n) => n.pitch)
    const center = pitches.length ? (Math.min(...pitches) + Math.max(...pitches)) / 2 : 60
    setScrollPitch(clamp(Math.round(127 - center - size.h / NOTE_H / 2), 0, 127 - Math.floor(size.h / NOTE_H)))
  }, [region.id, region.length, size.w, size.h]) // eslint-disable-line react-hooks/exhaustive-deps

  const px = pxPerSample || size.w / Math.max(1, region.length)
  const toX = (s: number) => s * px
  const toS = (x: number) => x / px
  const toY = (pitch: number) => (127 - pitch - scrollPitch) * NOTE_H
  const toPitch = (y: number) => 127 - scrollPitch - Math.floor(y / NOTE_H)

  const snap = useCallback(
    (s: number) => {
      const unit = view.snap.unit === 'off' ? { ...view.snap, unit: 'division' as const } : view.snap
      return snapLocal(s, unit, sr, project.tempo, project.sig)
    },
    [view.snap, sr, project.tempo, project.sig],
  )

  // ─── draw ────────────────────────────────────────────────────────────────
  useEffect(() => {
    const cv = canvasRef.current
    const kv = keysRef.current
    if (!cv || !kv) return
    const dpr = window.devicePixelRatio || 1
    cv.width = size.w * dpr
    cv.height = size.h * dpr
    cv.style.width = `${size.w}px`
    cv.style.height = `${size.h}px`
    kv.width = 44 * dpr
    kv.height = size.h * dpr
    kv.style.width = '44px'
    kv.style.height = `${size.h}px`
    const ctx = cv.getContext('2d')!
    const kctx = kv.getContext('2d')!
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
    kctx.setTransform(dpr, 0, 0, dpr, 0, 0)
    const css = getComputedStyle(document.documentElement)
    const tok = (n: string) => css.getPropertyValue(n).trim()
    ctx.clearRect(0, 0, size.w, size.h)
    kctx.clearRect(0, 0, 44, size.h)

    // rows
    const rows = Math.ceil(size.h / NOTE_H) + 1
    for (let i = 0; i < rows; i++) {
      const pitch = 127 - scrollPitch - i
      if (pitch < 0) break
      const y = i * NOTE_H
      ctx.fillStyle = isBlack(pitch) ? 'rgba(0,0,0,0.22)' : 'rgba(255,255,255,0.015)'
      ctx.fillRect(0, y, size.w, NOTE_H)
      ctx.fillStyle = pitch % 12 === 0 ? tok('--grid-major') : tok('--grid-minor')
      ctx.fillRect(0, y + NOTE_H - 1, size.w, 1)
      // keys
      kctx.fillStyle = isBlack(pitch) ? '#1a1c22' : '#d9dbe2'
      kctx.fillRect(0, y, 44, NOTE_H - 1)
      if (pitch % 12 === 0) {
        kctx.fillStyle = '#333'
        kctx.font = `9px ${tok('--font-mono')}`
        kctx.textBaseline = 'middle'
        kctx.fillText(`C${Math.floor(pitch / 12) - 2}`, 24, y + NOTE_H / 2)
      }
    }
    // grid columns (beats / divisions)
    const beat = samplesPerBeat(sr, project.tempo)
    const div = beat / 4
    const step = div * px >= 8 ? div : beat * px >= 8 ? beat : beat * project.sig.num
    for (let s = 0; s <= region.length; s += step) {
      const x = Math.round(toX(s)) + 0.5
      const isBar = Math.abs((s / (beat * project.sig.num)) % 1) < 1e-6
      const isBeat = Math.abs((s / beat) % 1) < 1e-6
      ctx.strokeStyle = isBar ? 'rgba(255,255,255,0.22)' : isBeat ? tok('--grid-major') : tok('--grid-minor')
      ctx.beginPath()
      ctx.moveTo(x, 0)
      ctx.lineTo(x, size.h)
      ctx.stroke()
    }
    // notes
    shown.forEach((n, i) => {
      const x = toX(n.sample)
      const w = Math.max(3, toX(n.length) - 1)
      const y = toY(n.pitch)
      if (y + NOTE_H < 0 || y > size.h) return
      const sel = selected.includes(i)
      const v = n.velocity / 127
      ctx.fillStyle = sel ? '#ffffff' : `hsl(${210 - v * 60} ${70 + v * 30}% ${45 + v * 20}%)`
      ctx.fillRect(x, y + 1, w, NOTE_H - 2)
      ctx.strokeStyle = 'rgba(0,0,0,0.6)'
      ctx.strokeRect(x + 0.5, y + 1.5, w - 1, NOTE_H - 3)
    })
    // playhead (region-relative)
    if (status) {
      const rel = status.playhead - region.start
      if (rel >= 0 && rel <= region.length) {
        const x = Math.round(toX(rel)) + 0.5
        ctx.strokeStyle = tok('--acc-primary')
        ctx.beginPath()
        ctx.moveTo(x, 0)
        ctx.lineTo(x, size.h)
        ctx.stroke()
      }
    }
  }, [shown, selected, size, scrollPitch, px, status?.playhead, region, sr, project, status]) // eslint-disable-line react-hooks/exhaustive-deps

  const commit = (list: EditNote[]) => {
    void commands().send({ op: 'set_notes', region: region.id, notes: list.map((n) => ({ sample: Math.round(n.sample), length: Math.max(1, Math.round(n.length)), pitch: n.pitch, velocity: n.velocity })) })
  }

  const hit = (x: number, y: number) => {
    const p = toPitch(y)
    const s = toS(x)
    for (let i = shown.length - 1; i >= 0; i--) {
      const n = shown[i]
      if (n.pitch === p && s >= n.sample && s <= n.sample + n.length) {
        const edge = toX(n.sample + n.length) - x <= 5
        return { idx: i, edge }
      }
    }
    return null
  }

  const onDown = (e: React.MouseEvent) => {
    const rect = gridRef.current!.getBoundingClientRect()
    const x = e.clientX - rect.left
    const y = e.clientY - rect.top
    const h = hit(x, y)
    const tool = view.tool
    const beat = samplesPerBeat(sr, project.tempo)

    if (h && (tool === 'eraser' || e.altKey && tool === 'pencil')) {
      const list = notes.filter((_, i) => i !== h.idx)
      setSelected([])
      commit(list)
      return
    }
    if (!h && (tool === 'pencil' || e.detail === 2)) {
      const s = snap(toS(x))
      const n: EditNote = { sample: clamp(s, 0, region.length - 1), length: Math.round(view.snap.unit === 'division' ? (beat * 4) / view.snap.division : beat / 4), pitch: clamp(toPitch(y), 0, 127), velocity: 100 }
      const list = [...notes, n]
      setSelected([list.length - 1])
      void commands().send({ op: 'note_on', track: region.track, pitch: n.pitch, velocity: n.velocity })
      window.setTimeout(() => void commands().send({ op: 'note_off', track: region.track, pitch: n.pitch }), 150)
      setDrag({ kind: 'create', idx: [list.length - 1], notes: list, anchor: { s: toS(x), p: n.pitch }, orig: list })
      startDrag(rect, 'create', [list.length - 1], list, { s: toS(x), p: n.pitch })
      return
    }
    if (!h) {
      setSelected([])
      return
    }
    const sel = selected.includes(h.idx) ? selected : e.shiftKey ? [...selected, h.idx] : [h.idx]
    setSelected(sel)
    const n = notes[h.idx]
    void commands().send({ op: 'note_on', track: region.track, pitch: n.pitch, velocity: n.velocity })
    window.setTimeout(() => void commands().send({ op: 'note_off', track: region.track, pitch: n.pitch }), 150)
    const kind = e.metaKey || e.ctrlKey ? 'velocity' : h.edge ? 'resize' : 'move'
    startDrag(rect, kind, sel, notes, { s: toS(x), p: toPitch(y) })
  }

  const startDrag = (rect: DOMRect, kind: 'move' | 'resize' | 'create' | 'velocity', idx: number[], base: EditNote[], anchor: { s: number; p: number }) => {
    const orig = base.map((n) => ({ ...n }))
    let cur = base
    setDrag({ kind, idx, notes: base, anchor, orig })
    const move = (ev: MouseEvent) => {
      const x = ev.clientX - rect.left
      const y = ev.clientY - rect.top
      const ds = toS(x) - anchor.s
      const dp = toPitch(y) - anchor.p
      cur = orig.map((n, i) => {
        if (!idx.includes(i)) return n
        if (kind === 'move') {
          const ns = ev.shiftKey ? n.sample : snap(n.sample + ds)
          return { ...n, sample: clamp(ns, 0, region.length - 1), pitch: clamp(n.pitch + dp, 0, 127) }
        }
        if (kind === 'resize' || kind === 'create') {
          const end = snap(n.sample + n.length + ds)
          return { ...n, length: Math.max(kind === 'create' ? n.length : 1, end - n.sample) }
        }
        return { ...n, velocity: clamp(Math.round(n.velocity - ev.movementY * 0), 1, 127) }
      })
      if (kind === 'velocity') {
        const dv = Math.round(-(ev.clientY - (rect.top + toY(anchor.p))) / 2)
        cur = orig.map((n, i) => (idx.includes(i) ? { ...n, velocity: clamp(n.velocity + dv, 1, 127) } : n))
      }
      setDrag({ kind, idx, notes: cur, anchor, orig })
    }
    const up = () => {
      window.removeEventListener('mousemove', move)
      window.removeEventListener('mouseup', up)
      setDrag(null)
      commit(cur)
    }
    window.addEventListener('mousemove', move)
    window.addEventListener('mouseup', up)
  }

  const onKey = (e: React.KeyboardEvent) => {
    if (e.key === 'Backspace' || e.key === 'Delete') {
      if (!selected.length) return
      e.preventDefault()
      e.stopPropagation()
      commit(notes.filter((_, i) => !selected.includes(i)))
      setSelected([])
    } else if (e.key === 'a' && (e.metaKey || e.ctrlKey)) {
      e.preventDefault()
      e.stopPropagation()
      setSelected(notes.map((_, i) => i))
    }
  }

  const onWheel = (e: React.WheelEvent) => {
    if (e.ctrlKey || e.metaKey) {
      e.preventDefault()
      setPxPerSample(clamp(px * Math.exp(-e.deltaY * 0.01), size.w / Math.max(1, region.length) / 2, 2))
    } else if (e.shiftKey) {
      // horizontal scroll not needed while region fits; reserved
    } else {
      setScrollPitch((p) => clamp(p + Math.sign(e.deltaY) * 2, 0, 127 - Math.floor(size.h / NOTE_H)))
    }
  }

  return (
    <div className="pianoroll" onKeyDown={onKey} tabIndex={0}>
      <div className="keys">
        <canvas ref={keysRef} />
      </div>
      <div ref={gridRef} className="grid" onMouseDown={onDown} onWheel={onWheel} style={{ cursor: view.tool === 'pencil' ? 'crosshair' : 'default' }} title="Double-click / Pencil: add note · drag: move · right edge: length · ⌘-drag: velocity · ⌫: delete">
        <canvas ref={canvasRef} />
        <div className="toast mono" style={{ bottom: 6, right: 8, left: 'auto', transform: 'none', padding: '3px 8px', fontSize: 10 }}>
          {region.name} · {notes.length} notes · {selected.length ? `${selected.length} sel · ${NAMES[notes[selected[0]]?.pitch % 12]}${Math.floor((notes[selected[0]]?.pitch ?? 0) / 12) - 2} vel ${notes[selected[0]]?.velocity}` : 'PPQ 960'}
        </div>
      </div>
    </div>
  )
}
