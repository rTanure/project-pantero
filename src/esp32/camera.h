#pragma once

// Câmera OV2640 (pinos da ESP-WROVER-KIT), em JPEG QVGA. Quadros são lidos com esp_camera_fb_get().
// Retorna false se a câmera não iniciar ou não entregar um quadro de teste.
bool cameraInit();
