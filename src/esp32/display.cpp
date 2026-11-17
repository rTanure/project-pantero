#include "display.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include "config.h"
#include "log.h"
#include "reset_info.h"
#include "robot_link.h"
#include "udp_link.h"
#include "video_udp.h"
#include "wifi_ap.h"

namespace display {
namespace {

constexpr int COLS = 10;       // 64 px / 6 px por caractere
constexpr int LINE_PX = 8;     // altura de cada linha de texto
constexpr uint8_t ADDRESSES[] = {0x3C, 0x3D};  // endereços comuns do SSD1306

Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
bool ready = false;
bool cameraOk = false;

bool respondsAt(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

// Linha de texto: rótulo à esquerda e valor alinhado à direita, em COLS colunas (valor longo é cortado)
void put(int row, const char *label, const char *value = "") {
  char line[COLS + 1];
  memset(line, ' ', COLS);
  line[COLS] = '\0';

  size_t labelLen = min(strlen(label), (size_t)COLS);
  memcpy(line, label, labelLen);

  size_t room = COLS - labelLen - (labelLen ? 1 : 0);
  size_t valueLen = min(strlen(value), room);
  memcpy(line + COLS - valueLen, value, valueLen);

  oled.setCursor(0, row * LINE_PX);
  oled.print(line);
}

void separator(int row) { oled.drawFastHLine(0, row * LINE_PX + LINE_PX / 2, oled.width(), SSD1306_WHITE); }

void title(const char *text) {
  oled.fillRect(0, 0, oled.width(), LINE_PX, SSD1306_WHITE);
  oled.setTextColor(SSD1306_BLACK);
  oled.setCursor(1, 0);
  oled.print(text);
  oled.setTextColor(SSD1306_WHITE);
}

void formatUptime(char *out, size_t size) {
  unsigned long s = millis() / 1000;
  if (s < 3600) snprintf(out, size, "%02lu:%02lu", s / 60, s % 60);
  else snprintf(out, size, "%luh%02lum", s / 3600, (s / 60) % 60);
}

void drawStatus() {
  char v[16];
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);

  title("PANTERO");

  // Conexões
  snprintf(v, sizeof(v), "%d", wifi_ap::stations());
  put(1, "WIFI", v);
  put(2, "PC", udp_link::clientActive() ? "ON" : "OFF");
  if (!cameraOk) {
    put(3, "VID", "FALHA");
  } else if (!udp_link::clientActive()) {
    put(3, "VID", "parado");
  } else {
    snprintf(v, sizeof(v), "%.0ffps", video_udp::fps());
    put(3, "VID", v);
  }
  put(4, "UNO", robot_link::unoAlive() ? "OK" : "OFF");

  // Último comando de movimento (-100..100)
  separator(5);
  snprintf(v, sizeof(v), "%+d", robot_link::lastX());
  put(6, "X", v);
  snprintf(v, sizeof(v), "%+d", robot_link::lastY());
  put(7, "Y", v);

  // Parâmetros
  separator(8);
  snprintf(v, sizeof(v), "%.1fdBm", wifi_ap::txPowerDbm());
  put(9, "TX", v);
  snprintf(v, sizeof(v), "%d", WIFI_CHANNEL);
  put(10, "CANAL", v);
  snprintf(v, sizeof(v), "%ukB", (unsigned)(ESP.getFreeHeap() / 1024));
  put(11, "MEM", v);
  formatUptime(v, sizeof(v));
  put(12, "UP", v);

  // Motivo do último reset de cada placa
  put(13, "RESET");
  put(14, "E", reset_info::name());
  char uno[16];
  robot_link::unoResetName(uno, sizeof(uno));
  put(15, "U", uno[0] ? uno : "-");

  oled.display();
}

// Redesenha periodicamente. O envio de 1 KB por I2C leva ~25 ms: numa task própria, de baixa
// prioridade, ele não atrasa a leitura dos comandos nem o ping de controle.
void displayTask(void *) {
  for (;;) {
    drawStatus();
    vTaskDelay(pdMS_TO_TICKS(DISPLAY_REFRESH_MS));
  }
}

}  // namespace

void begin() {
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  Wire.setClock(400000);

  uint8_t address = 0;
  for (uint8_t candidate : ADDRESSES) {
    if (respondsAt(candidate)) {
      address = candidate;
      break;
    }
  }
  if (!address) {
    LOG("OLED", "nenhum display respondeu no I2C (SDA=%d, SCL=%d): display desativado", PIN_OLED_SDA,
        PIN_OLED_SCL);
    return;
  }
  if (!oled.begin(SSD1306_SWITCHCAPVCC, address)) {
    LOG("OLED", "ERRO ao iniciar o SSD1306 em 0x%02X: display desativado", address);
    return;
  }

  oled.setRotation(OLED_ROTATION);
  ready = true;
  LOG("OLED", "SSD1306 em 0x%02X, rotacao %d (tela logica %dx%d)", address, OLED_ROTATION, oled.width(),
      oled.height());

  oled.clearDisplay();
  oled.setTextSize(1);
  title("PANTERO");
  oled.setTextColor(SSD1306_WHITE);
  put(2, "iniciando");
  oled.display();
  xTaskCreatePinnedToCore(displayTask, "display", 4096, nullptr, 1, nullptr, 1);
}

void setCameraOk(bool ok) { cameraOk = ok; }

}  // namespace display
