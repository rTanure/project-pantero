# Pantero

Robô controlado por um **programa Python no PC**. O ESP32 cria o Wi-Fi **Pantero** (senha `12345678`); o PC se
conecta a ele e fala **UDP**: recebe o vídeo da câmera e envia os comandos de movimento. O ESP32 repassa os comandos
ao Arduino Uno, que aciona os motores e pede 9 V ao carregador Quick Charge. O ESP32 não serve nenhuma página.

```
 PC (pc/main.py) ──Wi-Fi Pantero──► ESP32 ──UART──► Arduino Uno ──► ponte H ──► motores
   UDP: comandos  ──►                │  câmera OV2640                └─► Quick Charge 3.0 (9 V)
   UDP: vídeo     ◄──                └─ 192.168.4.1
```

## Como usar

1. Grave os firmwares (abaixo) e ligue o robô.
2. Conecte o PC ao Wi-Fi **Pantero**.
3. No PC: `pip install -r pc/requirements.txt` (só o Pillow) e `python3 pc/main.py`.

Controle: teclas **W A S D** (ou setas) e o **joystick** na tela (mouse). O teclado tem prioridade. A janela mostra o
vídeo, o estado da conexão, o comando enviado, avisos de reset do ESP32/Arduino e os logs. Ao fechar, o robô é parado.
Se o firewall do PC bloquear a entrada UDP, libere a porta `4211`.

## Estrutura

| Pasta | Conteúdo |
|---|---|
| `src/esp32/` | Firmware do ESP32, em módulos (ver abaixo) |
| `src/uno/` | Firmware do Arduino Uno, em módulos |
| `pc/` | Programa Python do PC: interface Tk, enlace UDP, receptor de vídeo, simulador do robô e testes |
| `include/QC3Control/` | Biblioteca de Quick Charge (só `src/`; usada apenas para `begin()` e `set9V()`) |
| `tools/bench/` | Testes de vídeo/Wi-Fi: cliente de medição, resultados e relatório em PDF |

**ESP32** (`src/esp32/`): `main.cpp` só orquestra a partida; `config.h` reúne pinos e ajustes; `wifi_ap` (ponto de
acesso), `udp_link` (comandos e avisos), `video_udp` (vídeo), `camera` (OV2640), `robot_link` (UART com o Uno),
`display` (OLED de status), `reset_info` e `log.h`.

**Arduino Uno** (`src/uno/`): `main.cpp`, `config.h`, `motors` (ponte H, mistura tanque, parada automática),
`quick_charge` (9 V), `esp_link` (UART com o ESP32), `reset_info`, `log.h`.

**PC** (`pc/pantero/`): `gui` (janela), `widgets` (joystick e teclas), `link` (UDP de controle), `video` (UDP de vídeo e
remontagem dos quadros), `resets`, `log`, `config`.

## Pinos

| ESP32 | Uso | | Arduino Uno | Uso |
|---|---|---|---|---|
| 14 / 13 | UART RX / TX com o Uno | | 12 / 13 | UART RX / TX com o ESP32 |
| 32 / 33 | I2C do display OLED SSD1306 (SDA / SCL) | | A0 / A1 | TCRT5000 esquerdo / direito |
| 4, 5, 18, 19, 21–27, 34–36, 39 | Câmera (ESP-WROVER-KIT) | | A3 / A2 | Quick Charge D+ / D− |
| | | | 6, 9, 10, 11 | Ponte H (IN1 a IN4) |

> O TX do Uno é 5 V e o RX do ESP32 é 3,3 V: use divisor de tensão ou conversor de nível.

## Protocolo UDP (PC ↔ ESP32)

Sem confiabilidade de propósito: o que se perde, se perde (menos latência).

- **PC → ESP32, porta 4210:** `H,<porta>,<ajustes>` a cada ~1 s (olá/keep-alive: registra o PC, a porta onde ele recebe
  o vídeo e os ajustes `q`, `fs`, `fec`, `pace`, `cam`, `rate`, reaplicados a cada olá); `x,y` (inteiros de −100 a 100)
  a cada 80 ms enquanto há movimento, mais o `0,0` final; e `P,<n>` (ping do controle, devolvido na hora).
- **ESP32 → PC:** em resposta ao olá, `R,<codigo>,<NOME>,<uptime_ms>` (reset do ESP32), `U,...` (reset do Uno) e
  `S,<fps>,<q>,<fs>,<fec>,<pace>,<cam>,<rate>,<perdidos>` (estado do vídeo e do rádio).
- **Vídeo, ESP32 → porta do PC (padrão 4211):** cada quadro JPEG (320×240) é dividido em pacotes de até 1.400 bytes,
  com 8 bytes de cabeçalho (id do quadro de 16 bits, índice do pedaço, total de pedaços e o instante da captura em ms,
  que o PC usa para medir o atraso). Com FEC ligado há um pacote de paridade (XOR) por quadro, que recupera um pedaço
  perdido. O PC remonta o quadro e **descarta o que ficar incompleto**. Há um só PC por vez.
- **Segurança:** sem pacote do PC por 3 s, o ESP32 para o robô e o vídeo. O Uno para os motores se ficar 500 ms sem
  comando. Ao fechar o programa, o PC manda a parada.
- **UART ESP32 → Uno:** `M <x> <y>`, `S` (parar), `?` (pedir motivo do reset). **Uno → ESP32:** `R,<mcusr>,<NOME>,<uptime>`.

## Display OLED

O painel SSD1306 (128×64) está montado girado 90° no sentido horário, então o desenho é girado (`OLED_ROTATION` em
`src/esp32/config.h`, padrão 3; use 1 se aparecer de cabeça para baixo). A tela é em retrato (10 colunas × 16 linhas)
e mostra: Wi-Fi (dispositivos), PC conectado, vídeo (fps), Uno respondendo, último comando X/Y, potência de TX, canal,
memória livre, uptime e o motivo do último reset do ESP32 (`E`) e do Uno (`U`).

## Comandos

```bash
pio run -e esp32 -t upload                # firmware do ESP32
pio run -e uno -t upload                  # firmware do Uno
pio device monitor -e esp32               # logs ([TAG] por módulo); idem para -e uno

python3 pc/main.py                        # programa do PC (use --ip, --control-port, --video-port se precisar)
python3 pc/measure.py                     # mede o vídeo no robô real, cenário por cenário (fps, atraso, jitter, perda, ping)
python3 pc/fake_robot.py                  # robô simulado, para testar o programa sem o hardware (main.py --ip 127.0.0.1)
python3 -m unittest discover -s pc/tests  # testes do protocolo e do fluxo completo
```

## Ajustes

Em `platformio.ini` (`build_flags`) ou nos `config.h`: `POWER_SETTLE_MS` e `POWER_STEP_MS` (partida escalonada, para
fontes externas que levam ~1 s para estabilizar) e `MAX_SPEED_PERCENT` (limite de velocidade dos motores, padrão 50%).

## Desempenho do vídeo

A janela mostra, ao vivo, fps, Mbps, atraso relativo (p50/p95), jitter, perda, o ping do controle e o estado do ESP32,
e tem um painel para mudar os ajustes na hora (qualidade, resolução, pacing, FEC, modo da câmera, taxa do rádio e
compressão adaptativa). `pc/measure.py` roda os cenários no robô real, em rodadas intercaladas, e imprime as medianas.

Medido no robô (25 fps, ~1 Mbps, 320×240, sinal bom; atraso relativo ao melhor caso de cada medição):

| Ajuste | Atraso p50 / p95 | Ping do controle | Conclusão |
|---|---|---|---|
| sem pacing (pacotes em rajada) | 47 / 265 ms | 87 ms | |
| pacing de 1.500 µs | 12 / 40 ms | 20 ms | |
| **pacing de 4.000 µs (padrão)** | **6 / 28 ms** | **21 ms** | melhor; repetido em 4 baterias |
| FEC | pior | 80 ms | sem perda não ajuda (mais pacotes) |
| taxa do rádio fixa (54M / MCS7) | igual | igual | sem ganho, com perdas em algumas rodadas |
| 2 buffers de câmera | 52 / 146 ms | 73 ms | dobra os fps (49), mas piora o controle |
| compressão adaptativa | igual ao base | igual | rede de segurança: só age com perda ou atraso alto |

O pacing só funciona com **espera ocupada** no ESP32 (`delayMicroseconds`): trocar por `vTaskDelay`, que cede a CPU,
perdeu todo o ganho. Não sei explicar o mecanismo; descartei o `loop()` sem pausa e o display como causas. O efeito é
reprodutível. A economia de energia do Wi-Fi do próprio PC pesa pouco (atraso p50 ~20 ms menor ao desligar).

Os testes anteriores em `tools/bench/` (versão por WebSocket) mostram que a câmera ligada eleva a latência do Wi-Fi do
ESP32 mesmo ociosa (ping de 4–8 ms para 15–60 ms). Detalhes em `tools/bench/results/relatorio-video-wifi.pdf`.
