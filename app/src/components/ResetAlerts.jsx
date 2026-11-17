// Faixas de aviso quando o ESP32 ou o Arduino reiniciam de forma anormal (brownout, watchdog...).
export default function ResetAlerts({ resets, onDismiss }) {
  return Object.entries(resets).map(
    ([board, info]) =>
      info && (
        <div className="reset-alert" role="alert" key={board}>
          <span>
            {info.label} reiniciou: <strong>{info.name}</strong>
            {info.name === 'BROWNOUT' && ' (alimentação fraca)'} — ligado há {(info.uptimeMs / 1000).toFixed(0)} s
          </span>
          <button type="button" onClick={() => onDismiss(board)}>
            Ok
          </button>
        </div>
      ),
  )
}
