#pragma once

// Enlace serial com o ESP32 (SoftwareSerial). Protocolo, uma linha por comando terminada em '\n':
//   ESP32 -> Uno:  "M <x> <y>"  mover (x e y de -100 a 100)
//                  "S"          parar
//                  "?"          pedir o motivo do último reset do Uno
//   Uno -> ESP32:  "R,<mcusr>,<NOME>,<uptime_ms>"  motivo do reset do Uno
namespace esp_link {

void begin();
void poll();            // chamar no loop(): lê e executa os comandos recebidos
void sendResetInfo();   // avisa o ESP32 do motivo do último reset

}  // namespace esp_link
