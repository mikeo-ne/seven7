import { useEffect } from 'react'
import { ControlBar } from './components/ControlBar'
import { SpotDialog } from './components/SpotDialog'
import { StatusBar } from './components/StatusBar'
import { TitleBar } from './components/TitleBar'
import { Toolbar } from './components/Toolbar'
import { Workspace } from './components/Workspace'
import { useKeyboard } from './lib/keys'
import { useStore } from './state/store'

export default function App() {
  const start = useStore((s) => s.start)
  useEffect(() => start(), [start])
  useKeyboard()
  return (
    <div className="frame" style={{ position: 'relative' }}>
      <TitleBar />
      <Toolbar />
      <ControlBar />
      <Workspace />
      <StatusBar />
      <SpotDialog />
    </div>
  )
}
