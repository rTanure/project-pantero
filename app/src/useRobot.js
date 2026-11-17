import { useCallback, useEffect, useRef, useState } from 'react'
import { SEND_INTERVAL_MS, WS_URL } from './robot'
import { log } from './logger'

const KEY_MAP = {
  w: [0, 1],
  s: [0, -1],
  a: [-1, 0],
  d: [1, 0],
}

// Motivos de reset que indicam problema (alimentação fraca, travamento, etc.)
const ABNORMAL_RESETS = ['BROWNOUT', 'PANIC', 'INT_WDT', 'TASK_WDT', 'WDT', 'WATCHDOG', 'UNKNOWN']

const clamp = (v) => Math.max(-1, Math.min(1, v))

// Gerencia o WebSocket e combina teclado (WASD) + joystick em um único comando {x, y}.
// x: -1 (esquerda) a 1 (direita); y: -1 (ré) a 1 (frente).
export function useRobot() {
  const [connected, setConnected] = useState(false)
  const [command, setCommand] = useState({ x: 0, y: 0 })
  const [resets, setResets] = useState({}) // { esp, uno }: motivo do último reset de cada placa

  const wsRef = useRef(null)
  const keysRef = useRef(new Set())
  const joyRef = useRef({ x: 0, y: 0 })
  const lastSentRef = useRef('')
  const lastUptimeRef = useRef({}) // { esp, uno } -> { uptimeMs, at } da última mensagem de reset

  const compute = useCallback(() => {
    let x = 0
    let y = 0
    for (const k of keysRef.current) {
      x += KEY_MAP[k][0]
      y += KEY_MAP[k][1]
    }
    if (x === 0 && y === 0) {
      x = joyRef.current.x
      y = joyRef.current.y
    }
    return { x: clamp(x), y: clamp(y) }
  }, [])

  // Conexão com reconexão automática
  useEffect(() => {
    let closed = false
    let retry
    let attempt = 0
    const connect = () => {
      attempt++
      log.info('WS', `conectando (tentativa ${attempt})`, WS_URL)
      const ws = new WebSocket(WS_URL)
      wsRef.current = ws
      ws.onopen = () => {
        attempt = 0
        log.info('WS', 'conectado')
        setConnected(true)
      }
      // Mensagens de reset: "R,<codigo>,<NOME>,<uptime_ms>" (ESP32) e "U,..." (Arduino Uno)
      ws.onmessage = (e) => {
        const kind = typeof e.data === 'string' ? { R: 'esp', U: 'uno' }[e.data[0]] : null
        if (!kind || e.data[1] !== ',') {
          log.warn('RX', 'mensagem desconhecida', e.data)
          return
        }
        const board = kind === 'esp' ? 'ESP32' : 'Arduino'
        const [, code, name, uptime] = e.data.split(',')
        const uptimeMs = Number(uptime)
        const now = Date.now()
        const prev = lastUptimeRef.current[kind]
        // se o uptime atual é menor que o esperado desde a última vez, a placa reiniciou
        const rebooted = prev ? uptimeMs < prev.uptimeMs + (now - prev.at) - 2000 : null
        lastUptimeRef.current[kind] = { uptimeMs, at: now }

        const abnormal = ABNORMAL_RESETS.includes(name)
        const info = { board, code: Number(code), name, uptimeMs, abnormal, rebooted, at: now }
        const msg = `${board} ligado ha ${(uptimeMs / 1000).toFixed(1)} s; ultimo reset: ${name} (${code})`
        if (rebooted) log.error(board, `REINICIOU! ${msg}`)
        else if (abnormal) log.warn(board, msg)
        else log.info(board, msg)
        // só destaca se for reset anormal recente ou se reiniciou enquanto o app estava aberto
        setResets((r) => ({ ...r, [kind]: abnormal && (rebooted || uptimeMs < 60000) ? info : null }))
      }
      ws.onclose = (e) => {
        log.warn('WS', `fechado (code=${e.code}, motivo="${e.reason}", limpo=${e.wasClean})`)
        setConnected(false)
        if (!closed) retry = setTimeout(connect, 1000)
      }
      ws.onerror = () => {
        log.error('WS', 'erro na conexao (robo fora do alcance ou rede errada?)')
        ws.close()
      }
    }
    connect()
    return () => {
      closed = true
      clearTimeout(retry)
      wsRef.current?.close()
    }
  }, [])

  // Teclado
  useEffect(() => {
    const onDown = (e) => {
      const k = e.key.toLowerCase()
      if (k in KEY_MAP && !e.repeat) {
        log.info('KEY', `${k.toUpperCase()} pressionada`)
        keysRef.current.add(k)
        e.preventDefault()
      }
    }
    const onUp = (e) => {
      const k = e.key.toLowerCase()
      if (keysRef.current.delete(k)) log.info('KEY', `${k.toUpperCase()} solta`)
    }
    const clear = () => {
      if (keysRef.current.size) log.warn('KEY', 'janela perdeu o foco, soltando todas as teclas')
      keysRef.current.clear()
    }
    window.addEventListener('keydown', onDown)
    window.addEventListener('keyup', onUp)
    window.addEventListener('blur', clear) // perdeu o foco: solta tudo
    return () => {
      window.removeEventListener('keydown', onDown)
      window.removeEventListener('keyup', onUp)
      window.removeEventListener('blur', clear)
    }
  }, [])

  // Laço de envio: reenvia enquanto houver movimento (keep-alive) e envia o 0,0 final uma vez
  useEffect(() => {
    const id = setInterval(() => {
      const cmd = compute()
      setCommand((prev) => (prev.x === cmd.x && prev.y === cmd.y ? prev : cmd))

      const msg = `${Math.round(cmd.x * 100)},${Math.round(cmd.y * 100)}`
      const moving = cmd.x !== 0 || cmd.y !== 0
      if (!moving && lastSentRef.current === msg) return

      const ws = wsRef.current
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
  }, [compute])

  const setJoystick = useCallback((x, y) => {
    joyRef.current = { x, y }
  }, [])

  const dismissReset = useCallback((kind) => setResets((r) => ({ ...r, [kind]: null })), [])

  return { connected, command, setJoystick, resets, dismissReset }
}
