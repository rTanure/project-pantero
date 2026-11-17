// Pantero: robô controlado por uma aplicação React servida pelo próprio ESP32.
//
//   Wi-Fi (AP "Pantero") ── web_server  serve o app React (LittleFS)
//                        ├─ control_ws  /ws   comandos do app -> robot_link -> Arduino Uno (UART)
//                        └─ video_ws    /wsv  quadros JPEG da câmera -> app
//
// Núcleo 0: Wi-Fi, pilha TCP/IP, driver da câmera e servidor web. Núcleo 1: captura e envio do vídeo.
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include "camera.h"
#include "config.h"
#include "control_ws.h"
#include "log.h"
#include "reset_info.h"
#include "robot_link.h"
#include "video_ws.h"
#include "web_server.h"
#include "wifi_ap.h"

AsyncWebServer server(HTTP_PORT);

// O Uno informou o motivo do reset dele: repassa a todos os apps conectados
void onUnoReset(const char *message) { control_ws::broadcast(message); }

void logBootInfo() {
  LOG("BOOT", "Pantero iniciando | chip %s rev %d | %u MHz | flash %u KB | heap livre %u",
      ESP.getChipModel(), (int)ESP.getChipRevision(), (unsigned)ESP.getCpuFreqMHz(),
      (unsigned)(ESP.getFlashChipSize() / 1024), (unsigned)ESP.getFreeHeap());
  LOG("BOOT", "motivo do ultimo reset: %d (%s)", reset_info::code(), reset_info::name());
}

void setup() {
  Serial.begin(115200);
  delay(POWER_SETTLE_MS);  // deixa a alimentação estabilizar antes de ligar qualquer carga
  logBootInfo();

  web_server::mountFilesystem();
  robot_link::begin(onUnoReset);

  // Partida escalonada: Wi-Fi com TX baixo, depois a câmera, depois a potência final do Wi-Fi
  wifi_ap::begin();
  delay(POWER_STEP_MS);
  bool cameraOk = cameraInit();
  if (!cameraOk) LOG("BOOT", "camera INDISPONIVEL: o video nao sera iniciado");
  delay(POWER_STEP_MS);
  wifi_ap::rampTxPower();

  control_ws::begin(server);
  if (cameraOk) video_ws::begin(server);
  web_server::begin(server);
  server.begin();
  LOG("HTTP", "servidor iniciado na porta %u", HTTP_PORT);

  robot_link::requestResetInfo();  // pede ao Uno o motivo do último reset dele
}

// Log de saúde periódico: ajuda a ver se o ESP32 travou ou ficou sem memória
void heartbeat() {
  static unsigned long lastAt = 0;
  if (millis() - lastAt < HEARTBEAT_MS) return;
  lastAt = millis();
  LOG("BEAT", "heap livre %u | min %u | wifi %d | controle %u | video %u", (unsigned)ESP.getFreeHeap(),
      (unsigned)ESP.getMinFreeHeap(), wifi_ap::stations(), (unsigned)control_ws::clients(),
      (unsigned)video_ws::clients());
}

void loop() {
  robot_link::poll();
  control_ws::cleanup();
  video_ws::cleanup();
  heartbeat();
}
