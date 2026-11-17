const KEYS = ['w', 'a', 's', 'd']
const THRESHOLD = 0.3

// Teclas WASD desenhadas na tela; acendem conforme o comando atual (teclado ou joystick).
export default function WasdKeys({ command }) {
  const active = {
    w: command.y > THRESHOLD,
    s: command.y < -THRESHOLD,
    a: command.x < -THRESHOLD,
    d: command.x > THRESHOLD,
  }

  return (
    <div className="wasd" aria-label="Teclas WASD">
      {KEYS.map((k) => (
        <kbd key={k} className={`key-${k} ${active[k] ? 'active' : ''}`}>
          {k.toUpperCase()}
        </kbd>
      ))}
    </div>
  )
}
