#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <ESPAsyncWebServer.h>

AsyncWebServer server(80);

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
  delay(1000);

  // false = NÃO formata se falhar ao montar (evita apagar o filesystem sem querer)
  if (!LittleFS.begin(false)) {
    Serial.println("Falha ao montar LittleFS (rode: pio run -e esp32 -t uploadfs)");
  } else {
    Serial.println("LittleFS montado. Arquivos:");
    listDir("/", 3);
    Serial.printf("Usado: %u / %u bytes\n",
                  (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());
  }

  WiFi.softAP("Pantero", "12345678");
  Serial.print("IP do AP: ");
  Serial.println(WiFi.softAPIP());   // normalmente 192.168.4.1

  // Rota da raiz: sempre envia alguma resposta e diz o que está faltando
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
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
    Serial.printf("404: %s\n", request->url().c_str());
    request->send(404, "text/plain", "Nao encontrado");
  });

  server.begin();
  Serial.println("Servidor iniciado");
}

void loop() {}