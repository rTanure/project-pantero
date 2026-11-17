#include "wifi_ap.h"
#include "config.h"
#include "log.h"
#include <esp_wifi.h>

// API interna do ESP-IDF (não declarada nos headers públicos) que fixa a taxa de transmissão
extern "C" esp_err_t esp_wifi_internal_set_fix_rate(wifi_interface_t ifx, bool en, wifi_phy_rate_t rate);

namespace wifi_ap {
namespace {

int rateMode = 0;

void onWifiEvent(arduino_event_id_t event, arduino_event_info_t info) {
  if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
    const uint8_t *m = info.wifi_ap_staconnected.mac;
    LOG("WIFI", "dispositivo conectou: %02x:%02x:%02x:%02x:%02x:%02x (total: %d)", m[0], m[1],
        m[2], m[3], m[4], m[5], stations());
  } else if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
    LOG("WIFI", "dispositivo desconectou (total: %d)", stations());
  }
}

}  // namespace

void begin() {
  WiFi.softAP(WIFI_SSID, WIFI_PASSWORD, WIFI_CHANNEL);
  WiFi.setSleep(false);  // sem modem sleep: elimina atrasos de dezenas de ms
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.onEvent(onWifiEvent);
  LOG("WIFI", "AP \"%s\" ativo no canal %d, IP %s (TX codigo %d)", WIFI_SSID, WIFI_CHANNEL,
      WiFi.softAPIP().toString().c_str(), (int)WiFi.getTxPower());
}

void rampTxPower() {
  const wifi_power_t steps[] = {WIFI_POWER_11dBm, WIFI_POWER_15dBm, WIFI_POWER_19_5dBm};
  for (wifi_power_t power : steps) {
    WiFi.setTxPower(power);
    LOG("WIFI", "potencia de TX ajustada (codigo %d, em 0,25 dBm)", (int)WiFi.getTxPower());
    delay(POWER_STEP_MS / 2);
  }

  // Teto do driver: o chip ainda pode limitar o valor por taxa de transmissão e calibração;
  // o log mostra o que foi realmente aceito.
  esp_err_t err = esp_wifi_set_max_tx_power(WIFI_TX_POWER_MAX);
  int8_t accepted = 0;
  esp_wifi_get_max_tx_power(&accepted);
  LOG("WIFI", "TX maximo pedido %d -> %s; aceito %d (%.2f dBm)", (int)WIFI_TX_POWER_MAX,
      esp_err_to_name(err), (int)accepted, accepted * 0.25);
}

int stations() { return WiFi.softAPgetStationNum(); }

void setFixedRate(int mode) {
  if (mode == rateMode || mode < 0 || mode > 2) return;
  esp_err_t err;
  if (mode == 0) err = esp_wifi_internal_set_fix_rate(WIFI_IF_AP, false, WIFI_PHY_RATE_1M_L);
  else err = esp_wifi_internal_set_fix_rate(WIFI_IF_AP, true, mode == 1 ? WIFI_PHY_RATE_54M : WIFI_PHY_RATE_MCS7_SGI);
  LOG("WIFI", "taxa de transmissao %d (0=auto 1=54M 2=MCS7): %s", mode, esp_err_to_name(err));
  if (err == ESP_OK) rateMode = mode;
}

int fixedRate() { return rateMode; }

float txPowerDbm() {
  int8_t power = 0;
  esp_wifi_get_max_tx_power(&power);
  return power * 0.25f;
}

}  // namespace wifi_ap
