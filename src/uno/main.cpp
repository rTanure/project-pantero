// Pantero (Arduino Uno): executa os comandos do ESP32 e controla motores e Quick Charge.
//
//   esp_link      UART com o ESP32: comandos de movimento e motivo do reset
//   motors        ponte H com mistura estilo tanque e parada automática
//   quick_charge  pede 9 V ao carregador Quick Charge 3.0
//   reset_info    motivo do último reset do Uno (registrador MCUSR)
#include <Arduino.h>
#include "config.h"
#include "esp_link.h"
#include "log.h"
#include "motors.h"
#include "quick_charge.h"
#include "reset_info.h"

// Leitura periódica dos sensores de linha (ainda sem seguidor de linha: serve para calibrar o TCRT5000)
void heartbeat() {
  static unsigned long lastAt = 0;
  if (millis() - lastAt < HEARTBEAT_MS) return;
  lastAt = millis();
  Serial.print('[');
  Serial.print(millis());
  Serial.print(F(" ms][BEAT] linha esq="));
  Serial.print(analogRead(PIN_LINE_L));
  Serial.print(F(" dir="));
  Serial.println(analogRead(PIN_LINE_R));
}

void setup() {
  Serial.begin(115200);
  LOGU("BOOT", F("Pantero Uno iniciando"));
  LOGU("RESET", String(F("MCUSR=")) + reset_info::mcusr() + F(" (") + reset_info::name() + F(")"));

  esp_link::begin();
  motors::begin();
  pinMode(PIN_LINE_L, INPUT);
  pinMode(PIN_LINE_R, INPUT);

  quick_charge::begin();   // bloqueia ~2 s (handshake + pausa)
  esp_link::sendResetInfo();  // avisa o ESP32 que o Uno (re)iniciou

  LOGU("BOOT", String(F("velocidade maxima: ")) + MAX_SPEED_PERCENT + F("% do PWM"));
  LOGU("BOOT", F("pronto, aguardando comandos do ESP32"));
}

void loop() {
  esp_link::poll();
  motors::update();
  quick_charge::update(motors::isIdle());
  heartbeat();
}
