#pragma once
// Configuração central do firmware do Arduino Uno: pinos e ajustes.
#include <Arduino.h>

// ---- UART com o ESP32 (cruzado: RX do Uno <- TX do ESP32 e vice-versa) ----
constexpr uint8_t PIN_LINK_RX = 12;
constexpr uint8_t PIN_LINK_TX = 13;
constexpr long LINK_BAUD = 9600;  // SoftwareSerial é instável acima disso

// ---- Sensores do seguidor de linha (TCRT5000) ----
constexpr uint8_t PIN_LINE_L = A0;
constexpr uint8_t PIN_LINE_R = A1;

// ---- Quick Charge 3 (D+ / D-) ----
constexpr uint8_t PIN_QC_DP = A3;
constexpr uint8_t PIN_QC_DM = A2;

// ---- Ponte H (todos PWM no Uno) ----
constexpr uint8_t PIN_IN1 = 6;   // motor esquerdo
constexpr uint8_t PIN_IN2 = 9;
constexpr uint8_t PIN_IN3 = 10;  // motor direito
constexpr uint8_t PIN_IN4 = 11;

// ---- Motores ----
// Velocidade máxima em % do PWM total (reduz consumo e picos de corrente)
#ifndef MAX_SPEED_PERCENT
#define MAX_SPEED_PERCENT 50  // ajustável com -D no build_flags
#endif
constexpr unsigned long COMMAND_TIMEOUT_MS = 500;  // sem comando: para o robô

// ---- Quick Charge: a tensão é sempre 9 V ----
constexpr unsigned long QC_AFTER_BEGIN_DELAY_MS = 500;  // pausa entre o handshake e o 1º pedido
constexpr unsigned long QC_REASSERT_MS = 5000;          // repete o pedido de 9 V com o robô parado

constexpr unsigned long HEARTBEAT_MS = 10000;  // intervalo do log de sensores
