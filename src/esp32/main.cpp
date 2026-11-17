// Pantero: robô controlado por um programa Python no PC (pasta pc/), que se conecta ao Wi-Fi do ESP32.
//
//   Wi-Fi (AP "Pantero") ── udp_link   UDP: comandos do PC -> robot_link -> Arduino Uno (UART)
//                                       e avisos de reset de volta ao PC
//                        └─ video_udp  UDP: quadros JPEG da câmera -> PC
// display: painel OLED com o status do robô (I2C, pinos 32/33)
//
// Núcleo 0: Wi-Fi, pilha TCP/IP e driver da câmera. Núcleo 1: captura e envio do vídeo.
#include <Arduino.h>
#include "camera.h"
#include "config.h"
#include "display.h"
#include "log.h"
#include "reset_info.h"
#include "robot_link.h"
#include "udp_link.h"
#include "video_udp.h"
#include "wifi_ap.h"

// O Uno informou o motivo do reset dele: repassa ao PC conectado
void onUnoReset(const char *message) { udp_link::sendText(message); }

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
  display::begin();

  robot_link::begin(onUnoReset);

  // Partida escalonada: Wi-Fi com TX baixo, depois a câmera, depois a potência final do Wi-Fi
  wifi_ap::begin();
  delay(POWER_STEP_MS);
  bool cameraOk = cameraInit();
  display::setCameraOk(cameraOk);
  if (!cameraOk) LOG("BOOT", "camera INDISPONIVEL: o video nao sera iniciado");
  delay(POWER_STEP_MS);
  wifi_ap::rampTxPower();

  udp_link::begin();
  if (cameraOk) video_udp::begin();

  robot_link::requestResetInfo();  // pede ao Uno o motivo do último reset dele
}

// Log de saúde periódico: ajuda a ver se o ESP32 travou ou ficou sem memória
void heartbeat() {
  static unsigned long lastAt = 0;
  if (millis() - lastAt < HEARTBEAT_MS) return;
  lastAt = millis();
  LOG("BEAT", "heap livre %u | min %u | wifi %d | PC %s | video %.1f fps", (unsigned)ESP.getFreeHeap(),
      (unsigned)ESP.getMinFreeHeap(), wifi_ap::stations(), udp_link::clientActive() ? "sim" : "nao",
      video_udp::fps());
}

void loop() {
  robot_link::poll();
  udp_link::poll();
  heartbeat();
}
