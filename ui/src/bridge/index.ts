// seven7 UI — bridge selection.
//
// Desktop (JUCE WebBrowserComponent): window.__JUCE__ is injected by the host and
// `s7Command`, `s7State`, `s7Status`, `s7Peaks`, `s7ChooseFile`, `s7AudioSettings`
// are registered native functions (app/src/WebShell.cpp). Events "s7Status",
// "s7StateChanged", "s7Log" and "s7Menu" are pushed by the host so the UI does not poll.
//
// Browser dev preview: HTTP calls to /api/* proxied by Vite to s7bridge.

import { getNativeFunction } from '@juce-framework/webview'
import type { Command, CommandResult, EngineBridge, ProjectState, Status } from './types'

// window.__JUCE__ is declared by @juce-framework/webview (it also installs a placeholder
// in plain browsers, so we detect the real host by the registered native functions).
type JuceWindow = Window & {
  __JUCE__?: {
    initialisationData?: { __juce__functions?: string[]; __juce__platform?: string[] }
    backend?: { addEventListener(id: string, fn: (payload: unknown) => void): unknown }
  }
}
const juceWindow = window as unknown as JuceWindow

export function hostHasNativeFunction(name: string): boolean {
  return !!juceWindow.__JUCE__?.initialisationData?.__juce__functions?.includes(name)
}

class JuceBridge implements EngineBridge {
  readonly kind = 'juce' as const
  private cmd = getNativeFunction('s7Command')
  private state = getNativeFunction('s7State')
  private status = getNativeFunction('s7Status')
  private peaks = getNativeFunction('s7Peaks')
  private choose = getNativeFunction('s7ChooseFile')
  private audioSettings = getNativeFunction('s7AudioSettings')

  async command(cmd: Command): Promise<CommandResult> {
    return JSON.parse((await this.cmd(JSON.stringify(cmd))) as string) as CommandResult
  }
  async getState(): Promise<ProjectState> {
    return JSON.parse((await this.state()) as string) as ProjectState
  }
  async getStatus(): Promise<Status> {
    return JSON.parse((await this.status()) as string) as Status
  }
  async getPeaks(source: number, from: number, to: number, buckets: number): Promise<number[]> {
    const r = JSON.parse((await this.peaks(source, from, to, buckets)) as string) as { peaks: number[] }
    return r.peaks ?? []
  }
  async chooseFile(kind: 'open-project' | 'save-project' | 'import-audio'): Promise<string | null> {
    const r = (await this.choose(kind)) as string
    return r && r.length ? r : null
  }
  async openAudioSettings(): Promise<void> {
    await this.audioSettings()
  }
}

class HttpBridge implements EngineBridge {
  readonly kind = 'http' as const
  private base = ''

  private async json<T>(path: string, init?: RequestInit): Promise<T> {
    const res = await fetch(this.base + path, init)
    if (!res.ok) throw new Error(`${path}: HTTP ${res.status}`)
    return (await res.json()) as T
  }
  command(cmd: Command): Promise<CommandResult> {
    return this.json<CommandResult>('/api/command', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(cmd),
    })
  }
  getState(): Promise<ProjectState> {
    return this.json<ProjectState>('/api/state')
  }
  getStatus(): Promise<Status> {
    return this.json<Status>('/api/status')
  }
  async getPeaks(source: number, from: number, to: number, buckets: number): Promise<number[]> {
    const r = await this.json<{ peaks: number[] }>(`/api/peaks?source=${source}&from=${from}&to=${to}&buckets=${buckets}`)
    return r.peaks ?? []
  }
  renderUrl(start: number, end: number): string {
    return `${this.base}/api/render?start=${start}&end=${end}&t=${Date.now()}`
  }
}

let instance: EngineBridge | null = null

export function bridge(): EngineBridge {
  if (!instance) instance = hostHasNativeFunction('s7Command') ? new JuceBridge() : new HttpBridge()
  return instance
}

/** Subscribe to host push events (desktop). Returns an unsubscribe no-op in the browser. */
export function onHostEvent(id: string, fn: (payload: unknown) => void): () => void {
  const backend = juceWindow.__JUCE__?.backend
  if (!backend || !hostHasNativeFunction('s7Command')) return () => {}
  backend.addEventListener(id, fn)
  return () => {}
}
