#include "robot_link.h"
#include "config.h"
#include "log.h"

namespace robot_link {
namespace {

ResetHandler resetHandler = nullptr;
char unoReset[48] = "";
char unoKey[24] = "";              // "<codigo>,<NOME>" do último reset do Uno (sem o uptime)
unsigned long unoUptime = 0;
unsigned long lastRxAt = 0;
unsigned long lastPingAt = 0;
int lastMoveX = 0, lastMoveY = 0;

// Trata uma linha completa vinda do Uno. O Uno responde ao "?" (ping) com o motivo do reset; só repassa
// ao app quando é novidade (primeira vez, motivo diferente ou uptime menor = o Uno reiniciou).
void handleLine(const char *line) {
  lastRxAt = millis();
  if (line[0] != 'R' || line[1] != ',') {
    LOG("UART", "RX do Uno: %s", line);
    return;
  }
  const char *lastComma = strrchr(line, ',');
  if (!lastComma || lastComma <= line + 2) return;

  unsigned long uptime = strtoul(lastComma + 1, nullptr, 10);
  size_t keyLen = lastComma - (line + 2);
  bool changed = unoReset[0] == '\0' || uptime < unoUptime || keyLen >= sizeof(unoKey) ||
                 strncmp(unoKey, line + 2, keyLen) != 0 || unoKey[keyLen] != '\0';

  snprintf(unoReset, sizeof(unoReset), "U,%s", line + 2);
  if (keyLen < sizeof(unoKey)) {
    memcpy(unoKey, line + 2, keyLen);
    unoKey[keyLen] = '\0';
  }
  unoUptime = uptime;

  if (changed) {
    LOG("UART", "RX do Uno: %s", line);
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
  if (millis() - lastPingAt >= UNO_PING_MS) {
    lastPingAt = millis();
    requestResetInfo();
  }

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
  lastMoveX = x;
  lastMoveY = y;
  Serial1.printf("M %d %d\n", x, y);
  LOG("UART", "TX: M %d %d", x, y);
}

void stop() {
  lastMoveX = lastMoveY = 0;
  Serial1.print("S\n");
  LOG("UART", "TX: S");
}

void requestResetInfo() { Serial1.print("?\n"); }

const char *lastUnoReset() { return unoReset; }

bool unoAlive() { return lastRxAt != 0 && millis() - lastRxAt < UNO_ALIVE_MS; }

int lastX() { return lastMoveX; }
int lastY() { return lastMoveY; }

// "U,<codigo>,<NOME>,<uptime>" -> NOME
void unoResetName(char *out, size_t size) {
  out[0] = '\0';
  const char *first = strchr(unoReset, ',');
  const char *second = first ? strchr(first + 1, ',') : nullptr;
  const char *third = second ? strchr(second + 1, ',') : nullptr;
  if (!second || !third) return;
  size_t n = min((size_t)(third - second - 1), size - 1);
  memcpy(out, second + 1, n);
  out[n] = '\0';
}

}  // namespace robot_link
