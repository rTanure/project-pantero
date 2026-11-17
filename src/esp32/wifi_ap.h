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

// Taxa de transmissão do AP: 0 = automática (padrão), 1 = fixa em 54 Mbps (OFDM), 2 = fixa em MCS7 (72 Mbps).
// Taxa fixa evita que a adaptação automática caia para taxas lentas (jitter), mas piora com sinal fraco.
void setFixedRate(int mode);
int fixedRate();

}  // namespace wifi_ap
