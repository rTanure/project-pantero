#pragma once
// Configuração central do firmware do ESP32: pinos, rede e ajustes de partida.
// Os valores marcados como "ajustável" podem ser trocados com -D no build_flags do platformio.ini.
#include <Arduino.h>
#include <WiFi.h>

// ---- UART com o Arduino Uno (Serial1) ----
constexpr int PIN_UNO_RX = 14;        // recebe o TX do Uno
constexpr int PIN_UNO_TX = 13;        // envia ao RX do Uno
constexpr uint32_t UNO_BAUD = 9600;   // o SoftwareSerial do Uno é instável acima disso

// ---- Wi-Fi (ponto de acesso) ----
constexpr const char *WIFI_SSID = "Pantero";
constexpr const char *WIFI_PASSWORD = "12345678";
constexpr int WIFI_CHANNEL = 6;
constexpr int8_t WIFI_TX_POWER_MAX = 84;  // teto do driver, em passos de 0,25 dBm (84 = 21 dBm)

// ---- Partida escalonada ----
// Com alimentação externa o 5 V leva ~1 s para estabilizar. Ligar Wi-Fi e câmera nesse intervalo
// causa brownout (falta de corrente) e o AP nunca aparece.
#ifndef POWER_SETTLE_MS
#define POWER_SETTLE_MS 2000  // ajustável: espera inicial antes de ligar qualquer carga
#endif
#ifndef POWER_STEP_MS
#define POWER_STEP_MS 150     // ajustável: intervalo entre ligar cada carga
#endif

// ---- Display OLED SSD1306 128x64 (I2C) ----
constexpr int PIN_OLED_SDA = 32;
constexpr int PIN_OLED_SCL = 33;
constexpr int OLED_WIDTH = 128;
constexpr int OLED_HEIGHT = 64;
// O painel está montado girado 90° no sentido horário: o desenho é girado 270° (3) para o texto
// ficar de pé. Se aparecer de cabeça para baixo, use 1. A tela lógica fica em retrato: 64x128 px.
#ifndef OLED_ROTATION
#define OLED_ROTATION 3  // ajustável
#endif
constexpr unsigned long DISPLAY_REFRESH_MS = 500;

// ---- Enlace com o Uno ----
constexpr unsigned long UNO_PING_MS = 5000;    // pergunta ao Uno se ele está vivo
constexpr unsigned long UNO_ALIVE_MS = 12000;  // sem resposta por este tempo: considerado fora

// ---- Enlace UDP com o programa do PC ----
constexpr uint16_t UDP_CONTROL_PORT = 4210;         // o PC envia "olá" e comandos para esta porta
constexpr unsigned long CLIENT_TIMEOUT_MS = 3000;   // sem pacotes do PC por este tempo: cliente perdido
constexpr size_t VIDEO_CHUNK_BYTES = 1400;          // dados de cada pacote UDP de vídeo (cabe no MTU)

constexpr unsigned long HEARTBEAT_MS = 10000;  // intervalo do log de saúde (heap, clientes)
