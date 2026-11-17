#pragma once
#include <Arduino.h>

// Motivo do último reset do Uno, lido do registrador MCUSR antes do main().
// Se o bootloader já tiver zerado o registrador, o motivo aparece como BOOTLOADER (desconhecido).
namespace reset_info {

uint8_t mcusr();
const char *name();  // POWERON, BROWNOUT, WATCHDOG, EXTERNAL ou BOOTLOADER

// Mensagem para o ESP32 (que repassa ao app): "R,<MCUSR>,<NOME>,<uptime_ms>"
void format(char *out, size_t size);

}  // namespace reset_info
