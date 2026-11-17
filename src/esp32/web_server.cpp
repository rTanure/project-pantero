#include "web_server.h"
#include "log.h"
#include <LittleFS.h>

namespace web_server {
namespace {

// Lista os arquivos recursivamente (mostra também o que está em /assets)
void listDir(const char *path, uint8_t levels) {
  File dir = LittleFS.open(path);
  if (!dir || !dir.isDirectory()) return;

  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (f.isDirectory()) {
      LOG("FS", "[DIR]  %s", f.path());
      if (levels > 0) listDir(f.path(), levels - 1);
    } else {
      LOG("FS", "[FILE] %s (%u bytes)", f.path(), (unsigned)f.size());
    }
  }
}

// Raiz: sempre responde algo e diz o que está faltando
void sendIndex(AsyncWebServerRequest *request) {
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
}

}  // namespace

bool mountFilesystem() {
  if (!LittleFS.begin(false)) {
    LOG("FS", "ERRO ao montar LittleFS (rode: pio run -e esp32 -t uploadfs)");
    return false;
  }
  LOG("FS", "LittleFS montado. Arquivos:");
  listDir("/", 3);
  LOG("FS", "Usado: %u / %u bytes", (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());
  return true;
}

void begin(AsyncWebServer &server) {
  server.on("/", HTTP_GET, sendIndex);
  server.serveStatic("/", LittleFS, "/");  // /assets/app.js, css etc.
  server.onNotFound([](AsyncWebServerRequest *request) {
    LOG("HTTP", "404: %s", request->url().c_str());
    request->send(404, "text/plain", "Nao encontrado");
  });
}

}  // namespace web_server
