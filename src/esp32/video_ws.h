#pragma once
#include <ESPAsyncWebServer.h>

// Vídeo da câmera por WebSocket binário (/wsv): cada mensagem é um quadro JPEG completo.
// Uma task no núcleo 1 alterna capturar e transmitir (a câmera fica parada enquanto o quadro sai); só
// roda enquanto houver algum cliente conectado.
// Sem buffer: a fila de cada cliente tem 1 mensagem (WS_MAX_QUEUED_MESSAGES=1). Se o quadro anterior
// ainda não foi entregue, o novo é descartado em vez de enfileirado: perdeu, perdeu.
namespace video_ws {

void begin(AsyncWebServer &server);  // registra /wsv e inicia a task de captura
void cleanup();                      // chamar no loop(): remove clientes desconectados
size_t clients();
float fps();  // quadros por segundo enviados (média dos últimos ~2 s; 0 sem clientes)

}  // namespace video_ws
