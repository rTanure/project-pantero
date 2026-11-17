#include "reset_info.h"
#include <Arduino.h>
#include <esp_system.h>

namespace {

// Lido uma vez, na inicialização do programa
const esp_reset_reason_t reason = esp_reset_reason();

const char *nameOf(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_EXT:       return "EXT";
    case ESP_RST_SW:        return "SOFTWARE";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "UNKNOWN";
  }
}

}  // namespace

namespace reset_info {

int code() { return (int)reason; }

const char *name() { return nameOf(reason); }

void format(char *out, size_t size) {
  snprintf(out, size, "R,%d,%s,%lu", code(), name(), millis());
}

}  // namespace reset_info
