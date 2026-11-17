#include "video_ws.h"
#include "log.h"
#include <esp_camera.h>

namespace video_ws {
namespace {

constexpr int VIDEO_CORE = 1;                // núcleo 0 fica com o Wi-Fi, a pilha TCP/IP e o driver da câmera
constexpr unsigned long STATS_MS = 5000;
constexpr unsigned long FPS_WINDOW_MS = 2000;
constexpr unsigned long SEND_WAIT_MAX_MS = 100;  // cliente mais lento que isso: o quadro segue sem esperar

volatile float currentFps = 0;

AsyncWebSocket socket("/wsv");

void onEvent(AsyncWebSocket *, AsyncWebSocketClient *client, AwsEventType type, void *, uint8_t *,
             size_t) {
  if (type == WS_EVT_CONNECT) {
    LOG("VIDEO", "cliente #%u conectou de %s", (unsigned)client->id(), client->remoteIP().toString().c_str());
  } else if (type == WS_EVT_DISCONNECT) {
    LOG("VIDEO", "cliente #%u desconectou (restam: %u)", (unsigned)client->id(), (unsigned)socket.count());
  }
}

// Sem buffer: a fila tem 1 posição (WS_MAX_QUEUED_MESSAGES=1), então um quadro só entra se o
// anterior já foi entregue a todos os clientes. Senão é descartado.
bool canSend() { return socket.availableForWriteAll(); }

// Espera a fila esvaziar (quadro entregue à pilha TCP). Retorna false se o cliente demorar demais.
bool waitDelivered() {
  unsigned long start = millis();
  while (!canSend()) {
    if (millis() - start > SEND_WAIT_MAX_MS) return false;
    vTaskDelay(1);
  }
  return true;
}

// Alterna captura e transmissão, sem sobreposição:
//   capturar -> transmitir -> esperar a entrega (com o buffer retido: a câmera fica parada) -> devolver
// Quadro que não couber na fila, ou cliente lento demais: perdeu, perdeu.
void captureTask(void *) {
  unsigned long sent = 0, dropped = 0, statsAt = millis();
  unsigned long windowSent = 0, windowAt = millis();
  bool wasIdle = true;
  for (;;) {
    if (socket.count() == 0) {  // ninguém assistindo: não gasta CPU
      currentFps = 0;
      wasIdle = true;
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      LOG("VIDEO", "ERRO: esp_camera_fb_get retornou NULL");
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    if (wasIdle) {  // o buffer ficou parado enquanto ninguém assistia: o primeiro quadro é velho
      wasIdle = false;
      esp_camera_fb_return(fb);
      continue;
    }

    if (canSend()) {
      socket.binaryAll(fb->buf, fb->len);  // copia o quadro para a fila do WebSocket
      sent++;
      windowSent++;
      waitDelivered();                     // câmera parada até o quadro sair
    } else {
      dropped++;
    }
    esp_camera_fb_return(fb);              // agora a câmera pode capturar o próximo

    if (millis() - windowAt >= FPS_WINDOW_MS) {
      currentFps = windowSent * 1000.0f / (millis() - windowAt);
      windowSent = 0;
      windowAt = millis();
    }
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

float fps() { return currentFps; }

}  // namespace video_ws
