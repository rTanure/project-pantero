import { useRef, useState } from 'react'
import { log } from '../lib/logger'

const SIZE = 180
const KNOB = 64
const RADIUS = (SIZE - KNOB) / 2

// Joystick analógico via Pointer Events (mouse e toque). onChange(x, y) em -1..1, y+ = frente.
export default function Joystick({ onChange }) {
  const baseRef = useRef(null)
  const activeId = useRef(null)
  const [offset, setOffset] = useState({ x: 0, y: 0 })

  const update = (e) => {
    const rect = baseRef.current.getBoundingClientRect()
    let dx = e.clientX - (rect.left + rect.width / 2)
    let dy = e.clientY - (rect.top + rect.height / 2)
    const dist = Math.hypot(dx, dy)
    if (dist > RADIUS) {
      dx = (dx / dist) * RADIUS
      dy = (dy / dist) * RADIUS
    }
    setOffset({ x: dx, y: dy })
    onChange(dx / RADIUS, -dy / RADIUS)
  }

  const release = () => {
    log.info('JOY', 'solto')
    activeId.current = null
    setOffset({ x: 0, y: 0 })
    onChange(0, 0)
  }

  return (
    <div
      ref={baseRef}
      className="joystick"
      style={{ width: SIZE, height: SIZE }}
      onPointerDown={(e) => {
        if (activeId.current !== null) return
        activeId.current = e.pointerId
        log.info('JOY', `toque iniciado (${e.pointerType})`)
        e.currentTarget.setPointerCapture(e.pointerId)
        update(e)
      }}
      onPointerMove={(e) => {
        if (e.pointerId === activeId.current) update(e)
      }}
      onPointerUp={(e) => e.pointerId === activeId.current && release()}
      onPointerCancel={(e) => e.pointerId === activeId.current && release()}
      onLostPointerCapture={(e) => e.pointerId === activeId.current && release()}
      role="application"
      aria-label="Joystick de controle do robô"
    >
      <div
        className="joystick-knob"
        style={{
          width: KNOB,
          height: KNOB,
          transform: `translate(${offset.x}px, ${offset.y}px)`,
        }}
      />
    </div>
  )
}
