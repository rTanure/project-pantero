#include "esp_link.h"
#include <SoftwareSerial.h>
#include "config.h"
#include "log.h"
#include "motors.h"
#include "reset_info.h"

namespace esp_link {
namespace {

SoftwareSerial link(PIN_LINK_RX, PIN_LINK_TX);

// Executa uma linha completa recebida do ESP32
void handleLine(const char *line) {
  switch (line[0]) {
    case 'M': {
      int x = 0, y = 0;
      if (sscanf(line + 1, "%d %d", &x, &y) == 2) {
        motors::drive(x, y);
      } else {
        LOGU("UART", String(F("M invalido: ")) + line);
      }
      break;
    }
    case 'S':
      LOGU("UART", F("RX: S (parar)"));
      motors::stop();
      break;
    case '?':
      sendResetInfo();
      break;
    default:
      LOGU("UART", String(F("comando desconhecido: ")) + line);
  }
}

}  // namespace

void begin() { link.begin(LINK_BAUD); }

void poll() {
  static char buf[24];
  static uint8_t len = 0;
  while (link.available()) {
    char c = link.read();
    if (c == '\n') {
      buf[len] = '\0';
      if (len > 0) handleLine(buf);
      len = 0;
    } else if (c != '\r' && len < sizeof(buf) - 1) {
      buf[len++] = c;
    }
  }
}

void sendResetInfo() {
  char msg[40];
  reset_info::format(msg, sizeof(msg));
  link.println(msg);
  LOGU("RESET", String(F("enviado ao ESP32: ")) + msg);
}

}  // namespace esp_link
