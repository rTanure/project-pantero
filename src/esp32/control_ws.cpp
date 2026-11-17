#include "control_ws.h"
#include "log.h"
#include "reset_info.h"
#include "robot_link.h"

namespace control_ws {
namespace {

AsyncWebSocket socket("/ws");

// Ao conectar: informa o motivo do reset do ESP32 e do Uno, e pede ao Uno um dado atualizado
void onConnect(AsyncWebSocketClient *client) {
  LOG("WS", "cliente #%u conectou de %s (total: %u)", (unsigned)client->id(),
      client->remoteIP().toString().c_str(), (unsigned)socket.count());

  char info[48];
  reset_info::format(info, sizeof(info));
  client->text(info);
  LOG("WS", "TX para #%u: %s", (unsigned)client->id(), info);

  const char *uno = robot_link::lastUnoReset();
  if (uno[0]) client->text(uno);
  robot_link::requestResetInfo();
}

void onDisconnect(AsyncWebSocketClient *client) {
  LOG("WS", "cliente #%u desconectou (restam: %u)", (unsigned)client->id(), (unsigned)socket.count());
  if (socket.count() == 0) robot_link::stop();  // ninguém no controle: para o robô
}

// Mensagem do app: "x,y"
void onMessage(const uint8_t *data, size_t len) {
  char msg[16];
  if (len >= sizeof(msg)) {
    LOG("WS", "mensagem ignorada (tamanho %u)", (unsigned)len);
    return;
  }
  memcpy(msg, data, len);
  msg[len] = '\0';

  int x, y;
  if (sscanf(msg, "%d,%d", &x, &y) == 2) {
    robot_link::move(x, y);
  } else {
    LOG("WS", "mensagem invalida: \"%s\"", msg);
  }
}

void onEvent(AsyncWebSocket *, AsyncWebSocketClient *client, AwsEventType type, void *arg,
             uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      onConnect(client);
      break;
    case WS_EVT_DISCONNECT:
      onDisconnect(client);
      break;
    case WS_EVT_ERROR:
      LOG("WS", "ERRO no cliente #%u", (unsigned)client->id());
      break;
    case WS_EVT_DATA: {
      auto *info = (AwsFrameInfo *)arg;
      if (info->final && info->index == 0 && info->len == len) onMessage(data, len);
      break;
    }
    default:
      break;
  }
}

}  // namespace

void begin(AsyncWebServer &server) {
  socket.onEvent(onEvent);
  server.addHandler(&socket);
}

void broadcast(const char *message) { socket.textAll(message); }

void cleanup() { socket.cleanupClients(); }

size_t clients() { return socket.count(); }

}  // namespace control_ws
