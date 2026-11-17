#include "robot_link.h"
#include "config.h"
#include "log.h"

namespace robot_link {
namespace {

ResetHandler resetHandler = nullptr;
char unoReset[48] = "";

// Trata uma linha completa vinda do Uno
void handleLine(const char *line) {
  LOG("UART", "RX do Uno: %s", line);
  if (line[0] == 'R' && line[1] == ',') {
    snprintf(unoReset, sizeof(unoReset), "U,%s", line + 2);
    if (resetHandler) resetHandler(unoReset);
  }
}

}  // namespace

void begin(ResetHandler onUnoReset) {
  resetHandler = onUnoReset;
  Serial1.begin(UNO_BAUD, SERIAL_8N1, PIN_UNO_RX, PIN_UNO_TX);
  LOG("UART", "Serial1 %lu 8N1 (RX=%d, TX=%d)", (unsigned long)UNO_BAUD, PIN_UNO_RX, PIN_UNO_TX);
}

void poll() {
  static char buf[48];
  static uint8_t len = 0;
  while (Serial1.available()) {
    char c = Serial1.read();
    if (c == '\n') {
      buf[len] = '\0';
      if (len > 0) handleLine(buf);
      len = 0;
    } else if (c != '\r' && len < sizeof(buf) - 1) {
      buf[len++] = c;
    }
  }
}

void move(int x, int y) {
  x = constrain(x, -100, 100);
  y = constrain(y, -100, 100);
  Serial1.printf("M %d %d\n", x, y);
  LOG("UART", "TX: M %d %d", x, y);
}

void stop() {
  Serial1.print("S\n");
  LOG("UART", "TX: S");
}

void requestResetInfo() { Serial1.print("?\n"); }

const char *lastUnoReset() { return unoReset; }

}  // namespace robot_link
