#pragma once

// Inicializa a OV2640 (pinos da ESP-WROVER-KIT). Retorna false se falhar.
bool cameraInit();

// Sobe o stream MJPEG em http://<ip>:81/stream (servidor httpd separado do da porta 80)
void cameraStreamStart();
