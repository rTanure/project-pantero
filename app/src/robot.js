// Endereço do robô. Servido pelo ESP32 (AP "Pantero") o host é o próprio da página;
// em desenvolvimento (npm run dev) use: VITE_ROBOT_HOST=192.168.4.1 npm run dev
export const ROBOT_HOST = import.meta.env.VITE_ROBOT_HOST || window.location.hostname

export const STREAM_URL = `http://${ROBOT_HOST}:81/stream`
export const WS_URL = `ws://${ROBOT_HOST}/ws`

// Intervalo de reenvio do comando (o Uno para o robô se ficar 500 ms sem receber nada)
export const SEND_INTERVAL_MS = 80
