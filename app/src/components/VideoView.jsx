import { useRef } from 'react'
import { useVideo } from '../hooks/useVideo'

// Imagem da câmera: quadros JPEG recebidos por WebSocket e desenhados num canvas.
export default function VideoView() {
  const canvasRef = useRef(null)
  const { status, reconnect } = useVideo(canvasRef)
  const live = status === 'live'

  return (
    <section className="camera">
      <canvas ref={canvasRef} className={live ? '' : 'hidden'} aria-label="Câmera do robô" />
      {!live && (
        <div className="camera-status">
          <p>{status === 'connecting' ? 'Conectando ao vídeo…' : 'Sem imagem da câmera'}</p>
          {status === 'down' && (
            <button type="button" onClick={reconnect}>
              Tentar de novo
            </button>
          )}
        </div>
      )}
    </section>
  )
}
