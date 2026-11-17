#pragma once
#include <ESPAsyncWebServer.h>

// Serve o app React gravado no LittleFS (pasta data/, gerada por "npm run deploy" em app/).
namespace web_server {

// Monta o LittleFS e lista os arquivos. Não formata se falhar (evita apagar dados sem querer).
bool mountFilesystem();

// Rotas: "/" (index.html.gz), arquivos estáticos e 404. O servidor é iniciado pelo main.
void begin(AsyncWebServer &server);

}  // namespace web_server
