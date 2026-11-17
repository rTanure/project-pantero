#pragma once
#include <IPAddress.h>

// Enlace UDP com o programa do PC (pasta pc/). O PC se conecta ao Wi-Fi do robô e fala UDP:
//   PC -> ESP32 (porta UDP_CONTROL_PORT):
//       "H,<porta>[,k=v,...]"  "olá" a cada ~1 s: registra o PC e a porta UDP onde ele quer receber o
//                              vídeo. Os ajustes k=v (q, fs, fec, pace, cam, rate) são reaplicados a
//                              cada olá, então uma perda de pacote não deixa o robô desajustado
//       "x,y"                  comando de movimento, inteiros de -100 a 100 (x: direita +, y: frente +)
//       "P,<n>"                ping do canal de controle: o ESP32 devolve o mesmo texto
//   ESP32 -> PC (de volta ao endereço de onde veio o "H"):
//       "R,<codigo>,<NOME>,<uptime_ms>"  motivo do último reset do ESP32
//       "U,<codigo>,<NOME>,<uptime_ms>"  motivo do último reset do Arduino Uno
//       "S,<fps>,<q>,<fs>,<fec>,<pace>,<cam>,<rate>,<perdidos>"  estado do vídeo e do rádio
// Só um PC por vez. Sem pacote do PC por CLIENT_TIMEOUT_MS, o cliente é dado como perdido: o robô
// é parado e o vídeo deixa de ser enviado.
namespace udp_link {

void begin();
void poll();  // chamar no loop(): lê os pacotes do PC e confere o timeout

bool clientActive();                                      // há um PC conectado
bool clientTarget(IPAddress *ip, uint16_t *videoPort);   // para onde enviar o vídeo (false se não há PC)
void sendText(const char *message);                       // texto ao PC (ignorado se não há PC)

}  // namespace udp_link
