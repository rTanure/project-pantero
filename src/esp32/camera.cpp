#include "camera.h"
#include "log.h"
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <Arduino.h>
#include "esp_camera.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"

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

static const char *STREAM_CT = "multipart/x-mixed-replace;boundary=frame";
static const char *STREAM_BOUNDARY = "\r\n--frame\r\n";

bool cameraInit() {
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

// ---------------------------------------------------------------------------
// Pipeline de vídeo em dois estágios, ambos no núcleo 1 (o Wi-Fi e o servidor web
// ficam no núcleo 0):
//   captureTask  -> lê a câmera continuamente e guarda SÓ o frame mais recente
//   streamHandler-> copia o frame mais novo e envia; se o cliente for lento, os frames
//                   intermediários são descartados (sem fila = sem latência acumulada)
// ---------------------------------------------------------------------------
static const int VIDEO_CORE = 1;

struct SharedFrame {
  uint8_t *buf = nullptr;
  size_t len = 0;
  size_t cap = 0;
  volatile uint32_t seq = 0;  // incrementa a cada frame publicado
};
static SharedFrame shared;
static SemaphoreHandle_t sharedMutex = nullptr;
static volatile int activeClients = 0;

// Buffer em PSRAM quando houver; senão na RAM normal
static uint8_t *allocFrameBuf(size_t n) {
  uint8_t *b = (uint8_t *)heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return b ? b : (uint8_t *)malloc(n);
}

static void captureTask(void *) {
  unsigned long frames = 0, lastLogAt = millis();
  for (;;) {
    if (activeClients == 0) {  // ninguém assistindo: não gasta CPU nem energia
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      LOG("CAPTURE", "ERRO: esp_camera_fb_get retornou NULL");
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    xSemaphoreTake(sharedMutex, portMAX_DELAY);
    if (fb->len > shared.cap) {
      free(shared.buf);
      shared.cap = fb->len + 4096;  // folga para frames maiores
      shared.buf = allocFrameBuf(shared.cap);
      if (!shared.buf) {
        shared.cap = 0;
        LOG("CAPTURE", "ERRO: sem memoria para o frame de %u bytes", (unsigned)fb->len);
      }
    }
    if (shared.buf) {
      memcpy(shared.buf, fb->buf, fb->len);
      shared.len = fb->len;
      shared.seq = shared.seq + 1;
    }
    xSemaphoreGive(sharedMutex);
    esp_camera_fb_return(fb);

    frames++;
    if (millis() - lastLogAt >= 5000) {
      LOG("CAPTURE", "%.1f fps capturados (nucleo %d)", frames * 1000.0 / (millis() - lastLogAt),
          xPortGetCoreID());
      frames = 0;
      lastLogAt = millis();
    }
  }
}

// GET /stream -> MJPEG (use direto em <img src="http://IP:81/stream">)
static esp_err_t streamHandler(httpd_req_t *req) {
  LOG("STREAM", "cliente conectou (uri=%s, nucleo %d)", req->uri, xPortGetCoreID());
  // Desliga o Nagle: sem isso pacotes pequenos esperam ACK e o vídeo "engasga"
  int one = 1;
  setsockopt(httpd_req_to_sockfd(req), IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

  httpd_resp_set_type(req, STREAM_CT);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  __atomic_add_fetch(&activeClients, 1, __ATOMIC_SEQ_CST);

  uint8_t *out = nullptr;  // cópia local: o envio não segura a captura
  size_t outCap = 0;
  char part[128];
  uint32_t lastSeq = shared.seq;
  unsigned long sent = 0, dropped = 0, bytes = 0, startedAt = millis(), lastLogAt = millis();
  esp_err_t res = ESP_OK;

  while (res == ESP_OK) {
    // espera um frame mais novo que o último enviado
    unsigned long waitStart = millis();
    while (shared.seq == lastSeq) {
      if (millis() - waitStart > 3000) {
        LOG("STREAM", "ERRO: sem frames novos ha 3 s, encerrando stream");
        res = ESP_FAIL;
        break;
      }
      vTaskDelay(1);
    }
    if (res != ESP_OK) break;

    xSemaphoreTake(sharedMutex, portMAX_DELAY);
    size_t len = shared.len;
    uint32_t seq = shared.seq;
    if (len > outCap) {
      free(out);
      outCap = len + 4096;
      out = allocFrameBuf(outCap);
    }
    if (out) memcpy(out, shared.buf, len);
    xSemaphoreGive(sharedMutex);
    if (!out) {
      LOG("STREAM", "ERRO: sem memoria para o buffer de envio");
      res = ESP_FAIL;
      break;
    }

    dropped += (seq - lastSeq) - 1;  // frames que ficaram para trás por rede lenta
    lastSeq = seq;

    // boundary + cabeçalho em um único envio (menos pacotes por frame)
    size_t hlen = snprintf(part, sizeof(part),
                           "%sContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
                           STREAM_BOUNDARY, (unsigned)len);
    res = httpd_resp_send_chunk(req, part, hlen);
    if (res == ESP_OK) res = httpd_resp_send_chunk(req, (const char *)out, len);
    if (res != ESP_OK) break;

    sent++;
    bytes += len;
    if (millis() - lastLogAt >= 5000) {  // resumo a cada 5 s
      LOG("STREAM", "%lu enviados, %lu descartados, %.1f fps, %.1f KB/s, heap livre %u",
          sent, dropped, sent * 1000.0 / (millis() - startedAt),
          bytes / 1024.0 / ((millis() - startedAt) / 1000.0), (unsigned)ESP.getFreeHeap());
      lastLogAt = millis();
    }
  }

  LOG("STREAM", "cliente saiu (%s) apos %lu frames em %lu ms", esp_err_to_name(res), sent,
      millis() - startedAt);
  free(out);
  __atomic_sub_fetch(&activeClients, 1, __ATOMIC_SEQ_CST);
  return ESP_OK;
}

// Servidor separado na porta 81 para não bloquear o servidor principal (porta 80)
void cameraStreamStart() {
  sharedMutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(captureTask, "cam-capture", 4096, nullptr, 6, nullptr, VIDEO_CORE);

  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.server_port = 81;
  cfg.ctrl_port += 1;       // evita conflito com a porta de controle de outro httpd
  cfg.core_id = VIDEO_CORE; // envio no mesmo núcleo da captura
  cfg.stack_size = 6144;
  cfg.max_open_sockets = 3; // menos sockets = mais memória livre para o vídeo

  httpd_uri_t uri = {"/stream", HTTP_GET, streamHandler, nullptr};
  httpd_handle_t h = nullptr;
  esp_err_t err = httpd_start(&h, &cfg);
  if (err != ESP_OK) {
    LOG("STREAM", "ERRO httpd_start na porta 81: 0x%x (%s)", err, esp_err_to_name(err));
    return;
  }
  httpd_register_uri_handler(h, &uri);
  LOG("STREAM", "servidor MJPEG em :81/stream (captura e envio no nucleo %d)", VIDEO_CORE);
}
