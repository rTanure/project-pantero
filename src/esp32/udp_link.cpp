#include "udp_link.h"
#include <WiFiUdp.h>
#include "config.h"
#include "log.h"
#include "reset_info.h"
#include "robot_link.h"
#include "video_udp.h"
#include "wifi_ap.h"

namespace udp_link {
namespace {

constexpr size_t MAX_PACKET = 128;

WiFiUDP socket;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;  // protege o endereço do PC (lido pela task de vídeo)

IPAddress clientIp;
uint16_t clientControlPort = 0;  // porta de origem dos pacotes do PC: para onde responder
uint16_t clientVideoPort = 0;    // porta onde o PC recebe o vídeo
unsigned long lastRxAt = 0;
volatile bool active = false;

// Aplica a lista de ajustes "k=v,k=v" enviada pelo PC no olá. "rate" é do Wi-Fi; o resto, do vídeo.
void applySettings(const char *list) {
  char buf[MAX_PACKET];
  strlcpy(buf, list, sizeof(buf));
  char *save = nullptr;
  for (char *token = strtok_r(buf, ",", &save); token; token = strtok_r(nullptr, ",", &save)) {
    char *eq = strchr(token, '=');
    if (!eq) continue;
    *eq = '\0';
    int value = atoi(eq + 1);
    if (!strcmp(token, "rate")) wifi_ap::setFixedRate(value);
    else if (!video_udp::set(token, value)) LOG("UDP", "ajuste desconhecido: %s", token);
  }
}

// Responde ao "olá" com o motivo do reset de cada placa e o estado do vídeo (o PC ignora o que já conhece)
void replyStatus() {
  char info[64];
  reset_info::format(info, sizeof(info));
  sendText(info);
  const char *uno = robot_link::lastUnoReset();
  if (uno[0]) sendText(uno);

  video_udp::Info v = video_udp::info();
  snprintf(info, sizeof(info), "S,%.1f,%d,%d,%d,%d,%d,%d,%lu", v.fps, v.quality, v.frameSize, v.fec, v.paceUs,
           v.camMode, wifi_ap::fixedRate(), v.lostPackets);
  sendText(info);
}

// "H,<porta de video>[,k=v,...]"
void handleHello(IPAddress from, uint16_t fromPort, const char *message) {
  int videoPort = 0;
  if (sscanf(message, "H,%d", &videoPort) != 1 || videoPort <= 0 || videoPort > 65535) {
    LOG("UDP", "olá invalido: \"%s\"", message);
    return;
  }
  if (active && from != clientIp) return;  // já há outro PC conectado

  bool isNew = !active;
  portENTER_CRITICAL(&mux);
  clientIp = from;
  clientControlPort = fromPort;
  clientVideoPort = (uint16_t)videoPort;
  lastRxAt = millis();
  active = true;
  portEXIT_CRITICAL(&mux);

  if (isNew) {
    LOG("UDP", "PC conectou: %s (video na porta %d)", from.toString().c_str(), videoPort);
    robot_link::requestResetInfo();  // pede ao Uno um dado atualizado
  }

  const char *settings = strchr(message + 2, ',');  // depois da porta
  if (settings) applySettings(settings + 1);
  replyStatus();
}

// "P,<n>": o PC mede o tempo de ida e volta do canal de controle. Responde o mesmo texto, na hora.
void handlePing(IPAddress from, const char *message) {
  if (!active || from != clientIp) return;
  lastRxAt = millis();
  sendText(message);
}

void handleCommand(IPAddress from, const char *message) {
  if (!active || from != clientIp) return;  // só aceita comandos do PC registrado
  lastRxAt = millis();

  int x, y;
  if (sscanf(message, "%d,%d", &x, &y) == 2) {
    robot_link::move(x, y);
  } else {
    LOG("UDP", "mensagem invalida: \"%s\"", message);
  }
}

}  // namespace

void begin() {
  socket.begin(UDP_CONTROL_PORT);
  LOG("UDP", "ouvindo na porta %u", (unsigned)UDP_CONTROL_PORT);
}

void poll() {
  char buf[MAX_PACKET];
  for (int size = socket.parsePacket(); size > 0; size = socket.parsePacket()) {
    int n = socket.read(buf, sizeof(buf) - 1);
    if (n <= 0) continue;
    buf[n] = '\0';
    if (buf[0] == 'H') handleHello(socket.remoteIP(), socket.remotePort(), buf);
    else if (buf[0] == 'P') handlePing(socket.remoteIP(), buf);
    else handleCommand(socket.remoteIP(), buf);
  }

  if (active && millis() - lastRxAt > CLIENT_TIMEOUT_MS) {
    active = false;
    LOG("UDP", "PC sem sinal ha %lu ms: parando o robo e o video", (unsigned long)CLIENT_TIMEOUT_MS);
    robot_link::stop();
  }
}

bool clientActive() { return active; }

bool clientTarget(IPAddress *ip, uint16_t *videoPort) {
  if (!active) return false;
  portENTER_CRITICAL(&mux);
  *ip = clientIp;
  *videoPort = clientVideoPort;
  portEXIT_CRITICAL(&mux);
  return true;
}

void sendText(const char *message) {
  if (!active) return;
  IPAddress ip;
  uint16_t port;
  portENTER_CRITICAL(&mux);
  ip = clientIp;
  port = clientControlPort;
  portEXIT_CRITICAL(&mux);

  socket.beginPacket(ip, port);
  socket.print(message);
  socket.endPacket();
}

}  // namespace udp_link
