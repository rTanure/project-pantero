#pragma once

// Ponto de acesso Wi-Fi do robô. A partida é escalonada para não puxar corrente demais de uma vez:
//   1) begin() liga o AP com potência de TX baixa
//   2) (o main liga a câmera)
//   3) rampTxPower() sobe a potência em degraus até o máximo do driver
namespace wifi_ap {

void begin();
void rampTxPower();
int stations();      // dispositivos conectados ao AP
float txPowerDbm();  // potência máxima de TX aceita pelo driver

}  // namespace wifi_ap
