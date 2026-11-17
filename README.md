# Pantero

Robô controlado remotamente por uma aplicação React servida pelo próprio ESP32. O ESP32 cria o Wi-Fi
**Pantero** (senha `12345678`), serve o app, transmite o vídeo da câmera e repassa os comandos ao
Arduino Uno, que aciona os motores e pede 9 V ao carregador Quick Charge.

```
 celular/PC ──Wi-Fi──► ESP32 ──UART──► Arduino Uno ──► ponte H ──► motores
   (app React)          │  câmera OV2640                └─► Quick Charge 3.0 (9 V)
                        └─ http://192.168.4.1
```

## Estrutura

| Pasta | Conteúdo |
|---|---|
| `src/esp32/` | Firmware do ESP32, em módulos (ver abaixo) |
| `src/uno/` | Firmware do Arduino Uno, em módulos |
| `app/` | App React (Vite). O build vai para `data/`, que é gravada no LittleFS do ESP32 |
| `include/QC3Control/` | Biblioteca de Quick Charge (só `src/`; usada apenas para `begin()` e `set9V()`) |
| `tools/bench/` | Testes de vídeo/Wi-Fi: cliente de medição, resultados e relatório em PDF |

**ESP32** (`src/esp32/`): `main.cpp` só orquestra a partida; `config.h` reúne pinos e ajustes;
`wifi_ap` (ponto de acesso), `web_server` (serve o app), `control_ws` (`/ws`, comandos),
`video_ws` (`/wsv`, vídeo), `camera` (OV2640), `robot_link` (UART com o Uno), `display` (OLED de status),
`reset_info` e `log.h`.

**Arduino Uno** (`src/uno/`): `main.cpp`, `config.h`, `motors` (ponte H, mistura tanque,
parada automática), `quick_charge` (9 V), `esp_link` (UART com o ESP32), `reset_info`, `log.h`.

## Pinos

| ESP32 | Uso | | Arduino Uno | Uso |
|---|---|---|---|---|
| 14 / 13 | UART RX / TX com o Uno | | 12 / 13 | UART RX / TX com o ESP32 |
| 32 / 33 | I2C do display OLED SSD1306 (SDA / SCL) | | A0 / A1 | TCRT5000 esquerdo / direito |
| 4, 5, 18, 19, 21–27, 34–36, 39 | Câmera (ESP-WROVER-KIT) | | A3 / A2 | Quick Charge D+ / D− |
| | | | 6, 9, 10, 11 | Ponte H (IN1 a IN4) |

> O TX do Uno é 5 V e o RX do ESP32 é 3,3 V: use divisor de tensão ou conversor de nível.

## Protocolos

- **`/ws` (controle):** o app envia `x,y` (inteiros de −100 a 100). O ESP32 envia `R,<codigo>,<NOME>,<uptime>`
  (reset do ESP32) e `U,...` (reset do Uno), uma linha por aviso.
- **`/wsv` (vídeo):** cada mensagem binária é um quadro JPEG (320×240). Sem buffer: a fila é de 1 mensagem,
  e um quadro que chega com o anterior ainda pendente é descartado. Captura e transmissão se alternam (1 buffer de câmera,
  retido até o quadro sair), para a câmera não trabalhar durante o envio.
- **UART ESP32 → Uno:** `M <x> <y>`, `S` (parar), `?` (pedir motivo do reset). **Uno → ESP32:** `R,<mcusr>,<NOME>,<uptime>`.
  Sem comando por 500 ms, o Uno para os motores.

## Display OLED

O painel SSD1306 (128×64) está montado girado 90° no sentido horário, então o desenho é girado (`OLED_ROTATION`
em `src/esp32/config.h`, padrão 3; use 1 se aparecer de cabeça para baixo). A tela é em retrato (10 colunas × 16 linhas)
e mostra: Wi-Fi (dispositivos), app conectado, vídeo (fps), Uno respondendo, último comando X/Y, potência de TX,
canal, memória livre, uptime e o motivo do último reset do ESP32 (`E`) e do Uno (`U`).

## Comandos

```bash
cd app && npm install && npm run deploy   # gera o app em data/ e grava no LittleFS do ESP32
pio run -e esp32 -t upload                # firmware do ESP32
pio run -e uno -t upload                  # firmware do Uno
pio device monitor -e esp32               # logs ([TAG] por módulo); idem para -e uno
```

Desenvolvimento do app contra o robô: `cd app && VITE_ROBOT_HOST=192.168.4.1 npm run dev`.

## Ajustes

Em `platformio.ini` (`build_flags`) ou nos `config.h`: `POWER_SETTLE_MS` e `POWER_STEP_MS` (partida
escalonada, para fontes externas que levam ~1 s para estabilizar) e `MAX_SPEED_PERCENT` (limite de
velocidade dos motores, padrão 50%).

## Desempenho do vídeo

Os testes em `tools/bench/` mostram que a câmera ligada eleva a latência do Wi-Fi do ESP32 mesmo ociosa
(ping de 4–8 ms para 15–60 ms). Detalhes e recomendações em `tools/bench/results/relatorio-video-wifi.pdf`.
