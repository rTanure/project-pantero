import { useEffect } from 'react'
import DebugPanel from './components/DebugPanel'
import Joystick from './components/Joystick'
import ResetAlerts from './components/ResetAlerts'
import VideoView from './components/VideoView'
import WasdKeys from './components/WasdKeys'
import { CONTROL_WS_URL, ROBOT_HOST, VIDEO_WS_URL } from './config'
import { useRobot } from './hooks/useRobot'
import { log } from './lib/logger'
import './App.css'

export default function App() {
  const { connected, command, setJoystick, resets, dismissReset } = useRobot()

  useEffect(() => {
    log.info('APP', 'iniciado', {
      host: ROBOT_HOST,
      controle: CONTROL_WS_URL,
      video: VIDEO_WS_URL,
      pagina: window.location.href,
    })
    if (window.location.protocol === 'https:') {
      log.error('APP', 'pagina em HTTPS: o navegador bloqueia ws:// (mixed content). Use http://')
    }
  }, [])

  return (
    <main className="app">
      <header>
        <h1>Pantero</h1>
        <span className={`status ${connected ? 'on' : 'off'}`}>
          {connected ? 'Conectado' : 'Desconectado'}
        </span>
      </header>

      <ResetAlerts resets={resets} onDismiss={dismissReset} />
      <VideoView />

      <section className="controls">
        <WasdKeys command={command} />
        <Joystick onChange={setJoystick} />
      </section>

      <DebugPanel />
    </main>
  )
}
