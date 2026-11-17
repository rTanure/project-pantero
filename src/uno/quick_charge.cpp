#include "quick_charge.h"
#include <QC3Control.h>
#include "config.h"
#include "log.h"

namespace quick_charge {
namespace {

QC3Control charger(PIN_QC_DP, PIN_QC_DM);
unsigned long lastRequestAt = 0;

void request9V() {
  charger.set9V();
  lastRequestAt = millis();
}

}  // namespace

void begin() {
  LOGU("QC3", F("handshake: aguardando 1,5 s desde o boot (D+ em 0,6 V)..."));
  charger.begin();
  LOGU("QC3", String(F("handshake concluido, aguardando ")) + QC_AFTER_BEGIN_DELAY_MS + F(" ms antes do pedido"));
  delay(QC_AFTER_BEGIN_DELAY_MS);

  request9V();
  LOGU("QC3", F("pedido de 9 V enviado (valor PEDIDO, nao medido: confira o VBUS com um multimetro)"));
}

void update(bool robotIdle) {
  if (!robotIdle || millis() - lastRequestAt < QC_REASSERT_MS) return;
  request9V();
  LOGU("QC3", F("pedido de 9 V reafirmado"));
}

}  // namespace quick_charge
