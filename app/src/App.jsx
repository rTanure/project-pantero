import { useEffect, useState } from 'react'
import Joystick from './Joystick'
import { useRobot } from './useRobot'
import DebugPanel from './DebugPanel'
import { STREAM_URL, ROBOT_HOST, WS_URL } from './robot'
import { log } from './logger'
import './App.css'

// Descobre por que a imagem não carrega: testa se a porta 81 responde (cabeçalhos apenas)
async function diagnoseCamera() {
  const ctrl = new AbortController()
  const timer = setTimeout(() => ctrl.abort(), 4000)
  const started = performance.now()
  try {
    // no-cors: a resposta é opaca, mas se a requisição concluir o servidor está de pé
    await fetch(STREAM_URL, { mode: 'no-cors', signal: ctrl.signal })
    log.warn('CAM', `porta 81 respondeu em ${Math.round(performance.now() - started)} ms, mas a <img> falhou: o ESP32 pode estar enviando um stream invalido`)
  } catch (err) {
    if (err.name === 'AbortError') {
      log.error('CAM', 'diagnostico: porta 81 nao respondeu em 4 s (camera nao iniciou no ESP32 ou outro cliente ja usa o stream)')
    } else {
      log.error('CAM', 'diagnostico: conexao recusada/inacessivel na porta 81. Veja o Serial do ESP32: procure [CAM] ERRO', String(err))
    }
  } finally {
    clearTimeout(timer)
  }
}

export default function App() {
  const { connected, command, setJoystick, resets, dismissReset } = useRobot()
  const [cameraOk, setCameraOk] = useState(true)
  const [streamKey, setStreamKey] = useState(0)

  useEffect(() => {
    log.info('APP', 'iniciado', {
      host: ROBOT_HOST,
      stream: STREAM_URL,
      ws: WS_URL,
      pagina: window.location.href,
    })
    if (window.location.protocol === 'https:') {
      log.error('APP', 'pagina em HTTPS: o navegador bloqueia stream/ws em http (mixed content). Use http://')
    }
  }, [])

  const pressed = (key) => {
    const { x, y } = command
    return { w: y > 0.3, s: y < -0.3, a: x < -0.3, d: x > 0.3 }[key]
  }

  return (
    <main className="app">
      <header>
        <h1>Pantero</h1>
        <span className={`status ${connected ? 'on' : 'off'}`}>
          {connected ? 'Conectado' : 'Desconectado'}
        </span>
      </header>

      {Object.entries(resets).map(
        ([kind, info]) =>
          info && (
            <div className="reset-alert" role="alert" key={kind}>
              <span>
                {info.board} reiniciou: <strong>{info.name}</strong>
                {info.name === 'BROWNOUT' && ' (alimentação fraca)'} — ligado há{' '}
                {(info.uptimeMs / 1000).toFixed(0)} s
              </span>
              <button type="button" onClick={() => dismissReset(kind)}>
                Ok
              </button>
            </div>
          ),
      )}

      <section className="camera">
        {cameraOk ? (
          <img
            key={streamKey}
            src={`${STREAM_URL}?t=${streamKey}`}
            alt="Câmera do robô"
            onLoad={(e) =>
              log.info('CAM', `imagem recebida ${e.currentTarget.naturalWidth}x${e.currentTarget.naturalHeight}`)
            }
            onError={() => {
              log.error('CAM', `falha ao carregar ${STREAM_URL}`)
              setCameraOk(false)
              diagnoseCamera()
            }}
          />
        ) : (
          <div className="camera-error">
            <p>Sem imagem da câmera</p>
            <button
              type="button"
              onClick={() => {
                log.info('CAM', 'tentando de novo')
                setCameraOk(true)
                setStreamKey((k) => k + 1)
              }}
            >
              Tentar de novo
            </button>
          </div>
        )}
      </section>

      <section className="controls">
        <div className="wasd" aria-label="Teclas WASD">
          {['w', 'a', 's', 'd'].map((k) => (
            <kbd key={k} className={`key-${k} ${pressed(k) ? 'active' : ''}`}>
              {k.toUpperCase()}
            </kbd>
          ))}
        </div>
        <Joystick onChange={setJoystick} />
      </section>

      <DebugPanel />
    </main>
  )
}
