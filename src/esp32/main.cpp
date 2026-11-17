#include <Arduino.h>
#include <esp_system.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>
#include "camera.h"
#include "log.h"

// UART com o Arduino Uno
const int UART_RX = 14;
const int UART_TX = 13;

// Partida escalonada: com alimentação externa o 5 V leva ~1 s para estabilizar. Ligar Wi-Fi e
// câmera nesse intervalo causa brownout (falta de corrente) e o AP nunca aparece.
#ifndef POWER_SETTLE_MS
#define POWER_SETTLE_MS 1000   // espera inicial (a alimentação leva ~1 s para estabilizar)
#endif
#ifndef POWER_STEP_MS
#define POWER_STEP_MS 150      // intervalo entre ligar cada carga
#endif
#ifndef PANTERO_TX_POWER
#define PANTERO_TX_POWER WIFI_POWER_19_5dBm  // potência final do Wi-Fi (reduza se faltar corrente)
#endif

// Motivo do último reset (lido uma vez no boot) e nome legível para log/cliente
const esp_reset_reason_t RESET_REASON = esp_reset_reason();

const char *resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_EXT:       return "EXT";
    case ESP_RST_SW:        return "SOFTWARE";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "UNKNOWN";
  }
}

// Último motivo de reset informado pelo Uno ("U,<mcusr>,<NOME>,<uptime_ms>"), repassado aos clientes
char unoResetMsg[48] = "";

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Mensagens do app: "x,y" com inteiros de -100 a 100 (x: direita +, y: frente +)
// e "V,<mV>" para a tensão do Quick Charge. Repassa ao Uno em linhas "M x y\n" / "V mV\n".
void onWsEvent(AsyncWebSocket *, AsyncWebSocketClient *client, AwsEventType type,
               void *arg, uint8_t *data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    LOG("WS", "cliente #%u conectou de %s (total: %u)", client->id(),
        client->remoteIP().toString().c_str(), (unsigned)ws.count());
    // Informa o motivo do último reset e há quanto tempo o ESP32 está ligado
    char info[48];
    snprintf(info, sizeof(info), "R,%d,%s,%lu", (int)RESET_REASON, resetReasonName(RESET_REASON), millis());
    client->text(info);
    LOG("WS", "TX para #%u: %s", client->id(), info);
    if (unoResetMsg[0]) client->text(unoResetMsg);  // motivo do reset do Uno, se já conhecido
    Serial1.print("?\n");                           // pede ao Uno uma informação atualizada
    return;
  }
  if (type == WS_EVT_DISCONNECT) {
    LOG("WS", "cliente #%u desconectou (restam: %u)", client->id(), (unsigned)ws.count());
    if (ws.count() == 0) {
      Serial1.print("S\n");  // ninguém conectado: para o robô
      LOG("UART", "TX: S (sem clientes)");
    }
    return;
  }
  if (type == WS_EVT_ERROR) {
    LOG("WS", "ERRO no cliente #%u", client->id());
    return;
  }
  if (type != WS_EVT_DATA) return;

  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (!info->final || info->index != 0 || info->len != len || len > 15) {
    LOG("WS", "mensagem ignorada (final=%d index=%llu len=%u)", info->final,
        (unsigned long long)info->index, (unsigned)len);
    return;
  }

  char msg[16];
  memcpy(msg, data, len);
  msg[len] = '\0';

  if (msg[0] == 'V') {
    long mv = atol(msg + 2);
    if (mv > 0) {
      Serial1.printf("V %ld\n", mv);
      LOG("UART", "TX: V %ld", mv);
    }
    return;
  }
  int x, y;
  if (sscanf(msg, "%d,%d", &x, &y) == 2) {
    x = constrain(x, -100, 100);
    y = constrain(y, -100, 100);
    Serial1.printf("M %d %d\n", x, y);
    LOG("UART", "TX: M %d %d (ws: \"%s\")", x, y, msg);
  } else {
    LOG("WS", "mensagem invalida: \"%s\"", msg);
  }
}

// Lista os arquivos recursivamente (mostra também o que está em /assets)
void listDir(const char *path, uint8_t levels) {
  File dir = LittleFS.open(path);
  if (!dir || !dir.isDirectory()) return;

  File f = dir.openNextFile();
  while (f) {
    if (f.isDirectory()) {
      Serial.printf("[DIR]  %s\n", f.path());
      if (levels > 0) listDir(f.path(), levels - 1);
    } else {
      Serial.printf("[FILE] %s (%u bytes)\n", f.path(), (unsigned)f.size());
    }
    f = dir.openNextFile();
  }
}

void setup() {
  Serial.begin(115200);
  delay(POWER_SETTLE_MS);  // deixa a alimentação estabilizar antes de qualquer carga
  LOG("BOOT", "motivo do ultimo reset: %d (%s)", (int)RESET_REASON, resetReasonName(RESET_REASON));
  LOG("BOOT", "Pantero iniciando | chip %s rev %d | %u MHz | flash %u KB | heap livre %u",
      ESP.getChipModel(), (int)ESP.getChipRevision(), (unsigned)ESP.getCpuFreqMHz(),
      (unsigned)(ESP.getFlashChipSize() / 1024), (unsigned)ESP.getFreeHeap());

  // false = NÃO formata se falhar ao montar (evita apagar o filesystem sem querer)
  if (!LittleFS.begin(false)) {
    LOG("FS", "ERRO ao montar LittleFS (rode: pio run -e esp32 -t uploadfs)");
  } else {
    LOG("FS", "LittleFS montado. Arquivos:");
    listDir("/", 3);
    LOG("FS", "Usado: %u / %u bytes",
        (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());
  }

  Serial1.begin(9600, SERIAL_8N1, UART_RX, UART_TX);
  LOG("UART", "Serial1 9600 8N1 (RX=%d, TX=%d)", UART_RX, UART_TX);

  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  // Canal 6 costuma ser mais livre que o 1 (padrão) em casa/laboratório; ajuste se houver interferência
  // 1) Wi-Fi primeiro, com potência de TX baixa (menor pico de corrente)
  WiFi.softAP("Pantero", "12345678", 6);
  WiFi.setSleep(false);                       // sem modem sleep: elimina atrasos de dezenas de ms
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  LOG("BOOT", "Wi-Fi ligado (TX 8.5 dBm); aguardando %d ms antes da camera", POWER_STEP_MS);
  delay(POWER_STEP_MS);

  // 2) Câmera (só o init: o servidor do stream precisa do Wi-Fi já iniciado)
  bool cameraOk = cameraInit();
  if (!cameraOk) LOG("BOOT", "camera INDISPONIVEL: o stream na porta 81 nao sera iniciado");
  delay(POWER_STEP_MS);

  // 3) Sobe a potência do Wi-Fi em degraus até o valor final
  const wifi_power_t steps[] = {WIFI_POWER_11dBm, WIFI_POWER_15dBm, PANTERO_TX_POWER};
  for (wifi_power_t p : steps) {
    WiFi.setTxPower(p);
    LOG("WIFI", "potencia de TX ajustada (codigo %d, em 0,25 dBm)", (int)WiFi.getTxPower());
    delay(POWER_STEP_MS / 2);
  }
  LOG("WIFI", "AP \"Pantero\" ativo, IP: %s", WiFi.softAPIP().toString().c_str());
  WiFi.onEvent([](arduino_event_id_t ev, arduino_event_info_t info) {
    if (ev == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
      const uint8_t *m = info.wifi_ap_staconnected.mac;
      LOG("WIFI", "dispositivo conectou: %02x:%02x:%02x:%02x:%02x:%02x (total: %d)",
          m[0], m[1], m[2], m[3], m[4], m[5], WiFi.softAPgetStationNum());
    } else if (ev == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
      LOG("WIFI", "dispositivo desconectou (total: %d)", WiFi.softAPgetStationNum());
    }
  });

  if (cameraOk) cameraStreamStart();

  // Rota da raiz: sempre envia alguma resposta e diz o que está faltando
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    LOG("HTTP", "GET / de %s", request->client()->remoteIP().toString().c_str());
    if (LittleFS.exists("/index.html.gz")) {
      AsyncWebServerResponse *r = request->beginResponse(LittleFS, "/index.html.gz", "text/html");
      r->addHeader("Content-Encoding", "gzip");
      request->send(r);
    } else if (LittleFS.exists("/index.html")) {
      request->send(LittleFS, "/index.html", "text/html");
    } else {
      request->send(404, "text/plain", "index.html nao esta no filesystem");
    }
  });

  // Demais arquivos (/assets/app.js, css etc.)
  server.serveStatic("/", LittleFS, "/");

  server.onNotFound([](AsyncWebServerRequest *request) {
    LOG("HTTP", "404: %s", request->url().c_str());
    request->send(404, "text/plain", "Nao encontrado");
  });

  server.begin();
  LOG("HTTP", "servidor iniciado na porta 80");
  Serial1.print("?\n");  // pede ao Uno o motivo do último reset
}

// Heartbeat a cada 10 s: ajuda a ver se o ESP32 travou ou está sem memória
// Lê linhas vindas do Uno. "R,<mcusr>,<NOME>,<uptime>" vira "U,..." e é enviado a todos os clientes
void pollUno() {
  static char buf[48];
  static uint8_t len = 0;
  while (Serial1.available()) {
    char c = Serial1.read();
    if (c == '\n') {
      buf[len] = '\0';
      if (len > 0) {
        LOG("UART", "RX do Uno: %s", buf);
        if (buf[0] == 'R' && buf[1] == ',') {
          snprintf(unoResetMsg, sizeof(unoResetMsg), "U,%s", buf + 2);
          ws.textAll(unoResetMsg);
        }
      }
      len = 0;
    } else if (c != '\r' && len < sizeof(buf) - 1) {
      buf[len++] = c;
    }
  }
}

void loop() {
  pollUno();
  ws.cleanupClients();
  static unsigned long lastBeat = 0;
  if (millis() - lastBeat >= 10000) {
    lastBeat = millis();
    LOG("BEAT", "heap livre %u | min %u | wifi clientes %d | ws clientes %u",
        (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
        WiFi.softAPgetStationNum(), (unsigned)ws.count());
  }
}