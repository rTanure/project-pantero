import { useCallback, useEffect, useState } from 'react'
import { RECONNECT_MS, VIDEO_WS_URL } from '../config'
import { log } from '../lib/logger'

const STATS_MS = 5000

// Recebe quadros JPEG pelo WebSocket de vídeo e desenha no canvas.
// Se um quadro chegar enquanto o anterior ainda está sendo decodificado, o mais antigo é descartado,
// então a imagem mostrada é sempre a mais recente. Reconecta sozinho.
// status: 'connecting' | 'live' | 'down'
export function useVideo(canvasRef) {
  const [status, setStatus] = useState('connecting')
  const [attempt, setAttempt] = useState(0) // mudar este valor força uma nova conexão

  useEffect(() => {
    const canvas = canvasRef.current
    let closed = false
    let retryTimer
    let ws
    let latest = null // quadro mais recente ainda não desenhado
    let decoding = false
    let received = 0
    let drawn = 0

    const drawLatest = async () => {
      if (decoding) return // o laço abaixo pega o quadro novo assim que terminar o atual
      decoding = true
      while (latest) {
        const data = latest
        latest = null
        try {
          const bitmap = await createImageBitmap(new Blob([data], { type: 'image/jpeg' }))
          if (canvas.width !== bitmap.width || canvas.height !== bitmap.height) {
            canvas.width = bitmap.width
            canvas.height = bitmap.height
          }
          canvas.getContext('2d').drawImage(bitmap, 0, 0)
          bitmap.close()
          drawn++
          setStatus('live')
        } catch (err) {
          log.warn('VIDEO', 'quadro invalido descartado', String(err))
        }
      }
      decoding = false
    }

    const connect = () => {
      setStatus('connecting')
      log.info('VIDEO', 'conectando', VIDEO_WS_URL)
      ws = new WebSocket(VIDEO_WS_URL)
      ws.binaryType = 'arraybuffer'
      ws.onopen = () => log.info('VIDEO', 'conectado')
      ws.onmessage = (e) => {
        received++
        latest = e.data
        drawLatest()
      }
      ws.onclose = () => {
        setStatus('down')
        if (!closed) retryTimer = setTimeout(connect, RECONNECT_MS)
      }
      ws.onerror = () => ws.close()
    }

    const statsTimer = setInterval(() => {
      if (received === 0) return
      log.info('VIDEO', `${(drawn / (STATS_MS / 1000)).toFixed(1)} fps desenhados, ${received - drawn} descartados no app`)
      received = 0
      drawn = 0
    }, STATS_MS)

    connect()
    return () => {
      closed = true
      clearTimeout(retryTimer)
      clearInterval(statsTimer)
      ws?.close()
    }
  }, [canvasRef, attempt])

  const reconnect = useCallback(() => setAttempt((n) => n + 1), [])
  return { status, reconnect }
}
