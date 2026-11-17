#pragma once
#include <ESPAsyncWebServer.h>

// Vídeo da câmera por WebSocket binário (/wsv): cada mensagem é um quadro JPEG completo.
// Uma task no núcleo 1 captura e envia; só roda enquanto houver algum cliente conectado.
// A fila de cada cliente usa o padrão da biblioteca (32 mensagens). Se a fila encher, ou se a
// memória livre ficar abaixo do mínimo, o quadro é descartado em vez de enfileirado.
namespace video_ws {

void begin(AsyncWebServer &server);  // registra /wsv e inicia a task de captura
void cleanup();                      // chamar no loop(): remove clientes desconectados
size_t clients();
float fps();  // quadros por segundo enviados (média dos últimos ~2 s; 0 sem clientes)

}  // namespace video_ws
