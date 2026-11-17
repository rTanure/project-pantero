import { useCallback, useEffect, useRef } from 'react'
import { log } from '../lib/logger'

const KEY_VECTORS = {
  w: [0, 1],
  s: [0, -1],
  a: [-1, 0],
  d: [1, 0],
}

// Acompanha as teclas WASD pressionadas. Devolve uma função que lê o vetor atual { x, y }
// (x: -1 esquerda a 1 direita; y: -1 ré a 1 frente), sem limitar a soma de teclas combinadas.
export function useKeyboard() {
  const pressed = useRef(new Set())

  useEffect(() => {
    const onDown = (e) => {
      const key = e.key.toLowerCase()
      if (key in KEY_VECTORS && !e.repeat) {
        log.info('KEY', `${key.toUpperCase()} pressionada`)
        pressed.current.add(key)
        e.preventDefault()
      }
    }
    const onUp = (e) => {
      const key = e.key.toLowerCase()
      if (pressed.current.delete(key)) log.info('KEY', `${key.toUpperCase()} solta`)
    }
    const releaseAll = () => {
      if (pressed.current.size) log.warn('KEY', 'janela perdeu o foco, soltando todas as teclas')
      pressed.current.clear()
    }
    window.addEventListener('keydown', onDown)
    window.addEventListener('keyup', onUp)
    window.addEventListener('blur', releaseAll)
    return () => {
      window.removeEventListener('keydown', onDown)
      window.removeEventListener('keyup', onUp)
      window.removeEventListener('blur', releaseAll)
    }
  }, [])

  return useCallback(() => {
    let x = 0
    let y = 0
    for (const key of pressed.current) {
      x += KEY_VECTORS[key][0]
      y += KEY_VECTORS[key][1]
    }
    return { x, y }
  }, [])
}
