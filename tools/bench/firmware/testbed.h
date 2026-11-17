#pragma once
#include <stddef.h>
#include <stdint.h>

// Banco de testes de vídeo/Wi-Fi: só é compilado com -DTESTBED (ambiente esp32test).
// Em produção estes símbolos não existem e o main.cpp não os chama.

void testbedStart();  // testbed.cpp: captura + /stream (porta 81) + /cfg e /stats (porta 83)
void tbWsInit();      // testbed_ws.cpp: registra /wsv (vídeo por WebSocket) no servidor da porta 80

// testbed_ws.cpp: publica um frame (prefixo de 8 bytes: seq u32 + ts u32) se algum cliente /wsv estiver
// conectado e apto a receber; senão descarta (contabilizado em tbWsStats)
void tbWsPublish(const uint8_t *buf, size_t len, uint32_t seq, uint32_t tsMs);
int tbWsClients();
void tbWsStats(uint32_t *sent, uint32_t *dropped);

// camera_ex.cpp: init parametrizável da câmera (0/-1 mantêm o padrão; fb_location 0=DRAM 1=PSRAM)
bool cameraInitEx(int fbCountOverride, int grabOverride, int xclkOverride, int fbLocOverride);
bool cameraInit();
