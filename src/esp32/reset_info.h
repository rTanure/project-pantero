#pragma once
#include <stddef.h>

// Motivo do último reset do ESP32 (lido uma vez no boot).
namespace reset_info {

int code();           // valor de esp_reset_reason_t
const char *name();   // nome legível: POWERON, BROWNOUT, PANIC...

// Mensagem enviada aos clientes: "R,<codigo>,<NOME>,<uptime_ms>"
void format(char *out, size_t size);

}  // namespace reset_info
