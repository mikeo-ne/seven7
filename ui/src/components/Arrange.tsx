// seven7 UI — Arrange view: ruler + track headers + region lanes.
// Lanes are canvas-drawn (thousands of regions / 60 fps playhead). Geometry is
// integer-sample based: x = (sample − scrollSample) · pxPerSample.

import { useCallback, useEffect, useMemo, useRef, useState } from 'react'
import type { Region, Track } from '../bridge/types'
import { commands, snapLocal } from '../lib/commands'
import { onPeaksReady, peakRequest, peaksFor } from '../lib/peaks'
import { regionFill, trackColor } from '../lib/palette'
import { clamp, formatBarsBeats, formatMinSec, formatSamples, formatTimecode, samplesPerBar, samplesPerBeat } from '../lib/units'
import { LANE_HEIGHT, useStore, type Tool } from '../state/store'
import { TrackHeader } from './TrackHeader'

interface Layout {
  tracks: { track: Track; y: number; h: number; index: number }[]
  total: number
}

type Gesture =
  | { kind: 'none' }
  | { kind: 'move'; ids: number[]; anchorSample: number; anchorTrackIndex: number; orig: Map<number, { start: number; track: number }>; copy: boolean; dSample: number; dTrack: number; moved: boolean }
  | { kind: 'trim'; id: number; edge: 'head' | 'tail'; orig: Region; sample: number }
  | { kind: 'fade'; id: number; edge: 'in' | 'out'; orig: Region; value: number }
  | { kind: 'gain'; id: number; orig: Region; value: number }
  | { kind: 'range'; startSample: number; endSample: number; trackA: number; trackB: number }
  | { kind: 'marquee'; x0: number; y0: number; x1: number; y1: number }
  | { kind: 'scrub' }
  | { kind: 'pencil'; track: number; startSample: number; endSample: number }

const HANDLE = 6 // px hot zone for trim / fade handles (UIW-06)

export function Arrange({ headerWidth }: { headerWidth: number }) {
  const project = useStore((s) => s.project)
  const status = useStore((s) => s.status)
  const mode = useStore((s) => s.mode)
  const view = useStore((s) => s.views[s.mode])
  const setView = useStore((s) => s.setView)
  const density = useStore((s) => s.density)
  const selection = useStore((s) => s.selection)
  const select = useStore((s) => s.select)
  const recordStart = useStore((s) => s.recordStart)
  const spotRegion = useStore((s) => s.setSpotRegion)

  const lanesRef = useRef<HTMLDivElement>(null)
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const rulerRef = useRef<HTMLCanvasElement>(null)
  const headersRef = useRef<HTMLDivElement>(null)
  const [size, setSize] = useState({ w: 800, h: 400 })
  const [gesture, setGesture] = useState<Gesture>({ kind: 'none' })
  const gestureRef = useRef<Gesture>({ kind: 'none' })
  const [hover, setHover] = useState<{ cursor: string; tip?: string }>({ cursor: 'default' })
  const [, bump] = useState(0)

  const sr = project?.sample_rate ?? 48000
  const laneH = Math.round(LANE_HEIGHT[density] * view.laneScale)

  // Visible layout: every non-VCA track gets a lane; master gets a thin one.
  const layout: Layout = useMemo(() => {
    const out: Layout = { tracks: [], total: 0 }
    if (!project) return out
    let y = 0
    project.tracks.forEach((t, index) => {
      const h = t.kind === 'master' ? Math.max(26, Math.round(laneH * 0.6)) : t.kind === 'vca' ? Math.max(22, Math.round(laneH * 0.5)) : laneH
      out.tracks.push({ track: t, y, h, index })
      y += h
    })
    out.total = y
    return out
  }, [project, laneH])

  useEffect(() => {
    const el = lanesRef.current
    if (!el) return
    const ro = new ResizeObserver(() => setSize({ w: el.clientWidth, h: el.clientHeight }))
    ro.observe(el)
    setSize({ w: el.clientWidth, h: el.clientHeight })
    return () => ro.disconnect()
  }, [])

  useEffect(() => onPeaksReady(() => bump((n) => n + 1)), [])

  const pxPerSample = view.pxPerSample
  const toX = useCallback((sample: number) => (sample - view.scrollSample) * pxPerSample, [view.scrollSample, pxPerSample])
  const toSample = useCallback((x: number) => view.scrollSample + x / pxPerSample, [view.scrollSample, pxPerSample])
  const trackAtY = useCallback(
    (y: number) => {
      const yy = y + view.scrollY
      return layout.tracks.find((t) => yy >= t.y && yy < t.y + t.h) ?? null
    },
    [layout, view.scrollY],
  )

  const doSnap = useCallback(
    (sample: number, force = false) => {
      if (!project) return Math.round(sample)
      if (!force && (view.editMode === 'slip' || view.snap.unit === 'off')) return Math.round(sample)
      return snapLocal(sample, view.snap, sr, project.tempo, project.sig)
    },
    [project, view.snap, view.editMode, sr],
  )

  // Follow playhead while playing (page-flip like Logic/PT when it leaves the view).
  useEffect(() => {
    if (!status?.playing) return
    const x = toX(status.playhead)
    if (x > size.w - 40 || x < 0) setView({ scrollSample: Math.max(0, status.playhead - 40 / pxPerSample) })
  }, [status?.playhead, status?.playing, toX, size.w, pxPerSample, setView])

  // Live regions being drawn (drag feedback) — computed from gesture state.
  const displayRegions = useMemo(() => {
    if (!project) return [] as Region[]
    const g = gesture
    return project.regions.map((r) => {
      if (g.kind === 'move' && g.orig.has(r.id)) {
        const o = g.orig.get(r.id)!
        const ti = project.tracks.findIndex((t) => t.id === o.track)
        const nt = project.tracks[clamp(ti + g.dTrack, 0, project.tracks.length - 1)]
        const target = nt && nt.kind !== 'master' && nt.kind !== 'vca' && (nt.kind === 'instrument') === (r.kind === 'midi') ? nt.id : o.track
        return { ...r, start: Math.max(0, o.start + g.dSample), track: target }
      }
      if (g.kind === 'trim' && g.id === r.id) {
        if (g.edge === 'head') {
          const ns = clamp(g.sample, r.kind === 'audio' ? g.orig.start - g.orig.offset : 0, g.orig.start + g.orig.length - 1)
          return { ...r, start: ns, offset: g.orig.offset + (ns - g.orig.start), length: g.orig.length - (ns - g.orig.start) }
        }
        const ne = Math.max(g.orig.start + 1, g.sample)
        return { ...r, length: ne - g.orig.start }
      }
      if (g.kind === 'fade' && g.id === r.id) return g.edge === 'in' ? { ...r, fade_in: g.value } : { ...r, fade_out: g.value }
      if (g.kind === 'gain' && g.id === r.id) return { ...r, gain_db: g.value }
      return r
    })
  }, [project, gesture])

  // ─── drawing ──────────────────────────────────────────────────────────────
  useEffect(() => {
    const cv = canvasRef.current
    if (!cv || !project) return
    const dpr = window.devicePixelRatio || 1
    cv.width = Math.floor(size.w * dpr)
    cv.height = Math.floor(size.h * dpr)
    cv.style.width = `${size.w}px`
    cv.style.height = `${size.h}px`
    const ctx = cv.getContext('2d')!
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
    ctx.clearRect(0, 0, size.w, size.h)

    const css = getComputedStyle(document.documentElement)
    const tok = (n: string) => css.getPropertyValue(n).trim()

    // lanes
    for (const { track, y, h } of layout.tracks) {
      const yy = y - view.scrollY
      if (yy + h < 0 || yy > size.h) continue
      const selected = selection.tracks.includes(track.id)
      ctx.fillStyle = selected ? 'rgba(49,67,94,0.35)' : track.kind === 'master' || track.kind === 'bus' || track.kind === 'aux' ? 'rgba(255,255,255,0.015)' : 'transparent'
      ctx.fillRect(0, yy, size.w, h)
      ctx.fillStyle = tok('--hairline')
      ctx.fillRect(0, yy + h - 1, size.w, 1)
    }

    // grid
    const bar = samplesPerBar(sr, project.tempo, project.sig)
    const beat = samplesPerBeat(sr, project.tempo) * (4 / project.sig.den)
    const gridStep = chooseGridStep(view.ruler, pxPerSample, sr, bar, beat)
    const first = Math.floor(view.scrollSample / gridStep.minor) * gridStep.minor
    for (let s = first; toX(s) < size.w; s += gridStep.minor) {
      const x = Math.round(toX(s)) + 0.5
      const major = Math.abs(s / gridStep.major - Math.round(s / gridStep.major)) < 1e-6
      ctx.strokeStyle = major ? tok('--grid-major') : tok('--grid-minor')
      ctx.beginPath()
      ctx.moveTo(x, 0)
      ctx.lineTo(x, size.h)
      ctx.stroke()
    }

    // loop range
    if (status?.loop) {
      const x0 = toX(status.loop_start)
      const x1 = toX(status.loop_end)
      ctx.fillStyle = 'rgba(91,157,255,0.06)'
      ctx.fillRect(x0, 0, x1 - x0, size.h)
    }

    // range selection
    const rangeSel = gesture.kind === 'range' ? { start: Math.min(gesture.startSample, gesture.endSample), end: Math.max(gesture.startSample, gesture.endSample), a: gesture.trackA, b: gesture.trackB } : selection.range ? { start: selection.range.start, end: selection.range.end, a: -1, b: -1 } : null
    if (rangeSel) {
      const tracksIn = rangeSel.a >= 0 ? layout.tracks.filter((t) => t.index >= Math.min(rangeSel.a, rangeSel.b) && t.index <= Math.max(rangeSel.a, rangeSel.b)) : layout.tracks.filter((t) => selection.range!.tracks.includes(t.track.id))
      ctx.fillStyle = 'rgba(91,157,255,0.18)'
      for (const t of tracksIn) ctx.fillRect(toX(rangeSel.start), t.y - view.scrollY, (rangeSel.end - rangeSel.start) * pxPerSample, t.h)
      ctx.strokeStyle = tok('--acc-primary')
      ctx.lineWidth = 1
      ctx.strokeRect(Math.round(toX(rangeSel.start)) + 0.5, 0.5, Math.round((rangeSel.end - rangeSel.start) * pxPerSample), size.h - 1)
    }

    // regions
    const byTrack = new Map<number, { y: number; h: number }>()
    for (const t of layout.tracks) byTrack.set(t.track.id, { y: t.y - view.scrollY, h: t.h })
    const regionsSorted = [...displayRegions].sort((a, b) => Number(selection.regions.includes(a.id)) - Number(selection.regions.includes(b.id)))
    for (const r of regionsSorted) {
      const lane = byTrack.get(r.track)
      if (!lane) continue
      const x0 = toX(r.start)
      const x1 = toX(r.start + r.length)
      if (x1 < 0 || x0 > size.w || lane.y + lane.h < 0 || lane.y > size.h) continue
      const track = project.tracks.find((t) => t.id === r.track)!
      drawRegion(ctx, r, track, x0, x1, lane.y + 1, lane.h - 2, selection.regions.includes(r.id), mode === 'precision', view.automation, tok)
    }

    // recording preview
    if (status?.recording && recordStart !== null) {
      for (const t of layout.tracks) {
        if (!t.track.arm) continue
        const x0 = toX(recordStart)
        const x1 = toX(status.playhead)
        ctx.fillStyle = 'rgba(255,69,58,0.35)'
        ctx.fillRect(x0, t.y - view.scrollY + 1, Math.max(2, x1 - x0), t.h - 2)
        ctx.strokeStyle = tok('--acc-record')
        ctx.strokeRect(Math.round(x0) + 0.5, t.y - view.scrollY + 1.5, Math.max(2, x1 - x0), t.h - 3)
      }
    }

    // pencil preview
    if (gesture.kind === 'pencil') {
      const lane = byTrack.get(gesture.track)
      if (lane) {
        const a = Math.min(gesture.startSample, gesture.endSample)
        const b = Math.max(gesture.startSample, gesture.endSample)
        ctx.fillStyle = 'rgba(232,234,240,0.15)'
        ctx.fillRect(toX(a), lane.y + 1, (b - a) * pxPerSample, lane.h - 2)
        ctx.strokeStyle = tok('--ink-2')
        ctx.setLineDash([3, 3])
        ctx.strokeRect(Math.round(toX(a)) + 0.5, lane.y + 1.5, Math.round((b - a) * pxPerSample), lane.h - 3)
        ctx.setLineDash([])
      }
    }

    // markers
    for (const m of project.markers) {
      const x = Math.round(toX(m.sample)) + 0.5
      if (x < 0 || x > size.w) continue
      ctx.strokeStyle = 'rgba(255,214,10,0.35)'
      ctx.setLineDash([2, 4])
      ctx.beginPath()
      ctx.moveTo(x, 0)
      ctx.lineTo(x, size.h)
      ctx.stroke()
      ctx.setLineDash([])
    }

    // marquee
    if (gesture.kind === 'marquee') {
      const x = Math.min(gesture.x0, gesture.x1)
      const y = Math.min(gesture.y0, gesture.y1)
      ctx.fillStyle = 'rgba(91,157,255,0.12)'
      ctx.fillRect(x, y, Math.abs(gesture.x1 - gesture.x0), Math.abs(gesture.y1 - gesture.y0))
      ctx.strokeStyle = tok('--acc-primary')
      ctx.strokeRect(Math.round(x) + 0.5, Math.round(y) + 0.5, Math.round(Math.abs(gesture.x1 - gesture.x0)), Math.round(Math.abs(gesture.y1 - gesture.y0)))
    }

    // playhead
    if (status) {
      const x = Math.round(toX(status.playhead)) + 0.5
      ctx.strokeStyle = status.recording ? tok('--acc-record') : tok('--acc-primary')
      ctx.lineWidth = 1
      ctx.beginPath()
      ctx.moveTo(x, 0)
      ctx.lineTo(x, size.h)
      ctx.stroke()
    }
  }, [project, status, layout, size, view, selection, displayRegions, gesture, mode, toX, pxPerSample, sr, recordStart])

  // ─── ruler ────────────────────────────────────────────────────────────────
  useEffect(() => {
    const cv = rulerRef.current
    if (!cv || !project) return
    const dpr = window.devicePixelRatio || 1
    const h = 28
    cv.width = Math.floor(size.w * dpr)
    cv.height = Math.floor(h * dpr)
    cv.style.width = `${size.w}px`
    cv.style.height = `${h}px`
    const ctx = cv.getContext('2d')!
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
    ctx.clearRect(0, 0, size.w, h)
    const css = getComputedStyle(document.documentElement)
    const tok = (n: string) => css.getPropertyValue(n).trim()
    const bar = samplesPerBar(sr, project.tempo, project.sig)
    const beat = samplesPerBeat(sr, project.tempo) * (4 / project.sig.den)
    const step = chooseGridStep(view.ruler, pxPerSample, sr, bar, beat)
    ctx.font = `10px ${tok('--font-mono')}`
    ctx.textBaseline = 'top'
    // band 0–11: markers · band 12–22: time labels · bottom: ticks
    ctx.fillStyle = 'rgba(255,255,255,0.02)'
    ctx.fillRect(0, 0, size.w, 11)
    const first = Math.floor(view.scrollSample / step.minor) * step.minor
    for (let s = first; toX(s) < size.w; s += step.minor) {
      const x = Math.round(toX(s)) + 0.5
      const major = Math.abs(s / step.major - Math.round(s / step.major)) < 1e-6
      ctx.strokeStyle = major ? tok('--ink-2') : tok('--ink-3')
      ctx.beginPath()
      ctx.moveTo(x, major ? h - 8 : h - 4)
      ctx.lineTo(x, h)
      ctx.stroke()
      if (major) {
        ctx.fillStyle = tok('--ink-2')
        ctx.fillText(rulerLabel(view.ruler, s, sr, project.tempo, project.sig, step.major), x + 3, 11)
      }
    }
    // markers (top band)
    ctx.font = `9px ${tok('--font-ui')}`
    for (const m of project.markers) {
      const x = Math.round(toX(m.sample))
      if (x < -60 || x > size.w) continue
      ctx.fillStyle = 'rgba(255,214,10,0.9)'
      ctx.beginPath()
      ctx.moveTo(x, 0)
      ctx.lineTo(x + 4, 0)
      ctx.lineTo(x + 4, 7)
      ctx.lineTo(x, 10)
      ctx.closePath()
      ctx.fill()
      ctx.fillStyle = tok('--ink-2')
      ctx.fillText(m.name, x + 7, 1)
    }
    // loop range
    if (status?.loop) {
      ctx.fillStyle = 'rgba(91,157,255,0.5)'
      ctx.fillRect(toX(status.loop_start), h - 4, (status.loop_end - status.loop_start) * pxPerSample, 4)
    }
    // playhead triangle
    if (status) {
      const x = toX(status.playhead)
      ctx.fillStyle = status.recording ? tok('--acc-record') : tok('--acc-primary')
      ctx.beginPath()
      ctx.moveTo(x - 5, h - 10)
      ctx.lineTo(x + 5, h - 10)
      ctx.lineTo(x, h - 1)
      ctx.closePath()
      ctx.fill()
    }
  }, [project, status, size.w, view, mode, toX, pxPerSample, sr])

  // keep header column scrolled with lanes
  useEffect(() => {
    if (headersRef.current) headersRef.current.scrollTop = view.scrollY
  }, [view.scrollY])

  // ─── hit testing ──────────────────────────────────────────────────────────
  const hitRegion = useCallback(
    (x: number, y: number) => {
      if (!project) return null
      const lane = trackAtY(y)
      if (!lane) return null
      const yInLane = y + view.scrollY - lane.y
      const sample = toSample(x)
      const candidates = project.regions.filter((r) => r.track === lane.track.id && r.start <= sample && r.start + r.length >= sample)
      // selected regions on top, later in list on top
      const r = candidates.sort((a, b) => Number(selection.regions.includes(a.id)) - Number(selection.regions.includes(b.id))).pop()
      if (!r) return { lane, region: null as Region | null, zone: 'empty' as const, sample }
      const x0 = toX(r.start)
      const x1 = toX(r.start + r.length)
      const fadeInX = x0 + r.fade_in * pxPerSample
      const fadeOutX = x1 - r.fade_out * pxPerSample
      let zone: 'head' | 'tail' | 'fade-in' | 'fade-out' | 'gain' | 'top' | 'bottom' = yInLane < lane.h * 0.5 ? 'top' : 'bottom'
      if (yInLane < 12 && Math.abs(x - fadeInX) <= HANDLE + 2 && x1 - x0 > 20) zone = 'fade-in'
      else if (yInLane < 12 && Math.abs(x - fadeOutX) <= HANDLE + 2 && x1 - x0 > 20) zone = 'fade-out'
      else if (x - x0 <= HANDLE && x1 - x0 > 12) zone = 'head'
      else if (x1 - x <= HANDLE && x1 - x0 > 12) zone = 'tail'
      else if (r.kind === 'audio' && mode === 'precision' && Math.abs(yInLane - gainLineY(r, lane.h)) <= 4) zone = 'gain'
      return { lane, region: r, zone, sample }
    },
    [project, trackAtY, toSample, toX, pxPerSample, selection.regions, view.scrollY, mode],
  )

  const effectiveTool = (e: React.MouseEvent | MouseEvent): Tool => {
    if (e.metaKey || e.ctrlKey) return 'marquee' // ⌘-drag = marquee (UIW-06 marquee contract)
    return view.tool
  }

  const onMouseMove = (e: React.MouseEvent) => {
    if (gestureRef.current.kind !== 'none') return
    const rect = lanesRef.current!.getBoundingClientRect()
    const hit = hitRegion(e.clientX - rect.left, e.clientY - rect.top)
    const tool = effectiveTool(e)
    if (!hit || !hit.region) {
      setHover({ cursor: tool === 'pencil' ? 'crosshair' : tool === 'marquee' ? 'crosshair' : tool === 'zoom' ? 'zoom-in' : 'default' })
      return
    }
    if (tool === 'scissors') return setHover({ cursor: 'col-resize', tip: 'Split' })
    if (tool === 'eraser') return setHover({ cursor: 'not-allowed', tip: 'Delete' })
    if (tool === 'zoom') return setHover({ cursor: 'zoom-in' })
    switch (hit.zone) {
      case 'head':
      case 'tail':
        return setHover({ cursor: 'ew-resize', tip: e.altKey ? 'Time-stretch trim' : 'Trim' })
      case 'fade-in':
      case 'fade-out':
        return setHover({ cursor: 'ne-resize', tip: 'Fade' })
      case 'gain':
        return setHover({ cursor: 'row-resize', tip: 'Clip gain' })
      case 'top':
        return setHover({ cursor: tool === 'smart' ? 'text' : tool === 'marquee' ? 'crosshair' : 'default', tip: tool === 'smart' ? 'Selector' : undefined })
      default:
        return setHover({ cursor: tool === 'smart' ? 'grab' : tool === 'marquee' ? 'crosshair' : 'default', tip: tool === 'smart' ? 'Grabber' : undefined })
    }
  }

  const onMouseDown = (e: React.MouseEvent) => {
    if (!project || e.button !== 0) return
    lanesRef.current?.focus()
    const rect = lanesRef.current!.getBoundingClientRect()
    const x = e.clientX - rect.left
    const y = e.clientY - rect.top
    const hit = hitRegion(x, y)
    const tool = effectiveTool(e)
    const c = commands()

    let g: Gesture = { kind: 'none' }

    if (tool === 'zoom') {
      const factor = e.altKey ? 0.5 : 2
      const s = toSample(x)
      const npx = clamp(pxPerSample * factor, 0.00002, 4)
      setView({ pxPerSample: npx, scrollSample: Math.max(0, s - x / npx) })
      return
    }
    if (tool === 'marquee') {
      g = { kind: 'marquee', x0: x, y0: y, x1: x, y1: y }
    } else if (tool === 'pencil') {
      if (hit && hit.lane.track.kind === 'instrument') {
        const s = doSnap(hit.sample, true)
        g = { kind: 'pencil', track: hit.lane.track.id, startSample: s, endSample: s }
      }
    } else if (hit?.region && tool === 'scissors') {
      const sample = doSnap(hit.sample)
      void c.send({ op: 'split', region: hit.region.id, sample }).then((r) => {
        const made = (r as { region?: number }).region
        if (made) select({ regions: [made] })
      })
      return
    } else if (hit?.region && tool === 'eraser') {
      void c.send({ op: 'delete', regions: [hit.region.id] })
      return
    } else if (hit?.region) {
      const r = hit.region
      const additive = e.shiftKey
      const wasSelected = selection.regions.includes(r.id)
      if (!wasSelected) select({ regions: additive ? [...selection.regions, r.id] : [r.id], tracks: [r.track], range: null })
      else if (additive) select({ regions: selection.regions.filter((id) => id !== r.id) })
      const ids = wasSelected ? selection.regions : additive ? [...selection.regions, r.id] : [r.id]

      if (hit.zone === 'head' || hit.zone === 'tail') g = { kind: 'trim', id: r.id, edge: hit.zone, orig: r, sample: hit.zone === 'head' ? r.start : r.start + r.length }
      else if (hit.zone === 'fade-in') g = { kind: 'fade', id: r.id, edge: 'in', orig: r, value: r.fade_in }
      else if (hit.zone === 'fade-out') g = { kind: 'fade', id: r.id, edge: 'out', orig: r, value: r.fade_out }
      else if (hit.zone === 'gain') g = { kind: 'gain', id: r.id, orig: r, value: r.gain_db }
      else if (tool === 'smart' && hit.zone === 'top') {
        // Selector zone: time-range selection
        const s = doSnap(hit.sample)
        g = { kind: 'range', startSample: s, endSample: s, trackA: hit.lane.index, trackB: hit.lane.index }
      } else {
        const orig = new Map<number, { start: number; track: number }>()
        for (const id of ids) {
          const rr = project.regions.find((q) => q.id === id)
          if (rr) orig.set(id, { start: rr.start, track: rr.track })
        }
        g = { kind: 'move', ids, anchorSample: hit.sample, anchorTrackIndex: hit.lane.index, orig, copy: e.altKey, dSample: 0, dTrack: 0, moved: false }
      }
    } else if (hit) {
      // empty lane
      if (!e.shiftKey) select({ regions: [], tracks: [hit.lane.track.id], range: null })
      if (tool === 'smart' || tool === 'pointer') {
        const s = doSnap(hit.sample)
        g = { kind: 'range', startSample: s, endSample: s, trackA: hit.lane.index, trackB: hit.lane.index }
      }
    }

    gestureRef.current = g
    setGesture(g)
    if (g.kind === 'none') return

    const startX = e.clientX
    const move = (ev: MouseEvent) => {
      const gx = ev.clientX - rect.left
      const gy = ev.clientY - rect.top
      const cur = gestureRef.current
      let next: Gesture = cur
      if (cur.kind === 'move') {
        const rawD = toSample(gx) - cur.anchorSample
        const anchorOrig = cur.orig.get(cur.ids[0])!
        const proposed = anchorOrig.start + rawD
        const snapped = doSnap(proposed) - anchorOrig.start
        const lane = trackAtY(gy)
        const dTrack = lane ? lane.index - cur.anchorTrackIndex : cur.dTrack
        const dSample = ev.shiftKey ? 0 : snapped
        next = { ...cur, dSample, dTrack: ev.shiftKey || view.tool === 'pointer' && ev.altKey ? dTrack : dTrack, moved: cur.moved || Math.abs(ev.clientX - startX) > 3 || dTrack !== 0 }
      } else if (cur.kind === 'trim') {
        next = { ...cur, sample: doSnap(toSample(gx)) }
      } else if (cur.kind === 'fade') {
        const r = cur.orig
        const v = cur.edge === 'in' ? clamp(Math.round(toSample(gx) - r.start), 0, r.length) : clamp(Math.round(r.start + r.length - toSample(gx)), 0, r.length)
        next = { ...cur, value: v }
      } else if (cur.kind === 'gain') {
        const dy = ev.movementY
        next = { ...cur, value: clamp(Math.round((cur.value - dy * 0.25) * 10) / 10, -60, 24) }
      } else if (cur.kind === 'range') {
        const lane = trackAtY(gy)
        next = { ...cur, endSample: doSnap(toSample(gx)), trackB: lane ? lane.index : cur.trackB }
      } else if (cur.kind === 'marquee') {
        next = { ...cur, x1: gx, y1: gy }
      } else if (cur.kind === 'pencil') {
        next = { ...cur, endSample: doSnap(toSample(gx), true) }
      }
      gestureRef.current = next
      setGesture(next)
    }
    const up = () => {
      window.removeEventListener('mousemove', move)
      window.removeEventListener('mouseup', up)
      const cur = gestureRef.current
      gestureRef.current = { kind: 'none' }
      setGesture({ kind: 'none' })
      finishGesture(cur)
    }
    window.addEventListener('mousemove', move)
    window.addEventListener('mouseup', up)
  }

  const finishGesture = (g: Gesture) => {
    if (!project) return
    const c = commands()
    if (g.kind === 'move' && g.moved) {
      const moves: { region: number; start: number; track?: number }[] = []
      for (const id of g.ids) {
        const o = g.orig.get(id)!
        const r = project.regions.find((q) => q.id === id)!
        const ti = project.tracks.findIndex((t) => t.id === o.track)
        const nt = project.tracks[clamp(ti + g.dTrack, 0, project.tracks.length - 1)]
        const okTrack = nt && nt.kind !== 'master' && nt.kind !== 'vca' && (nt.kind === 'instrument') === (r.kind === 'midi')
        moves.push({ region: id, start: Math.max(0, o.start + g.dSample), track: okTrack ? nt.id : o.track })
      }
      if (g.copy) {
        void c.send({ op: 'duplicate', regions: g.ids }).then((r) => {
          const made = (r as { regions?: number[] }).regions
          if (!made?.length) return
          const remap = made.map((id, i) => ({ ...moves[i], region: id }))
          void c.send({ op: 'move', moves: remap })
          select({ regions: made })
        })
      } else {
        void c.send({ op: 'move', moves })
      }
    } else if (g.kind === 'trim') {
      const r = g.orig
      if (g.edge === 'head') {
        const ns = clamp(g.sample, r.kind === 'audio' ? r.start - r.offset : 0, r.start + r.length - 1)
        if (ns !== r.start) void c.send({ op: 'trim', region: r.id, start: ns })
      } else {
        const ne = Math.max(r.start + 1, g.sample)
        if (ne !== r.start + r.length) void c.send({ op: 'trim', region: r.id, end: ne })
      }
    } else if (g.kind === 'fade') {
      void c.send({ op: 'set_region', region: g.id, ...(g.edge === 'in' ? { fade_in: g.value } : { fade_out: g.value }) })
    } else if (g.kind === 'gain') {
      if (g.value !== g.orig.gain_db) void c.send({ op: 'set_region', region: g.id, gain_db: g.value })
    } else if (g.kind === 'range') {
      const a = Math.min(g.startSample, g.endSample)
      const b = Math.max(g.startSample, g.endSample)
      const lo = Math.min(g.trackA, g.trackB)
      const hi = Math.max(g.trackA, g.trackB)
      const tracks = layout.tracks.filter((t) => t.index >= lo && t.index <= hi).map((t) => t.track.id)
      if (b > a) {
        const inRange = project.regions.filter((r) => tracks.includes(r.track) && r.start < b && r.start + r.length > a).map((r) => r.id)
        select({ range: { start: a, end: b, tracks }, regions: inRange, tracks })
      } else {
        // click: locate playhead (Logic/PT behaviour on empty lane / selector click)
        void c.send({ op: 'locate', sample: a })
        select({ range: null })
      }
    } else if (g.kind === 'marquee') {
      const x0 = Math.min(g.x0, g.x1)
      const x1 = Math.max(g.x0, g.x1)
      const y0 = Math.min(g.y0, g.y1) + view.scrollY
      const y1 = Math.max(g.y0, g.y1) + view.scrollY
      const s0 = toSample(x0)
      const s1 = toSample(x1)
      const tracks = layout.tracks.filter((t) => t.y < y1 && t.y + t.h > y0).map((t) => t.track.id)
      const ids = project.regions.filter((r) => tracks.includes(r.track) && r.start < s1 && r.start + r.length > s0).map((r) => r.id)
      select({ regions: ids, tracks, range: x1 - x0 > 2 ? { start: Math.round(s0), end: Math.round(s1), tracks } : null })
    } else if (g.kind === 'pencil') {
      const a = Math.min(g.startSample, g.endSample)
      const b = Math.max(g.startSample, g.endSample)
      const bar = samplesPerBar(sr, project.tempo, project.sig)
      const length = b > a ? b - a : Math.round(bar)
      void c.send({ op: 'add_midi_region', track: g.track, name: 'MIDI', start: a, length }).then((r) => {
        const made = (r as { region?: number }).region
        if (made) select({ regions: [made] })
      })
    }
  }

  const onDoubleClick = (e: React.MouseEvent) => {
    const rect = lanesRef.current!.getBoundingClientRect()
    const hit = hitRegion(e.clientX - rect.left, e.clientY - rect.top)
    if (hit?.region) {
      if (view.editMode === 'spot') spotRegion(hit.region.id)
      else setView({ drawer: true, drawerTab: hit.region.kind === 'midi' ? 'piano' : 'audio' })
    }
  }

  const onWheel = (e: React.WheelEvent) => {
    if (e.ctrlKey || e.metaKey) {
      e.preventDefault()
      const rect = lanesRef.current!.getBoundingClientRect()
      const x = e.clientX - rect.left
      const s = toSample(x)
      const npx = clamp(pxPerSample * Math.exp(-e.deltaY * 0.01), 0.00002, 4)
      setView({ pxPerSample: npx, scrollSample: Math.max(0, s - x / npx) })
      return
    }
    if (e.shiftKey || Math.abs(e.deltaX) > Math.abs(e.deltaY)) {
      const d = (e.shiftKey ? e.deltaY : e.deltaX) / pxPerSample
      setView({ scrollSample: Math.max(0, view.scrollSample + d) })
    } else {
      setView({ scrollY: clamp(view.scrollY + e.deltaY, 0, Math.max(0, layout.total - size.h + 40)) })
    }
  }

  const onRulerDown = (e: React.MouseEvent) => {
    if (!project) return
    const rect = rulerRef.current!.getBoundingClientRect()
    const x0 = e.clientX - rect.left
    const s0 = doSnap(toSample(x0))
    void commands().send({ op: 'locate', sample: s0 })
    const move = (ev: MouseEvent) => {
      const s1 = doSnap(toSample(ev.clientX - rect.left))
      if (Math.abs(s1 - s0) > 1 / pxPerSample) void commands().send({ op: 'loop', on: true, start: Math.min(s0, s1), end: Math.max(s0, s1) })
    }
    const up = () => {
      window.removeEventListener('mousemove', move)
      window.removeEventListener('mouseup', up)
    }
    window.addEventListener('mousemove', move)
    window.addEventListener('mouseup', up)
  }

  const onRulerDouble = (e: React.MouseEvent) => {
    const rect = rulerRef.current!.getBoundingClientRect()
    const s = doSnap(toSample(e.clientX - rect.left))
    const name = window.prompt('Marker name', `Marker ${(project?.markers.length ?? 0) + 1}`)
    if (name) void commands().send({ op: 'add_marker', name, sample: s })
  }

  const contentEnd = Math.max(project?.content_end ?? 0, view.scrollSample + size.w / pxPerSample) + sr * 30

  const tip = hover.tip
  return (
    <div className="arrange" style={{ ['--header-w' as string]: `${headerWidth}px` }}>
      <div className="corner">
        <select className="select tiny" value={view.ruler} onChange={(e) => setView({ ruler: e.target.value as typeof view.ruler })} title="Ruler format">
          <option value="bars">Bars·Beats</option>
          <option value="minsec">Min:Sec</option>
          <option value="samples">Samples</option>
          <option value="timecode">Timecode</option>
        </select>
        <span className="spacer" />
        <button className="btn tiny" title="Add track" onClick={() => void commands().addTrack('audio')}>
          + Audio
        </button>
        <button className="btn tiny" title="Add instrument track" onClick={() => void commands().addTrack('instrument', 1)}>
          + Inst
        </button>
      </div>
      <div className="ruler" onMouseDown={onRulerDown} onDoubleClick={onRulerDouble} title="Click: locate · drag: cycle range · double-click: add marker">
        <canvas ref={rulerRef} />
      </div>
      <div className="headers" ref={headersRef}>
        <div style={{ height: layout.total + 200 }}>
          {layout.tracks.map(({ track, h, index }) => (
            <TrackHeader key={track.id} track={track} index={index} height={h} wide={mode === 'precision'} />
          ))}
        </div>
      </div>
      <div
        ref={lanesRef}
        className="lanes"
        tabIndex={0}
        style={{ cursor: gesture.kind === 'move' ? 'grabbing' : hover.cursor }}
        onMouseMove={onMouseMove}
        onMouseDown={onMouseDown}
        onDoubleClick={onDoubleClick}
        onWheel={onWheel}
        title={tip}
      >
        <canvas ref={canvasRef} />
        {gesture.kind !== 'none' && <GestureReadout gesture={gesture} sr={sr} project={project} />}
      </div>
      <div
        className="hscroll"
        onScroll={(e) => {
          const el = e.currentTarget
          const s = el.scrollLeft / pxPerSample
          if (Math.abs(s - view.scrollSample) > 0.5) setView({ scrollSample: s })
        }}
        ref={(el) => {
          if (el) {
            const want = view.scrollSample * pxPerSample
            if (Math.abs(el.scrollLeft - want) > 1) el.scrollLeft = want
          }
        }}
      >
        <div style={{ width: contentEnd * pxPerSample }} />
      </div>
    </div>
  )
}

function GestureReadout({ gesture, sr, project }: { gesture: Gesture; sr: number; project: ReturnType<typeof useStore.getState>['project'] }) {
  if (!project) return null
  let text = ''
  const fmt = (s: number) => `${formatBarsBeats(s, sr, project.tempo, project.sig).trim()}  ·  ${formatSamples(s)} smp`
  if (gesture.kind === 'move') text = `Δ ${gesture.dSample >= 0 ? '+' : ''}${formatSamples(gesture.dSample)} smp${gesture.copy ? '  (copy)' : ''}`
  else if (gesture.kind === 'trim') text = `${gesture.edge === 'head' ? 'Start' : 'End'} ${fmt(gesture.sample)}`
  else if (gesture.kind === 'fade') text = `Fade ${gesture.edge} ${formatSamples(gesture.value)} smp (${formatMinSec(gesture.value, sr)})`
  else if (gesture.kind === 'gain') text = `Clip gain ${gesture.value > 0 ? '+' : ''}${gesture.value.toFixed(1)} dB`
  else if (gesture.kind === 'range') {
    const a = Math.min(gesture.startSample, gesture.endSample)
    const b = Math.max(gesture.startSample, gesture.endSample)
    text = `${fmt(a)}  →  ${fmt(b)}   len ${formatSamples(b - a)} smp`
  } else if (gesture.kind === 'pencil') text = `New MIDI region ${formatSamples(Math.abs(gesture.endSample - gesture.startSample))} smp`
  if (!text) return null
  return (
    <div className="toast mono" style={{ bottom: 18 }}>
      {text}
    </div>
  )
}

// ─── helpers ────────────────────────────────────────────────────────────────

function gainLineY(r: Region, laneH: number): number {
  // −60…+24 dB maps to bottom…top of the lower 70% of the lane; 0 dB sits at ~35% from the bottom.
  const t = (r.gain_db + 60) / 84
  return laneH - 4 - t * (laneH * 0.7)
}

function chooseGridStep(ruler: string, pxPerSample: number, sr: number, bar: number, beat: number): { major: number; minor: number } {
  const minPx = 56
  if (ruler === 'bars') {
    const candidates = [beat / 4, beat / 2, beat, bar, bar * 2, bar * 4, bar * 8, bar * 16, bar * 32, bar * 64, bar * 128]
    const major = candidates.find((c) => c * pxPerSample >= minPx) ?? bar * 256
    const minor = major >= bar ? Math.max(beat, major / 4) : major / 2
    return { major, minor: minor * pxPerSample >= 6 ? minor : major }
  }
  if (ruler === 'samples') {
    const pows = [1, 2, 5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000, 50000, 100000, 200000, 500000, 1000000, 2000000, 5000000, 10000000]
    const major = pows.find((c) => c * pxPerSample >= minPx) ?? 1e8
    return { major, minor: major / (String(major)[0] === '2' ? 2 : 5) }
  }
  if (ruler === 'timecode') {
    const f = sr / 30
    const c = [f, f * 2, f * 5, f * 10, f * 15, sr, sr * 2, sr * 5, sr * 10, sr * 30, sr * 60, sr * 300, sr * 600]
    const major = c.find((v) => v * pxPerSample >= minPx) ?? sr * 3600
    return { major, minor: major / 5 }
  }
  const c = [sr / 1000, sr / 500, sr / 200, sr / 100, sr / 50, sr / 20, sr / 10, sr / 5, sr / 2, sr, sr * 2, sr * 5, sr * 10, sr * 30, sr * 60, sr * 300, sr * 600]
  const major = c.find((v) => v * pxPerSample >= minPx) ?? sr * 3600
  return { major, minor: major / 5 }
}

function rulerLabel(ruler: string, s: number, sr: number, tempo: { num: number; den: number }, sig: { num: number; den: number }, step: number): string {
  switch (ruler) {
    case 'bars': {
      const bb = formatBarsBeats(s, sr, tempo, sig).trim().split(/\s+/)
      return step >= samplesPerBar(sr, tempo, sig) ? bb[0] : `${bb[0]}.${bb[1]}`
    }
    case 'samples':
      return formatSamples(s)
    case 'timecode':
      return formatTimecode(s, sr)
    default:
      return formatMinSec(s, sr)
  }
}

function drawRegion(
  ctx: CanvasRenderingContext2D,
  r: Region,
  track: Track,
  x0: number,
  x1: number,
  y: number,
  h: number,
  selected: boolean,
  precision: boolean,
  automation: boolean,
  tok: (n: string) => string,
) {
  const w = Math.max(2, x1 - x0)
  const colorIndex = r.color >= 0 ? r.color : track.color
  const fill = regionFill(colorIndex, r.muted ? 0.18 : 0.5)
  const solid = trackColor(colorIndex)
  const radius = 3
  ctx.save()
  ctx.beginPath()
  roundRect(ctx, x0, y, w, h, radius)
  ctx.clip()
  ctx.fillStyle = r.muted ? 'rgba(255,255,255,0.03)' : fill
  ctx.fillRect(x0, y, w, h)
  // header strip
  const headH = 13
  ctx.fillStyle = r.muted ? 'rgba(255,255,255,0.12)' : solid
  ctx.fillRect(x0, y, w, headH)

  const bodyY = y + headH
  const bodyH = h - headH
  if (r.kind === 'audio' && bodyH > 6) {
    // waveform from engine peaks (min/max pairs)
    const req = peakRequest(r.offset, r.length, w)
    const peaks = peaksFor(r.source, req.from, req.to, req.buckets)
    const mid = bodyY + bodyH / 2
    const amp = Math.pow(10, r.gain_db / 20)
    ctx.fillStyle = r.muted ? 'rgba(232,234,240,0.25)' : 'rgba(232,234,240,0.85)'
    if (peaks && peaks.length >= 2) {
      const n = peaks.length / 2
      const bw = w / n
      ctx.beginPath()
      for (let i = 0; i < n; i++) {
        const mn = clamp(peaks[i * 2] * amp, -1, 1)
        const mx = clamp(peaks[i * 2 + 1] * amp, -1, 1)
        const px = x0 + i * bw
        const top = mid - mx * (bodyH / 2 - 1)
        const bot = mid - mn * (bodyH / 2 - 1)
        ctx.rect(px, Math.min(top, bot), Math.max(1, bw), Math.max(1, Math.abs(bot - top)))
      }
      ctx.fill()
    } else {
      ctx.fillRect(x0, mid - 0.5, w, 1)
    }
    // fades (acc.fade overlays)
    ctx.strokeStyle = tok('--acc-fade')
    ctx.fillStyle = 'rgba(100,210,255,0.14)'
    ctx.lineWidth = 1
    const fi = r.fade_in * (w / Math.max(1, r.length))
    const fo = r.fade_out * (w / Math.max(1, r.length))
    if (fi > 0) {
      ctx.beginPath()
      ctx.moveTo(x0, bodyY + bodyH)
      ctx.quadraticCurveTo(x0 + fi * 0.5, bodyY + bodyH * 0.2, x0 + fi, bodyY)
      ctx.lineTo(x0, bodyY)
      ctx.closePath()
      ctx.fill()
      ctx.beginPath()
      ctx.moveTo(x0, bodyY + bodyH)
      ctx.quadraticCurveTo(x0 + fi * 0.5, bodyY + bodyH * 0.2, x0 + fi, bodyY)
      ctx.stroke()
    }
    if (fo > 0) {
      ctx.beginPath()
      ctx.moveTo(x1, bodyY + bodyH)
      ctx.quadraticCurveTo(x1 - fo * 0.5, bodyY + bodyH * 0.2, x1 - fo, bodyY)
      ctx.lineTo(x1, bodyY)
      ctx.closePath()
      ctx.fill()
      ctx.beginPath()
      ctx.moveTo(x1, bodyY + bodyH)
      ctx.quadraticCurveTo(x1 - fo * 0.5, bodyY + bodyH * 0.2, x1 - fo, bodyY)
      ctx.stroke()
    }
    // fade handles (top corners)
    if (w > 20) {
      ctx.fillStyle = tok('--acc-fade')
      ctx.fillRect(x0 + fi - 3, y + headH + 1, 6, 6)
      ctx.fillRect(x1 - fo - 3, y + headH + 1, 6, 6)
    }
    // clip-gain line (precision)
    if (precision && bodyH > 20) {
      const gy = y + gainLineY(r, h)
      ctx.strokeStyle = 'rgba(100,210,255,0.9)'
      ctx.setLineDash([4, 3])
      ctx.beginPath()
      ctx.moveTo(x0, gy + 0.5)
      ctx.lineTo(x1, gy + 0.5)
      ctx.stroke()
      ctx.setLineDash([])
      if (w > 60) {
        ctx.fillStyle = 'rgba(100,210,255,0.9)'
        ctx.font = `9px ${tok('--font-mono')}`
        ctx.fillText(`${r.gain_db > 0 ? '+' : ''}${r.gain_db.toFixed(1)} dB`, x0 + 4, gy - 10)
      }
    }
  } else if (r.kind === 'midi' && r.notes && bodyH > 6) {
    // MIDI piano-roll thumbnail
    let lo = 127
    let hi = 0
    for (const n of r.notes) {
      lo = Math.min(lo, n[4])
      hi = Math.max(hi, n[4])
    }
    if (hi < lo) {
      lo = 48
      hi = 72
    }
    lo = Math.max(0, lo - 2)
    hi = Math.min(127, hi + 2)
    const span = Math.max(12, hi - lo)
    const nh = Math.max(1, Math.min(4, bodyH / span))
    ctx.fillStyle = r.muted ? 'rgba(232,234,240,0.25)' : 'rgba(232,234,240,0.9)'
    const scale = w / Math.max(1, r.length)
    for (const n of r.notes) {
      const nx = x0 + n[2] * scale
      const nw = Math.max(1.5, n[3] * scale - 0.5)
      const ny = bodyY + bodyH - ((n[4] - lo) / span) * (bodyH - nh) - nh
      ctx.globalAlpha = 0.45 + (n[5] / 127) * 0.55
      ctx.fillRect(nx, ny, nw, nh)
    }
    ctx.globalAlpha = 1
  }

  // automation overlay (Canvas A toggle): volume lane sketch on region
  if (automation && bodyH > 14) {
    ctx.strokeStyle = tok('--acc-autom')
    ctx.lineWidth = 1
    ctx.beginPath()
    const yv = bodyY + bodyH * (1 - track.fader_pos)
    ctx.moveTo(x0, yv + 0.5)
    ctx.lineTo(x1, yv + 0.5)
    ctx.stroke()
  }

  // name
  ctx.fillStyle = r.muted ? tok('--ink-3') : '#0e1014'
  ctx.font = `600 10px ${tok('--font-ui')}`
  ctx.textBaseline = 'middle'
  const label = `${r.name}${r.muted ? ' (muted)' : ''}`
  if (w > 24) ctx.fillText(label, x0 + 4, y + headH / 2 + 0.5, w - 8)
  ctx.restore()

  // outline
  ctx.lineWidth = selected ? 2 : 1
  ctx.strokeStyle = selected ? '#ffffff' : 'rgba(0,0,0,0.5)'
  ctx.beginPath()
  roundRect(ctx, x0 + (selected ? 1 : 0.5), y + (selected ? 1 : 0.5), w - (selected ? 2 : 1), h - (selected ? 2 : 1), radius)
  ctx.stroke()
  // micro-fade ticks at bounds (precision)
  if (precision && r.kind === 'audio') {
    ctx.fillStyle = tok('--acc-fade')
    ctx.fillRect(x0, y + h - 3, 2, 3)
    ctx.fillRect(x1 - 2, y + h - 3, 2, 3)
  }
}

function roundRect(ctx: CanvasRenderingContext2D, x: number, y: number, w: number, h: number, r: number) {
  const rr = Math.min(r, w / 2, h / 2)
  ctx.moveTo(x + rr, y)
  ctx.lineTo(x + w - rr, y)
  ctx.quadraticCurveTo(x + w, y, x + w, y + rr)
  ctx.lineTo(x + w, y + h - rr)
  ctx.quadraticCurveTo(x + w, y + h, x + w - rr, y + h)
  ctx.lineTo(x + rr, y + h)
  ctx.quadraticCurveTo(x, y + h, x, y + h - rr)
  ctx.lineTo(x, y + rr)
  ctx.quadraticCurveTo(x, y, x + rr, y)
  ctx.closePath()
}
