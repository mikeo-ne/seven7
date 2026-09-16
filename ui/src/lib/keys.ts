// seven7 UI — keyboard handling (docs/02 §8). One "Hybrid" personality for the shell
// milestone: the transport core is universal; single-key PT-style commands activate
// with Keyboard Focus in Precision mode.

import { useEffect } from 'react'
import { commands } from './commands'
import { useStore, type Tool } from '../state/store'

function isTyping(e: KeyboardEvent): boolean {
  const el = e.target as HTMLElement | null
  if (!el) return false
  const tag = el.tagName
  return tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT' || el.isContentEditable
}

export function useKeyboard() {
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      const s = useStore.getState()
      const c = commands()
      const meta = e.metaKey || e.ctrlKey
      const view = s.views[s.mode]

      // Mode toggle ⌃⌘M / Ctrl+Alt+M — always, even while typing.
      if (e.code === 'KeyM' && ((e.ctrlKey && e.metaKey) || (e.ctrlKey && e.altKey))) {
        e.preventDefault()
        s.toggleMode()
        return
      }
      if (isTyping(e)) return

      // ── Meta chords (both personalities) ──
      if (meta) {
        switch (e.code) {
          case 'KeyZ':
            e.preventDefault()
            void c.send({ op: e.shiftKey ? 'redo' : 'undo' })
            return
          case 'KeyA':
            e.preventDefault()
            c.selectAll()
            return
          case 'KeyD':
            e.preventDefault()
            c.duplicateSelection()
            return
          case 'KeyS':
            e.preventDefault()
            void c.saveProject(e.shiftKey)
            return
          case 'KeyO':
            e.preventDefault()
            void c.openProject()
            return
          case 'KeyN':
            e.preventDefault()
            void c.newProject()
            return
          case 'KeyE':
            e.preventDefault()
            c.splitAtPlayhead()
            return
          case 'KeyT':
            e.preventDefault()
            c.splitAtPlayhead()
            return
          case 'Equal':
          case 'NumpadAdd':
            e.preventDefault()
            s.setView({ pxPerSample: Math.min(4, view.pxPerSample * 1.5) })
            return
          case 'Minus':
          case 'NumpadSubtract':
            e.preventDefault()
            s.setView({ pxPerSample: Math.max(0.00002, view.pxPerSample / 1.5) })
            return
          case 'KeyK':
            if (e.altKey) {
              e.preventDefault()
              s.setView({ kbFocus: !view.kbFocus })
            }
            return
          case 'KeyL':
            e.preventDefault()
            void c.toggleLoop()
            return
        }
        return
      }

      // ── Transport core ──
      switch (e.code) {
        case 'Space':
          e.preventDefault()
          void c.send({ op: 'toggle_play' })
          return
        case 'Enter':
        case 'NumpadEnter':
          e.preventDefault()
          void c.send({ op: 'return_to_zero' })
          return
        case 'Numpad0':
          e.preventDefault()
          void c.send({ op: 'stop' })
          return
        case 'Comma':
          e.preventDefault()
          c.locateBars(-1)
          return
        case 'Period':
          e.preventDefault()
          c.locateBars(1)
          return
        case 'Backspace':
        case 'Delete':
          e.preventDefault()
          c.deleteSelection()
          return
        case 'Escape':
          s.clearSelection()
          s.setSpotRegion(null)
          return
        case 'NumpadAdd':
          e.preventDefault()
          c.nudge(view.nudge)
          return
        case 'NumpadSubtract':
          e.preventDefault()
          c.nudge(-view.nudge)
          return
      }

      // ── Single-key commands ──
      const key = e.key.toLowerCase()
      if (e.altKey && key === 'l') {
        s.setView({ library: !view.library })
        return
      }
      if (e.altKey && e.code === 'Equal') {
        s.setView({ editList: !view.editList })
        return
      }
      const pt = s.mode === 'precision' && view.kbFocus
      switch (key) {
        case 'r':
          if (pt) return s.setView({ pxPerSample: Math.max(0.00002, view.pxPerSample / 1.5) })
          void c.send({ op: 'record', on: !s.status?.recording })
          return
        case 't':
          if (pt) return s.setView({ pxPerSample: Math.min(4, view.pxPerSample * 1.5) })
          {
            const tools: Tool[] = s.mode === 'canvas' ? ['pointer', 'pencil', 'scissors', 'marquee', 'eraser', 'zoom'] : ['smart', 'pointer', 'marquee', 'scissors', 'pencil', 'zoom']
            const i = tools.indexOf(view.tool)
            s.setView({ tool: tools[(i + 1) % tools.length] })
          }
          return
        case 'l':
          void c.toggleLoop()
          return
        case 'k':
          void c.send({ op: 'metronome', on: !s.status?.metronome })
          return
        case 'm':
          if (pt) return c.muteSelection()
          {
            const t = s.selection.tracks[0]
            const tr = s.track(t)
            if (tr) s.sendLive(`${t}:mute`, { op: 'set_track', track: t, param: 'mute', value: !tr.mute })
          }
          return
        case 's':
          {
            const t = s.selection.tracks[0]
            const tr = s.track(t)
            if (tr) s.sendLive(`${t}:solo`, { op: 'set_track', track: t, param: 'solo', value: !tr.solo })
          }
          return
        case 'b':
          if (pt) c.splitAtPlayhead()
          return
        case 'i':
          if (s.mode === 'canvas') s.setView({ inspector: !view.inspector })
          return
        case 'y':
          if (s.mode === 'canvas') s.setView({ drawer: !view.drawer })
          return
        case 'x':
          if (s.mode === 'canvas') s.setView({ drawer: !(view.drawer && view.drawerTab === 'mixer'), drawerTab: 'mixer' })
          return
        case 'a':
          if (s.mode === 'canvas') s.setView({ automation: !view.automation })
          return
        case 'p':
          if (s.mode === 'canvas') s.setView({ drawer: true, drawerTab: 'piano' })
          return
        case 'f1':
          s.setView({ editMode: 'slip' })
          void c.send({ op: 'edit_mode', mode: 'slip' })
          return
        case 'f2':
          s.setView({ editMode: 'shuffle' })
          void c.send({ op: 'edit_mode', mode: 'shuffle' })
          return
        case 'f3':
          s.setView({ editMode: 'spot' })
          void c.send({ op: 'edit_mode', mode: 'spot' })
          return
        case 'f4':
          s.setView({ editMode: 'grid' })
          void c.send({ op: 'edit_mode', mode: 'grid' })
          return
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
          if (pt) {
            // PT-style F-key/number zoom presets → quick zoom levels
            const levels = [0.00005, 0.0002, 0.0008, 0.003, 0.02, 0.2]
            s.setView({ pxPerSample: levels[Number(key) - 1] })
          }
          return
      }
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [])
}
