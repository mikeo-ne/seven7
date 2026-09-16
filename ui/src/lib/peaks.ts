// seven7 UI — waveform peak cache (ARC-C12 client side).
// Fetches min/max buckets for a source from the engine and caches by (source, from, to, buckets).

import { bridge } from '../bridge'

const cache = new Map<string, number[]>()
const inflight = new Map<string, Promise<number[]>>()
const listeners = new Set<() => void>()

export function onPeaksReady(fn: () => void): () => void {
  listeners.add(fn)
  return () => listeners.delete(fn)
}

export function invalidatePeaks() {
  cache.clear()
}

/** Returns cached peaks or null; kicks off a fetch and notifies listeners when ready. */
export function peaksFor(source: number, from: number, to: number, buckets: number): number[] | null {
  const key = `${source}:${from}:${to}:${buckets}`
  const hit = cache.get(key)
  if (hit) return hit
  if (!inflight.has(key)) {
    const p = bridge()
      .getPeaks(source, from, to, buckets)
      .then((data) => {
        cache.set(key, data)
        inflight.delete(key)
        if (cache.size > 400) {
          // crude LRU: drop oldest quarter
          const keys = [...cache.keys()].slice(0, 100)
          for (const k of keys) cache.delete(k)
        }
        for (const l of listeners) l()
        return data
      })
      .catch(() => {
        inflight.delete(key)
        return []
      })
    inflight.set(key, p)
  }
  return null
}

/** Quantize a region's visible window to stable bucket grids so cache hits survive scrolling. */
export function peakRequest(offset: number, length: number, pxWidth: number): { from: number; to: number; buckets: number } {
  const buckets = Math.max(8, Math.min(2048, Math.ceil(pxWidth / 2)))
  return { from: offset, to: offset + length, buckets }
}
