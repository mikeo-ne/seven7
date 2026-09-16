// seven7 UI — Audio (sample) editor for the drawer: high-resolution waveform of the
// selected region's source with region bounds, fades and sample readout under cursor.

import { useEffect, useRef, useState } from 'react'
import type { Region } from '../bridge/types'
import { commands } from '../lib/commands'
import { onPeaksReady, peaksFor } from '../lib/peaks'
import { clamp, formatMinSec, formatSamples } from '../lib/units'
import { useStore } from '../state/store'

export function AudioEditor({ region }: { region: Region }) {
  const project = useStore((s) => s.project)!
  const status = useStore((s) => s.status)
  const source = project.sources.find((s) => s.id === region.source)
  const ref = useRef<HTMLDivElement>(null)
  const canvasRef = useRef<HTMLCanvasElement>(null)
  const [size, setSize] = useState({ w: 600, h: 200 })
  const [win, setWin] = useState({ from: 0, to: 1 })
  const [cursor, setCursor] = useState<number | null>(null)
  const [, bump] = useState(0)
  const sr = project.sample_rate

  useEffect(() => {
    const el = ref.current
    if (!el) return
    const ro = new ResizeObserver(() => setSize({ w: el.clientWidth, h: el.clientHeight }))
    ro.observe(el)
    setSize({ w: el.clientWidth, h: el.clientHeight })
    return () => ro.disconnect()
  }, [])
  useEffect(() => onPeaksReady(() => bump((n) => n + 1)), [])
  useEffect(() => {
    // show the region with 10% context on either side
    const pad = Math.round(region.length * 0.1)
    setWin({ from: Math.max(0, region.offset - pad), to: Math.min(source?.length ?? region.offset + region.length, region.offset + region.length + pad) })
  }, [region.id, region.offset, region.length, source?.length])

  const px = size.w / Math.max(1, win.to - win.from)
  const toX = (s: number) => (s - win.from) * px
  const toS = (x: number) => win.from + x / px

  useEffect(() => {
    const cv = canvasRef.current
    if (!cv) return
    const dpr = window.devicePixelRatio || 1
    cv.width = size.w * dpr
    cv.height = size.h * dpr
    cv.style.width = `${size.w}px`
    cv.style.height = `${size.h}px`
    const ctx = cv.getContext('2d')!
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
    const css = getComputedStyle(document.documentElement)
    const tok = (n: string) => css.getPropertyValue(n).trim()
    ctx.clearRect(0, 0, size.w, size.h)
    ctx.fillStyle = tok('--bg-sunken')
    ctx.fillRect(0, 0, size.w, size.h)

    // region bounds wash
    const rx0 = toX(region.offset)
    const rx1 = toX(region.offset + region.length)
    ctx.fillStyle = 'rgba(91,157,255,0.08)'
    ctx.fillRect(rx0, 0, rx1 - rx0, size.h)

    // center line + dB grid
    const mid = size.h / 2
    ctx.fillStyle = tok('--grid-minor')
    for (const db of [-6, -12, -24]) {
      const a = Math.pow(10, db / 20)
      ctx.fillRect(0, mid - a * (mid - 2), size.w, 1)
      ctx.fillRect(0, mid + a * (mid - 2), size.w, 1)
    }
    ctx.fillStyle = tok('--grid-major')
    ctx.fillRect(0, mid, size.w, 1)

    // waveform
    const buckets = clamp(Math.ceil(size.w), 16, 2048)
    const peaks = peaksFor(region.source, win.from, win.to, buckets)
    if (peaks && peaks.length >= 2) {
      const n = peaks.length / 2
      const bw = size.w / n
      const amp = Math.pow(10, region.gain_db / 20)
      ctx.fillStyle = 'rgba(232,234,240,0.85)'
      ctx.beginPath()
      for (let i = 0; i < n; i++) {
        const s = win.from + ((win.to - win.from) * i) / n
        const inRegion = s >= region.offset && s < region.offset + region.length
        if (!inRegion) continue
        const mn = clamp(peaks[i * 2] * amp, -1, 1)
        const mx = clamp(peaks[i * 2 + 1] * amp, -1, 1)
        const top = mid - mx * (mid - 2)
        const bot = mid - mn * (mid - 2)
        ctx.rect(i * bw, Math.min(top, bot), Math.max(1, bw), Math.max(1, Math.abs(bot - top)))
      }
      ctx.fill()
      ctx.fillStyle = 'rgba(232,234,240,0.25)'
      ctx.beginPath()
      for (let i = 0; i < n; i++) {
        const s = win.from + ((win.to - win.from) * i) / n
        const inRegion = s >= region.offset && s < region.offset + region.length
        if (inRegion) continue
        const mn = clamp(peaks[i * 2], -1, 1)
        const mx = clamp(peaks[i * 2 + 1], -1, 1)
        const top = mid - mx * (mid - 2)
        const bot = mid - mn * (mid - 2)
        ctx.rect(i * bw, Math.min(top, bot), Math.max(1, bw), Math.max(1, Math.abs(bot - top)))
      }
      ctx.fill()
    }
    // fades
    ctx.strokeStyle = tok('--acc-fade')
    ctx.lineWidth = 1.5
    if (region.fade_in > 0) {
      ctx.beginPath()
      ctx.moveTo(rx0, size.h)
      ctx.quadraticCurveTo(rx0 + region.fade_in * px * 0.5, size.h * 0.2, rx0 + region.fade_in * px, 0)
      ctx.stroke()
    }
    if (region.fade_out > 0) {
      ctx.beginPath()
      ctx.moveTo(rx1, size.h)
      ctx.quadraticCurveTo(rx1 - region.fade_out * px * 0.5, size.h * 0.2, rx1 - region.fade_out * px, 0)
      ctx.stroke()
    }
    // bounds
    ctx.strokeStyle = tok('--acc-primary')
    ctx.lineWidth = 1
    for (const x of [rx0, rx1]) {
      ctx.beginPath()
      ctx.moveTo(Math.round(x) + 0.5, 0)
      ctx.lineTo(Math.round(x) + 0.5, size.h)
      ctx.stroke()
    }
    // playhead (source-relative)
    if (status) {
      const rel = status.playhead - region.start + region.offset
      if (rel >= win.from && rel <= win.to) {
        const x = Math.round(toX(rel)) + 0.5
        ctx.strokeStyle = status.recording ? tok('--acc-record') : tok('--acc-primary')
        ctx.beginPath()
        ctx.moveTo(x, 0)
        ctx.lineTo(x, size.h)
        ctx.stroke()
      }
    }
    // cursor
    if (cursor !== null) {
      const x = Math.round(toX(cursor)) + 0.5
      ctx.strokeStyle = 'rgba(255,255,255,0.4)'
      ctx.beginPath()
      ctx.moveTo(x, 0)
      ctx.lineTo(x, size.h)
      ctx.stroke()
    }
  }, [size, win, region, status, cursor, px]) // eslint-disable-line react-hooks/exhaustive-deps

  const onWheel = (e: React.WheelEvent) => {
    e.preventDefault()
    const rect = ref.current!.getBoundingClientRect()
    const x = e.clientX - rect.left
    const s = toS(x)
    if (e.ctrlKey || e.metaKey || !e.shiftKey) {
      const f = Math.exp(e.deltaY * 0.01)
      const len = clamp((win.to - win.from) * f, 64, source?.length ?? region.length * 2)
      const from = clamp(s - (x / size.w) * len, 0, Math.max(0, (source?.length ?? len) - len))
      setWin({ from: Math.round(from), to: Math.round(from + len) })
    } else {
      const d = e.deltaX / px
      const len = win.to - win.from
      const from = clamp(win.from + d, 0, Math.max(0, (source?.length ?? len) - len))
      setWin({ from: Math.round(from), to: Math.round(from + len) })
    }
  }

  const onDouble = (e: React.MouseEvent) => {
    const rect = ref.current!.getBoundingClientRect()
    const s = Math.round(toS(e.clientX - rect.left))
    // Double-click: locate timeline playhead to this source sample
    void commands().send({ op: 'locate', sample: clamp(region.start + (s - region.offset), region.start, region.start + region.length) })
  }

  return (
    <div className="audio-editor">
      <div className="bar">
        <span style={{ color: 'var(--ink-1)' }}>{region.name}</span>
        <span>{source?.name ?? '—'}</span>
        <span>
          {source?.channels ?? 1} ch · {source?.sample_rate ?? sr} Hz · {formatSamples(source?.length ?? 0)} smp
        </span>
        <span style={{ flex: 1 }} />
        <span>
          cursor {cursor !== null ? `${formatSamples(cursor)} smp · ${formatMinSec(cursor, sr)}` : '—'}
        </span>
        <span>
          view {formatSamples(win.from)}–{formatSamples(win.to)} ({(px * 1000).toFixed(2)} px/ksmp)
        </span>
      </div>
      <div
        ref={ref}
        style={{ position: 'relative', overflow: 'hidden' }}
        onWheel={onWheel}
        onMouseMove={(e) => {
          const rect = ref.current!.getBoundingClientRect()
          setCursor(Math.round(toS(e.clientX - rect.left)))
        }}
        onMouseLeave={() => setCursor(null)}
        onDoubleClick={onDouble}
        title="Wheel: zoom · ⇧wheel: scroll · double-click: locate"
      >
        <canvas ref={canvasRef} />
      </div>
    </div>
  )
}
