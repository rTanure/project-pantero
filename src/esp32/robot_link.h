#pragma once

// Enlace serial com o Arduino Uno (que controla motores e Quick Charge).
// Protocolo, uma linha por comando terminada em '\n':
//   ESP32 -> Uno:  "M <x> <y>"  mover (x e y de -100 a 100)
//                  "S"          parar
//                  "?"          pedir o motivo do último reset do Uno
//   Uno -> ESP32:  "R,<mcusr>,<NOME>,<uptime_ms>"  motivo do reset do Uno
namespace robot_link {

// onUnoReset recebe a mensagem já pronta para os clientes: "U,<mcusr>,<NOME>,<uptime_ms>"
using ResetHandler = void (*)(const char *message);

void begin(ResetHandler onUnoReset);
void poll();  // chamar no loop(): lê as linhas vindas do Uno

void move(int x, int y);   // x: direita +, y: frente + (limitados a -100..100)
void stop();
void requestResetInfo();

// Último "U,..." recebido do Uno; vazio se ainda não houve nenhum
const char *lastUnoReset();

}  // namespace robot_link
