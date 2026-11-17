# Banco de testes de vídeo e Wi-Fi

Kit usado para medir o vídeo do ESP32 (fps, jitter, atraso, ping de controle). Não faz parte do firmware
de produção.

| Item | Função |
|---|---|
| `firmware/` | Firmware de teste: expõe `/stream` (porta 81) e `/cfg`, `/stats`, `/tasks` (porta 83) com parâmetros mutáveis em tempo de execução, e `/wsv` com vídeo por WebSocket |
| `bench.py` | Cliente que conecta ao ESP32 por Wi-Fi e roda as matrizes de teste (`run`, `rep`, `prio`, `clk`, `ab`, `summary`) |
| `report_pdf.py` | Gera `results/relatorio-video-wifi.pdf` a partir dos JSONs |
| `results/` | Medições brutas (`rodada1..3`) e o relatório em PDF |

## Como reutilizar o firmware de teste

O firmware de produção não inclui o banco de testes. Para usá-lo:

1. Copie `firmware/*` para `src/esp32/`. Isso **substitui** o `camera.cpp` e o `camera.h` de produção
   pelo `camera_ex.cpp` (init da câmera parametrizável); apague o `camera.cpp` original antes.
2. No `main.cpp`, depois de `cameraInit()`, chame `tbWsInit(); testbedStart();` (e inclua `testbed.h`)
   no lugar de `video_ws::begin(server)`.
3. Adicione um ambiente ao `platformio.ini`:

```ini
[env:esp32test]
extends = env:esp32
build_flags =
    ${env:esp32.build_flags}
    -DTESTBED
    -DWS_MAX_QUEUED_MESSAGES=2
    -DCAM_FRAME_SIZE=FRAMESIZE_VGA
```

4. Grave com `pio run -e esp32test -t upload`, conecte o PC no Wi-Fi Pantero e rode
   `python3 tools/bench/bench.py rep resultado.json` (ajuste `IFACE` e `HOST` no topo do script).
