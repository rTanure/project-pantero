#ifdef TESTBED
#include <ESPAsyncWebServer.h>
#include "testbed.h"
#include "log.h"

extern AsyncWebServer server;  // definido em main.cpp
static AsyncWebSocket wsv("/wsv");
static uint32_t wsSent = 0, wsDropped = 0;

void tbWsInit() {
  server.addHandler(&wsv);
  LOG("TESTBED", "WebSocket de video em /wsv");
}

int tbWsClients() { return (int)wsv.count(); }

void tbWsStats(uint32_t *sent, uint32_t *dropped) {
  *sent = wsSent;
  *dropped = wsDropped;
}

void tbWsPublish(const uint8_t *buf, size_t len, uint32_t seq, uint32_t tsMs) {
  static uint8_t *tmp = nullptr;
  static size_t cap = 0;
  static unsigned long lastClean = 0;
  if (millis() - lastClean > 1000) {
    wsv.cleanupClients();
    lastClean = millis();
  }
  if (wsv.count() == 0) return;

  // Cliente lento: descarta o frame em vez de enfileirar (fila = latência crescente)
  if (!wsv.availableForWriteAll()) {
    wsDropped++;
    return;
  }
  if (len + 8 > cap) {
    free(tmp);
    cap = len + 8 + 2048;
    tmp = (uint8_t *)malloc(cap);
    if (!tmp) { cap = 0; return; }
  }
  memcpy(tmp, &seq, 4);
  memcpy(tmp + 4, &tsMs, 4);
  memcpy(tmp + 8, buf, len);
  wsv.binaryAll(tmp, len + 8);
  wsSent++;
}
#endif
