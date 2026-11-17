import { useCallback, useEffect, useRef, useState } from 'react'
import { CONTROL_WS_URL, RECONNECT_MS, SEND_INTERVAL_MS } from '../config'
import { log } from '../lib/logger'
import { detectReboot, parseResetMessage, shouldAlert } from '../lib/resets'
import { useKeyboard } from './useKeyboard'

const clamp = (v) => Math.max(-1, Math.min(1, v))

// Controle do robô: WebSocket com reconexão, teclado (WASD) + joystick combinados em um comando
// { x, y } (-1..1), reenvio periódico e avisos de reset do ESP32 e do Arduino.
export function useRobot() {
  const readKeyboard = useKeyboard()
  const [connected, setConnected] = useState(false)
  const [command, setCommand] = useState({ x: 0, y: 0 })
  const [resets, setResets] = useState({}) // { esp, uno }: aviso de reset anormal de cada placa

  const socketRef = useRef(null)
  const joystickRef = useRef({ x: 0, y: 0 })
  const lastSentRef = useRef('')
  const lastUptimeRef = useRef({}) // { esp, uno } -> { uptimeMs, at } da última mensagem de reset

  // Teclado tem prioridade sobre o joystick
  const readCommand = useCallback(() => {
    const keys = readKeyboard()
    const source = keys.x !== 0 || keys.y !== 0 ? keys : joystickRef.current
    return { x: clamp(source.x), y: clamp(source.y) }
  }, [readKeyboard])

  const handleResetMessage = useCallback((text) => {
    const info = parseResetMessage(text)
    if (!info) {
      log.warn('RX', 'mensagem desconhecida', text)
      return
    }
    const now = Date.now()
    const rebooted = detectReboot(lastUptimeRef.current[info.board], info.uptimeMs, now)
    lastUptimeRef.current[info.board] = { uptimeMs: info.uptimeMs, at: now }

    const summary = `${info.label} ligado ha ${(info.uptimeMs / 1000).toFixed(1)} s; ultimo reset: ${info.name} (${info.code})`
    if (rebooted) log.error(info.label, `REINICIOU! ${summary}`)
    else if (info.abnormal) log.warn(info.label, summary)
    else log.info(info.label, summary)

    setResets((r) => ({ ...r, [info.board]: shouldAlert(info, rebooted) ? info : null }))
  }, [])

  // Conexão com reconexão automática
  useEffect(() => {
    let closed = false
    let retryTimer
    let attempt = 0

    const connect = () => {
      attempt++
      log.info('WS', `conectando (tentativa ${attempt})`, CONTROL_WS_URL)
      const ws = new WebSocket(CONTROL_WS_URL)
      socketRef.current = ws
      ws.onopen = () => {
        attempt = 0
        log.info('WS', 'conectado')
        setConnected(true)
      }
      ws.onmessage = (e) => handleResetMessage(e.data)
      ws.onclose = (e) => {
        log.warn('WS', `fechado (code=${e.code}, motivo="${e.reason}", limpo=${e.wasClean})`)
        setConnected(false)
        if (!closed) retryTimer = setTimeout(connect, RECONNECT_MS)
      }
      ws.onerror = () => {
        log.error('WS', 'erro na conexao (robo fora do alcance ou rede errada?)')
        ws.close()
      }
    }

    connect()
    return () => {
      closed = true
      clearTimeout(retryTimer)
      socketRef.current?.close()
    }
  }, [handleResetMessage])

  // Laço de envio: reenvia enquanto houver movimento (keep-alive) e envia o 0,0 final uma vez
  useEffect(() => {
    const id = setInterval(() => {
      const cmd = readCommand()
      setCommand((prev) => (prev.x === cmd.x && prev.y === cmd.y ? prev : cmd))

      const msg = `${Math.round(cmd.x * 100)},${Math.round(cmd.y * 100)}`
      const moving = cmd.x !== 0 || cmd.y !== 0
      if (!moving && lastSentRef.current === msg) return

      const ws = socketRef.current
      if (ws?.readyState === WebSocket.OPEN) {
        ws.send(msg)
        // loga só quando o comando muda (o reenvio keep-alive é a cada 80 ms)
        if (msg !== lastSentRef.current) log.info('TX', `x,y = ${msg}`)
        lastSentRef.current = msg
      } else if (moving && msg !== lastSentRef.current) {
        log.warn('TX', `comando ${msg} descartado: WebSocket nao esta aberto (estado ${ws?.readyState})`)
        lastSentRef.current = msg
      }
    }, SEND_INTERVAL_MS)
    return () => clearInterval(id)
  }, [readCommand])

  const setJoystick = useCallback((x, y) => {
    joystickRef.current = { x, y }
  }, [])

  const dismissReset = useCallback((board) => setResets((r) => ({ ...r, [board]: null })), [])

  return { connected, command, setJoystick, resets, dismissReset }
}
