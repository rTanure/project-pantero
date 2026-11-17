"""Configuração do programa do PC. O PC se conecta ao Wi-Fi "Pantero" (senha 12345678) do ESP32."""

ROBOT_IP = "192.168.4.1"   # endereço do ESP32 no próprio Wi-Fi dele
CONTROL_PORT = 4210        # porta UDP do ESP32: recebe "olá" e comandos
VIDEO_PORT = 4211          # porta UDP local do PC: recebe o vídeo

SEND_INTERVAL_S = 0.08     # reenvio do comando (o Uno para o robô se ficar 500 ms sem receber nada)
HELLO_INTERVAL_S = 1.0     # "olá" de keep-alive (o ESP32 dá o PC como perdido após 3 s)
LINK_TIMEOUT_S = 3.0       # sem resposta do ESP32 por este tempo: mostra "desconectado"

VIDEO_SCALE = 2            # tamanho da imagem na tela (o vídeo é 320x240)
VIDEO_STALE_S = 1.5        # sem quadro novo por este tempo: mostra "sem imagem"
