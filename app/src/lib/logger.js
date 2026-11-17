import { useSyncExternalStore } from 'react'

const MAX_LINES = 200
let lines = []
const listeners = new Set()

function push(level, tag, message, data) {
  const t = new Date()
  const time = `${t.toLocaleTimeString('pt-BR', { hour12: false })}.${String(t.getMilliseconds()).padStart(3, '0')}`
  const text = data === undefined ? message : `${message} ${JSON.stringify(data)}`
  lines = [...lines.slice(-(MAX_LINES - 1)), { id: t.getTime() + Math.random(), time, level, tag, text }]

  const fn = level === 'error' ? console.error : level === 'warn' ? console.warn : console.log
  fn(`[${time}][${tag}] ${message}`, ...(data === undefined ? [] : [data]))

  listeners.forEach((l) => l())
}

// log.info('WS', 'conectado') / log.error('CAM', 'falhou', { motivo })
export const log = {
  info: (tag, msg, data) => push('info', tag, msg, data),
  warn: (tag, msg, data) => push('warn', tag, msg, data),
  error: (tag, msg, data) => push('error', tag, msg, data),
}

export function clearLogs() {
  lines = []
  listeners.forEach((l) => l())
}

const subscribe = (cb) => {
  listeners.add(cb)
  return () => listeners.delete(cb)
}

export const useLogs = () => useSyncExternalStore(subscribe, () => lines)

// Erros globais também vão para o painel
window.addEventListener('error', (e) => log.error('JS', e.message, { src: e.filename, line: e.lineno }))
window.addEventListener('unhandledrejection', (e) => log.error('JS', 'promise rejeitada', String(e.reason)))
