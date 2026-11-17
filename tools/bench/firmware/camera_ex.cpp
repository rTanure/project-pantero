// Kit de testes: init da câmera parametrizável (fb_count, grab_mode, xclk, local do frame buffer).
// Declaração: ver testbed.h. Copiar junto com os demais arquivos desta pasta para src/esp32/.
#include "log.h"
#include <Arduino.h>
#include "esp_camera.h"

// Pinos da câmera na ESP-WROVER-KIT (OV2640)
#define CAM_PIN_PWDN  -1
#define CAM_PIN_RESET -1
#define CAM_PIN_XCLK  21
#define CAM_PIN_SIOD  26
#define CAM_PIN_SIOC  27
#define CAM_PIN_D7    35
#define CAM_PIN_D6    34
#define CAM_PIN_D5    39
#define CAM_PIN_D4    36
#define CAM_PIN_D3    19
#define CAM_PIN_D2    18
#define CAM_PIN_D1     5
#define CAM_PIN_D0     4
#define CAM_PIN_VSYNC 25
#define CAM_PIN_HREF  23
#define CAM_PIN_PCLK  22

// Ajustes de desempenho do stream (menos pixels/qualidade = menos latência)
#ifndef CAM_FRAME_SIZE
#define CAM_FRAME_SIZE FRAMESIZE_QVGA  // QQVGA 160x120, QVGA 320x240, VGA 640x480
#endif
#ifndef CAM_JPEG_QUALITY
#define CAM_JPEG_QUALITY 15            // 10 (melhor) a 63 (pior); maior = frame menor e mais rápido
#endif

// Versão parametrizável (usada pelo banco de testes); 0/-1 = valor padrão
bool cameraInitEx(int fbCountOverride, int grabOverride, int xclkOverride, int fbLocOverride) {
  camera_config_t c = {};
  c.ledc_channel = LEDC_CHANNEL_0;
  c.ledc_timer = LEDC_TIMER_0;
  c.pin_d0 = CAM_PIN_D0;
  c.pin_d1 = CAM_PIN_D1;
  c.pin_d2 = CAM_PIN_D2;
  c.pin_d3 = CAM_PIN_D3;
  c.pin_d4 = CAM_PIN_D4;
  c.pin_d5 = CAM_PIN_D5;
  c.pin_d6 = CAM_PIN_D6;
  c.pin_d7 = CAM_PIN_D7;
  c.pin_xclk = CAM_PIN_XCLK;
  c.pin_pclk = CAM_PIN_PCLK;
  c.pin_vsync = CAM_PIN_VSYNC;
  c.pin_href = CAM_PIN_HREF;
  c.pin_sccb_sda = CAM_PIN_SIOD;
  c.pin_sccb_scl = CAM_PIN_SIOC;
  c.pin_pwdn = CAM_PIN_PWDN;
  c.pin_reset = CAM_PIN_RESET;
  c.xclk_freq_hz = 20000000;
  c.pixel_format = PIXFORMAT_JPEG;
  c.frame_size = CAM_FRAME_SIZE;
  c.jpeg_quality = CAM_JPEG_QUALITY;
  c.grab_mode = CAMERA_GRAB_LATEST;

  // Sem PSRAM (ou sem -DBOARD_HAS_PSRAM) o frame buffer precisa ficar na DRAM, com 1 buffer
  bool psram = psramFound();
  LOG("CAM", "PSRAM: %s (livre: %u bytes) | heap livre: %u bytes",
      psram ? "sim" : "NAO", (unsigned)ESP.getFreePsram(), (unsigned)ESP.getFreeHeap());
  if (psram) {
    c.fb_count = 2;
    c.fb_location = CAMERA_FB_IN_PSRAM;
  } else {
    c.fb_count = 1;
    c.fb_location = CAMERA_FB_IN_DRAM;
    LOG("CAM", "usando DRAM com 1 frame buffer (qualidade/FPS menores)");
  }

  if (fbCountOverride > 0) c.fb_count = fbCountOverride;
  if (grabOverride >= 0) c.grab_mode = (camera_grab_mode_t)grabOverride;
  if (xclkOverride > 0) c.xclk_freq_hz = xclkOverride;
  if (fbLocOverride == 0) c.fb_location = CAMERA_FB_IN_DRAM;
  if (fbLocOverride == 1) c.fb_location = CAMERA_FB_IN_PSRAM;

  LOG("CAM", "iniciando: frame_size=%d JPEG q=%d xclk=%lu Hz fb_count=%d",
      (int)c.frame_size, c.jpeg_quality, (unsigned long)c.xclk_freq_hz, (int)c.fb_count);
  esp_err_t err = esp_camera_init(&c);
  if (err != ESP_OK) {
    LOG("CAM", "ERRO esp_camera_init: 0x%x (%s). Confira o modelo da placa/pinos da camera",
        err, esp_err_to_name(err));
    return false;
  }

  sensor_t *sensor = esp_camera_sensor_get();
  if (sensor) {
    LOG("CAM", "sensor detectado: PID=0x%02x (OV2640=0x26) ", sensor->id.PID);
  } else {
    LOG("CAM", "ERRO: sensor nao encontrado apos init");
  }

  // Teste de captura: confirma que a camera realmente entrega frames
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    LOG("CAM", "ERRO: init OK mas a captura de teste falhou (fb_get retornou NULL)");
    return false;
  }
  LOG("CAM", "captura de teste OK: %ux%u, %u bytes", fb->width, fb->height, (unsigned)fb->len);
  esp_camera_fb_return(fb);
  return true;
}

bool cameraInit() { return cameraInitEx(0, -1, 0, -1); }  // usado pelo main.cpp do kit
