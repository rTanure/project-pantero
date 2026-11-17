// Mensagens de reset enviadas pelo ESP32 pelo WebSocket de controle:
//   "R,<codigo>,<NOME>,<uptime_ms>"  reset do ESP32
//   "U,<codigo>,<NOME>,<uptime_ms>"  reset do Arduino Uno (repassado pelo ESP32)

const BOARDS = { R: { id: 'esp', label: 'ESP32' }, U: { id: 'uno', label: 'Arduino' } }

// Motivos que indicam problema (alimentação fraca, travamento, etc.)
const ABNORMAL = ['BROWNOUT', 'PANIC', 'INT_WDT', 'TASK_WDT', 'WDT', 'WATCHDOG', 'UNKNOWN']

// Reset anormal só é destacado se for recente (a placa acabou de ligar)
const RECENT_MS = 60000
// Tolerância ao comparar o uptime recebido com o esperado
const REBOOT_TOLERANCE_MS = 2000

// Converte o texto em { board, label, code, name, uptimeMs, abnormal } ou null se não for um aviso de reset
export function parseResetMessage(text) {
  const board = typeof text === 'string' ? BOARDS[text[0]] : null
  if (!board || text[1] !== ',') return null
  const [, code, name, uptime] = text.split(',')
  return {
    board: board.id,
    label: board.label,
    code: Number(code),
    name,
    uptimeMs: Number(uptime),
    abnormal: ABNORMAL.includes(name),
  }
}

// A placa reiniciou se o uptime atual é menor que o esperado desde a última mensagem
// (previous = { uptimeMs, at } da mensagem anterior, ou undefined na primeira)
export function detectReboot(previous, uptimeMs, now) {
  if (!previous) return false
  return uptimeMs < previous.uptimeMs + (now - previous.at) - REBOOT_TOLERANCE_MS
}

// Só vale mostrar o aviso se o reset foi anormal e recente, ou se aconteceu com o app aberto
export function shouldAlert(info, rebooted) {
  return info.abnormal && (rebooted || info.uptimeMs < RECENT_MS)
}
