#pragma once

// Câmera OV2640 (pinos da ESP-WROVER-KIT), em JPEG QVGA. Quadros são lidos com esp_camera_fb_get().
// Modo dos buffers:
//   WhenEmpty1  1 buffer e o driver só captura quando ele está livre (quadro sempre recém-capturado)
//   Latest2     2 buffers e o driver captura sempre, entregando o quadro mais recente
// Retorna false se a câmera não iniciar ou não entregar um quadro de teste.
enum class CameraMode : unsigned char { WhenEmpty1 = 0, Latest2 = 1 };

bool cameraInit(CameraMode mode = CameraMode::WhenEmpty1);
