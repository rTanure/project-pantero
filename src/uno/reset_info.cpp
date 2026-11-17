#include "reset_info.h"
#include <avr/wdt.h>

// O MCUSR é lido e zerado em .init3 (antes do main); senão o watchdog pode ficar ativo e reiniciar o
// Uno em loop. A variável fica em .noinit para não ser apagada na inicialização do C.
static uint8_t mcusrMirror __attribute__((section(".noinit")));

void captureMcusr(void) __attribute__((naked)) __attribute__((used)) __attribute__((section(".init3")));
void captureMcusr(void) {
  mcusrMirror = MCUSR;
  MCUSR = 0;
  wdt_disable();
}

namespace reset_info {

uint8_t mcusr() { return mcusrMirror; }

const char *name() {
  if (mcusrMirror & _BV(PORF)) return "POWERON";
  if (mcusrMirror & _BV(BORF)) return "BROWNOUT";
  if (mcusrMirror & _BV(WDRF)) return "WATCHDOG";
  if (mcusrMirror & _BV(EXTRF)) return "EXTERNAL";
  return "BOOTLOADER";
}

void format(char *out, size_t size) {
  snprintf(out, size, "R,%u,%s,%lu", mcusrMirror, name(), millis());
}

}  // namespace reset_info
