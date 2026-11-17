// Endereço do robô. Servido pelo ESP32 (AP "Pantero") o host é o próprio da página;
// em desenvolvimento (npm run dev) use: VITE_ROBOT_HOST=192.168.4.1 npm run dev
export const ROBOT_HOST = import.meta.env.VITE_ROBOT_HOST || window.location.hostname

export const CONTROL_WS_URL = `ws://${ROBOT_HOST}/ws`   // comandos do app e avisos de reset
export const VIDEO_WS_URL = `ws://${ROBOT_HOST}/wsv`    // quadros JPEG da câmera

// Intervalo de reenvio do comando (o Uno para o robô se ficar 500 ms sem receber nada)
export const SEND_INTERVAL_MS = 80

export const RECONNECT_MS = 1000
