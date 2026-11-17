#include "wifi_ap.h"
#include "config.h"
#include "log.h"
#include <esp_wifi.h>

namespace wifi_ap {
namespace {

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

}  // namespace wifi_ap
