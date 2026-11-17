#include "video_ws.h"
#include "log.h"
#include <esp_camera.h>

namespace video_ws {
namespace {

constexpr int VIDEO_CORE = 1;                // núcleo 0 fica com o Wi-Fi, a pilha TCP/IP e o driver da câmera
constexpr size_t MIN_FREE_HEAP = 40 * 1024;  // abaixo disso o quadro é descartado (evita falta de memória)
constexpr unsigned long STATS_MS = 5000;

AsyncWebSocket socket("/wsv");

void onEvent(AsyncWebSocket *, AsyncWebSocketClient *client, AwsEventType type, void *, uint8_t *,
             size_t) {
  if (type == WS_EVT_CONNECT) {
    LOG("VIDEO", "cliente #%u conectou de %s", (unsigned)client->id(), client->remoteIP().toString().c_str());
  } else if (type == WS_EVT_DISCONNECT) {
    LOG("VIDEO", "cliente #%u desconectou (restam: %u)", (unsigned)client->id(), (unsigned)socket.count());
  }
}

// O quadro só é enfileirado se a fila de todos os clientes tiver espaço e houver memória livre
bool canSend() { return socket.availableForWriteAll() && ESP.getFreeHeap() > MIN_FREE_HEAP; }

void captureTask(void *) {
  unsigned long sent = 0, dropped = 0, statsAt = millis();
  for (;;) {
    if (socket.count() == 0) {  // ninguém assistindo: não gasta CPU
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      LOG("VIDEO", "ERRO: esp_camera_fb_get retornou NULL");
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    if (canSend()) {
      socket.binaryAll(fb->buf, fb->len);
      sent++;
    } else {
      dropped++;
    }
    esp_camera_fb_return(fb);

    if (millis() - statsAt >= STATS_MS) {
      LOG("VIDEO", "%.1f fps enviados, %lu descartados, heap livre %u",
          sent * 1000.0 / (millis() - statsAt), dropped, (unsigned)ESP.getFreeHeap());
      sent = dropped = 0;
      statsAt = millis();
    }
  }
}

}  // namespace

void begin(AsyncWebServer &server) {
  socket.onEvent(onEvent);
  server.addHandler(&socket);
  xTaskCreatePinnedToCore(captureTask, "video-capture", 4096, nullptr, 6, nullptr, VIDEO_CORE);
  LOG("VIDEO", "WebSocket de video em /wsv (captura no nucleo %d)", VIDEO_CORE);
}

void cleanup() { socket.cleanupClients(); }

size_t clients() { return socket.count(); }

}  // namespace video_ws
