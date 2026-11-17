#pragma once
#include <ESPAsyncWebServer.h>

// WebSocket de controle (/ws), usado pelo app para dirigir o robô.
//   app -> ESP32: "x,y"                          inteiros de -100 a 100 (x: direita +, y: frente +)
//   ESP32 -> app: "R,<codigo>,<NOME>,<uptime>"   motivo do último reset do ESP32 (ao conectar)
//                 "U,<mcusr>,<NOME>,<uptime>"    motivo do último reset do Arduino Uno
// Quando o último cliente sai, o robô é parado.
namespace control_ws {

void begin(AsyncWebServer &server);
void broadcast(const char *message);  // texto para todos os clientes
void cleanup();                       // chamar no loop(): remove clientes desconectados
size_t clients();

}  // namespace control_ws
