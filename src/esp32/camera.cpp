#include "camera.h"
#include "log.h"
#include <Arduino.h>
#include <esp_camera.h>

namespace {

// Pinos da câmera na ESP-WROVER-KIT (OV2640)
constexpr int PIN_PWDN = -1;
constexpr int PIN_RESET = -1;
constexpr int PIN_XCLK = 21;
constexpr int PIN_SIOD = 26;
constexpr int PIN_SIOC = 27;
constexpr int PIN_D7 = 35;
constexpr int PIN_D6 = 34;
constexpr int PIN_D5 = 39;
constexpr int PIN_D4 = 36;
constexpr int PIN_D3 = 19;
constexpr int PIN_D2 = 18;
constexpr int PIN_D1 = 5;
constexpr int PIN_D0 = 4;
constexpr int PIN_VSYNC = 25;
constexpr int PIN_HREF = 23;
constexpr int PIN_PCLK = 22;

// Imagem: menos pixels e mais compressão = menos bytes por quadro
constexpr framesize_t FRAME_SIZE = FRAMESIZE_QVGA;  // 320x240
constexpr int JPEG_QUALITY = 15;                    // 10 (melhor) a 63 (pior)
constexpr int XCLK_HZ = 20000000;

camera_config_t buildConfig(CameraMode mode) {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer = LEDC_TIMER_0;
  c.pin_d0 = PIN_D0;
  c.pin_d1 = PIN_D1;
  c.pin_d2 = PIN_D2;
  c.pin_d3 = PIN_D3;
  c.pin_d4 = PIN_D4;
  c.pin_d5 = PIN_D5;
  c.pin_d6 = PIN_D6;
  c.pin_d7 = PIN_D7;
  c.pin_xclk = PIN_XCLK;
  c.pin_pclk = PIN_PCLK;
  c.pin_vsync = PIN_VSYNC;
  c.pin_href = PIN_HREF;
  c.pin_sccb_sda = PIN_SIOD;
  c.pin_sccb_scl = PIN_SIOC;
  c.pin_pwdn = PIN_PWDN;
  c.pin_reset = PIN_RESET;
  c.xclk_freq_hz = XCLK_HZ;
  c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size = FRAME_SIZE;
  c.jpeg_quality = JPEG_QUALITY;
  if (mode == CameraMode::Latest2) {
    // 2 buffers: o driver captura sempre e entrega o quadro mais recente
    c.grab_mode = CAMERA_GRAB_LATEST;
    c.fb_count = 2;
  } else {
    // Captura e envio em sequência: 1 buffer, e o driver só captura quando ele está livre. O video_udp
    // só devolve o buffer depois de enviar o quadro, então não há quadro antigo esperando em fila.
    c.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
    c.fb_count = 1;
  }
  c.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;  // sem -DBOARD_HAS_PSRAM: DRAM
  if (!psramFound()) LOG("CAM", "sem PSRAM: frame buffer na DRAM");
  return c;
}

}  // namespace

bool cameraInit(CameraMode mode) {
  camera_config_t config = buildConfig(mode);
  LOG("CAM", "iniciando: JPEG q=%d, xclk=%d Hz, modo %s (%d buffer), PSRAM livre=%u bytes", JPEG_QUALITY, XCLK_HZ,
      mode == CameraMode::Latest2 ? "quadro mais recente" : "captura so com buffer livre", (int)config.fb_count,
      (unsigned)ESP.getFreePsram());

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    LOG("CAM", "ERRO esp_camera_init: 0x%x (%s). Confira o modelo da placa e os pinos", err,
        esp_err_to_name(err));
    return false;
  }

  // Captura de teste: confirma que a câmera realmente entrega quadros
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    LOG("CAM", "ERRO: init OK, mas a captura de teste falhou");
    return false;
  }
  LOG("CAM", "captura de teste OK: %ux%u, %u bytes", fb->width, fb->height, (unsigned)fb->len);
  esp_camera_fb_return(fb);
  return true;
}
