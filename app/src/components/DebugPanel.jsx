import { useEffect, useRef, useState } from 'react'
import { clearLogs, useLogs } from '../lib/logger'

// Painel de logs na tela (útil no celular, onde não há DevTools)
export default function DebugPanel() {
  const logs = useLogs()
  const [open, setOpen] = useState(false)
  const endRef = useRef(null)

  useEffect(() => {
    if (open) endRef.current?.scrollIntoView({ block: 'end' })
  }, [logs, open])

  return (
    <section className="debug">
      <div className="debug-bar">
        <button type="button" onClick={() => setOpen((o) => !o)}>
          {open ? 'Ocultar logs' : `Logs (${logs.length})`}
        </button>
        {open && (
          <>
            <button type="button" onClick={clearLogs}>Limpar</button>
            <button
              type="button"
              onClick={() =>
                navigator.clipboard?.writeText(
                  logs.map((l) => `[${l.time}][${l.tag}] ${l.text}`).join('\n'),
                )
              }
            >
              Copiar
            </button>
          </>
        )}
      </div>
      {open && (
        <pre className="debug-log">
          {logs.map((l) => (
            <div key={l.id} className={`log-${l.level}`}>
              {`${l.time} [${l.tag}] ${l.text}`}
            </div>
          ))}
          <div ref={endRef} />
        </pre>
      )}
    </section>
  )
}
