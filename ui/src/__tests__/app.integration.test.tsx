// Mounts the whole seven7 UI against a live s7bridge (real C++ engine).
// Run with:  S7_BRIDGE=http://127.0.0.1:8787 npx vitest run
import { act, cleanup, fireEvent, render, screen, waitFor } from '@testing-library/react'
import { afterAll, beforeAll, describe, expect, it } from 'vitest'
import App from '../App'
import { useStore } from '../state/store'

const BRIDGE = (globalThis as unknown as { process?: { env: Record<string, string | undefined> } }).process?.env.S7_BRIDGE
const runIf = BRIDGE ? describe : describe.skip

// happy-dom lacks canvas + ResizeObserver; provide minimal stand-ins so the arrange view mounts.
beforeAll(() => {
  ;(globalThis as unknown as { ResizeObserver: unknown }).ResizeObserver = class {
    observe() {}
    disconnect() {}
    unobserve() {}
  }
  const proto = (globalThis as unknown as { HTMLCanvasElement: { prototype: { getContext: unknown } } }).HTMLCanvasElement.prototype
  proto.getContext = () =>
    new Proxy(
      {},
      {
        get: (_t, prop) => {
          if (prop === 'measureText') return () => ({ width: 10 })
          return () => {}
        },
        set: () => true,
      },
    )
  // route fetch('/api/…') to the bridge
  const realFetch = globalThis.fetch
  globalThis.fetch = ((input: RequestInfo | URL, init?: RequestInit) => {
    const url = typeof input === 'string' && input.startsWith('/') ? BRIDGE + input : input
    return realFetch(url as RequestInfo, init)
  }) as typeof fetch
  ;(globalThis as unknown as { requestAnimationFrame: unknown }).requestAnimationFrame = (cb: FrameRequestCallback) => setTimeout(() => cb(performance.now()), 4)
})
afterAll(() => cleanup())

runIf('seven7 shell against the live engine', () => {
  it('boots, loads the demo project and shows both workspaces', async () => {
    render(<App />)
    await waitFor(() => expect(useStore.getState().project?.name).toBe('Midnight City'), { timeout: 10000 })
    const p = useStore.getState().project!
    expect(p.tracks.map((t) => t.name)).toContain('Vox Ld')
    // Track headers rendered (Canvas)
    await screen.findAllByText('Prism Keys')
    // Toggle to Precision via the segmented control
    fireEvent.click(screen.getByRole('tab', { name: 'Precision' }))
    await waitFor(() => expect(useStore.getState().mode).toBe('precision'))
    // Precision shows the edit-mode cluster + mixer strips
    await screen.findByText('Shfl')
    expect(await screen.findByText('MIX')).toBeTruthy()
    // Toggle back with ⌃⌘M
    fireEvent.keyDown(window, { code: 'KeyM', key: 'm', ctrlKey: true, metaKey: true })
    await waitFor(() => expect(useStore.getState().mode).toBe('canvas'))
  })

  it('transport round-trips through the engine', async () => {
    fireEvent.keyDown(window, { code: 'Space', key: ' ' })
    await waitFor(() => expect(useStore.getState().status?.playing).toBe(true), { timeout: 5000 })
    const ph0 = useStore.getState().status!.playhead
    await new Promise((r) => setTimeout(r, 300))
    expect(useStore.getState().status!.playhead).toBeGreaterThan(ph0)
    fireEvent.keyDown(window, { code: 'Space', key: ' ' })
    await waitFor(() => expect(useStore.getState().status?.playing).toBe(false), { timeout: 5000 })
    await act(async () => {
      await useStore.getState().send({ op: 'return_to_zero' })
    })
    await waitFor(() => expect(useStore.getState().status?.playhead).toBe(0))
  })

  it('edits: split at playhead, nudge, undo — all sample-exact', async () => {
    const s = useStore.getState()
    const region = s.project!.regions.find((r) => r.name === 'Bass Line')!
    await act(async () => {
      await s.send({ op: 'locate', sample: 100001 })
    })
    await waitFor(() => expect(useStore.getState().status?.playhead).toBe(100001))
    act(() => s.select({ regions: [region.id] }))
    fireEvent.keyDown(window, { code: 'KeyE', key: 'e', metaKey: true })
    await waitFor(() => expect(useStore.getState().project!.regions.length).toBe(s.project!.regions.length + 1), { timeout: 5000 })
    const right = useStore.getState().project!.regions.find((r) => r.start === 100001)!
    expect(right).toBeTruthy()
    act(() => useStore.getState().select({ regions: [right.id] }))
    fireEvent.keyDown(window, { code: 'NumpadAdd', key: '+' })
    await waitFor(() => expect(useStore.getState().project!.regions.find((r) => r.id === right.id)!.start).toBe(100002))
    fireEvent.keyDown(window, { code: 'KeyZ', key: 'z', metaKey: true })
    await waitFor(() => expect(useStore.getState().project!.regions.find((r) => r.id === right.id)!.start).toBe(100001))
    fireEvent.keyDown(window, { code: 'KeyZ', key: 'z', metaKey: true })
    await waitFor(() => expect(useStore.getState().project!.regions.find((r) => r.id === right.id)).toBeUndefined())
  })

  it('mixer: fader moves reach the engine without creating undo steps', async () => {
    const s = useStore.getState()
    const keys = s.project!.tracks.find((t) => t.name === 'Prism Keys')!
    const undoLabel = s.project!.undo_label
    act(() => s.sendLive(`${keys.id}:fader`, { op: 'set_track', track: keys.id, param: 'fader', value: 0.5 }))
    await new Promise((r) => setTimeout(r, 150))
    const res = await fetch('/api/state').then((r) => r.json())
    expect(res.tracks.find((t: { id: number }) => t.id === keys.id).fader_pos).toBe(0.5)
    expect(res.undo_label).toBe(undoLabel)
    act(() => s.sendLive(`${keys.id}:fader`, { op: 'set_track', track: keys.id, param: 'fader', value: 0.68 }))
    await new Promise((r) => setTimeout(r, 100))
  })
})
