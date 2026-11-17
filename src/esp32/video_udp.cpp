#include "video_udp.h"
#include <WiFiUdp.h>
#include <esp_camera.h>
#include "camera.h"
#include "config.h"
#include "log.h"
#include "udp_link.h"

namespace video_udp {
namespace {

constexpr int VIDEO_CORE = 1;  // núcleo 0 fica com o Wi-Fi, a pilha TCP/IP e o driver da câmera
constexpr size_t HEADER_BYTES = 8;
constexpr unsigned long FPS_WINDOW_MS = 2000;
constexpr unsigned long STATS_MS = 5000;

// Ajustes vindos do PC. Os valores iniciais são os padrões; o PC os reenvia a cada "olá".
struct Settings {
  volatile int quality = 15;
  volatile int frameSize = 2;
  volatile int fec = 0;
  volatile int paceUs = 4000;  // medido: atraso p95 de ~160 para ~30 ms (ver README)
  volatile int camMode = 0;
} cfg;
volatile bool cameraDirty = false;  // modo da câmera mudou: a task reinicia a câmera

WiFiUDP socket;  // só envia
uint16_t frameId = 0;
volatile float currentFps = 0;
volatile unsigned long lostPackets = 0;

framesize_t toFrameSize(int fs) {
  switch (fs) {
    case 0:  return FRAMESIZE_QQVGA;  // 160x120
    case 1:  return FRAMESIZE_HQVGA;  // 240x176
    default: return FRAMESIZE_QVGA;   // 320x240
  }
}

// Cabeçalho do pacote (ver video_udp.h)
void makeHeader(uint8_t *h, uint8_t index, uint8_t count, uint32_t ts) {
  h[0] = frameId >> 8;
  h[1] = frameId;
  h[2] = index;
  h[3] = count;
  h[4] = ts >> 24;
  h[5] = ts >> 16;
  h[6] = ts >> 8;
  h[7] = ts;
}

bool sendPacket(IPAddress ip, uint16_t port, const uint8_t *header, const uint8_t *a, size_t aLen,
                const uint8_t *b = nullptr, size_t bLen = 0) {
  socket.beginPacket(ip, port);
  socket.write(header, HEADER_BYTES);
  if (aLen) socket.write(a, aLen);
  if (bLen) socket.write(b, bLen);
  return socket.endPacket();
}

// Envia um quadro em pacotes UDP. Pacote que não saiu é perda: não há retransmissão.
// Retorna quantos pacotes não saíram.
int sendFrame(const uint8_t *data, size_t len, uint32_t ts, IPAddress ip, uint16_t port) {
  size_t count = (len + VIDEO_CHUNK_BYTES - 1) / VIDEO_CHUNK_BYTES;
  if (count == 0 || count > 254) return (int)count;  // não cabe no cabeçalho (JPEG gigante)

  const bool fec = cfg.fec;
  const int paceUs = cfg.paceUs;
  uint8_t parity[VIDEO_CHUNK_BYTES];
  if (fec) memset(parity, 0, sizeof(parity));

  int failed = 0;
  uint8_t header[HEADER_BYTES];
  for (size_t i = 0; i < count; i++) {
    size_t offset = i * VIDEO_CHUNK_BYTES;
    size_t n = min((size_t)VIDEO_CHUNK_BYTES, len - offset);
    makeHeader(header, (uint8_t)i, (uint8_t)count, ts);
    if (!sendPacket(ip, port, header, data + offset, n)) failed++;
    if (fec) {
      for (size_t k = 0; k < n; k++) parity[k] ^= data[offset + k];
    }
    // Espaça os pacotes com ESPERA OCUPADA, de propósito: nos testes, vTaskDelay (que cede a CPU) perdeu todo o
    // ganho, e só a espera ocupada reduziu o atraso (p95 de ~160 para ~60 ms) e o ping do controle (de ~60 para ~25 ms).
    if (paceUs > 0 && i + 1 < count) delayMicroseconds(paceUs);
  }

  if (fec) {  // pacote de paridade: tamanho total do JPEG + XOR dos pedaços
    uint8_t lenBytes[4] = {(uint8_t)(len >> 24), (uint8_t)(len >> 16), (uint8_t)(len >> 8), (uint8_t)len};
    makeHeader(header, (uint8_t)count, (uint8_t)count, ts);
    if (!sendPacket(ip, port, header, lenBytes, 4, parity, VIDEO_CHUNK_BYTES)) failed++;
  }
  frameId++;
  return failed;
}

// Aplica no sensor a qualidade e a resolução pedidas, se mudaram. Retorna true se a resolução mudou
// (o quadro que estava no buffer tem o tamanho antigo e deve ser descartado).
bool applySensor(int &appliedQuality, int &appliedFrameSize) {
  sensor_t *sensor = esp_camera_sensor_get();
  if (!sensor) return false;
  bool resized = false;
  if (appliedQuality != cfg.quality) {
    sensor->set_quality(sensor, cfg.quality);
    appliedQuality = cfg.quality;
  }
  if (appliedFrameSize != cfg.frameSize) {
    sensor->set_framesize(sensor, toFrameSize(cfg.frameSize));
    appliedFrameSize = cfg.frameSize;
    resized = true;
  }
  return resized;
}

// Captura e envia em sequência, sem fila: quadro novo só depois do anterior ter saído.
void captureTask(void *) {
  unsigned long sent = 0, statsAt = millis(), lostAtStats = 0;
  unsigned long windowSent = 0, windowAt = millis();
  int flush = 1;  // quadros a descartar (o buffer pode guardar um quadro velho)
  int appliedQuality = -1, appliedFrameSize = -1;

  for (;;) {
    IPAddress ip;
    uint16_t port;
    if (!udp_link::clientTarget(&ip, &port)) {  // sem PC: não gasta CPU
      currentFps = 0;
      flush = 1;
      vTaskDelay(pdMS_TO_TICKS(50));
      continue;
    }

    if (cameraDirty) {  // o modo da câmera mudou: reinicia com o novo modo
      cameraDirty = false;
      esp_camera_deinit();
      bool ok = cameraInit((CameraMode)(int)cfg.camMode);
      LOG("VIDEO", "camera reiniciada no modo %d: %s", (int)cfg.camMode, ok ? "ok" : "ERRO");
      appliedQuality = appliedFrameSize = -1;
      flush = 1;
      if (!ok) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        cameraDirty = true;  // tenta de novo
        continue;
      }
    }
    if (applySensor(appliedQuality, appliedFrameSize)) flush = 2;

    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      LOG("VIDEO", "ERRO: esp_camera_fb_get retornou NULL");
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    if (flush > 0) {
      flush--;
      esp_camera_fb_return(fb);
      continue;
    }

    uint32_t ts = millis();  // a captura acabou de terminar
    lostPackets += sendFrame(fb->buf, fb->len, ts, ip, port);
    esp_camera_fb_return(fb);
    sent++;
    windowSent++;

    if (millis() - windowAt >= FPS_WINDOW_MS) {
      currentFps = windowSent * 1000.0f / (millis() - windowAt);
      windowSent = 0;
      windowAt = millis();
    }
    if (millis() - statsAt >= STATS_MS) {
      LOG("VIDEO", "%.1f fps enviados, %lu pacotes nao saíram, q=%d fs=%d fec=%d pace=%d cam=%d, heap %u",
          sent * 1000.0 / (millis() - statsAt), (unsigned long)lostPackets - lostAtStats, (int)cfg.quality,
          (int)cfg.frameSize, (int)cfg.fec, (int)cfg.paceUs, (int)cfg.camMode, (unsigned)ESP.getFreeHeap());
      sent = 0;
      lostAtStats = lostPackets;
      statsAt = millis();
    }
  }
}

}  // namespace

void begin() {
  xTaskCreatePinnedToCore(captureTask, "video-udp", 4096, nullptr, 6, nullptr, VIDEO_CORE);
  LOG("VIDEO", "video por UDP (captura e envio no nucleo %d, pacotes de %u bytes)", VIDEO_CORE,
      (unsigned)VIDEO_CHUNK_BYTES);
}

bool set(const char *key, int value) {
  int before;
  if (!strcmp(key, "q")) {
    before = cfg.quality;
    cfg.quality = constrain(value, 10, 63);
  } else if (!strcmp(key, "fs")) {
    before = cfg.frameSize;
    cfg.frameSize = constrain(value, 0, 2);
  } else if (!strcmp(key, "fec")) {
    before = cfg.fec;
    cfg.fec = value ? 1 : 0;
  } else if (!strcmp(key, "pace")) {
    before = cfg.paceUs;
    cfg.paceUs = constrain(value, 0, 5000);
  } else if (!strcmp(key, "cam")) {
    before = cfg.camMode;
    cfg.camMode = value ? 1 : 0;
    if (cfg.camMode != before) cameraDirty = true;
  } else {
    return false;
  }
  int now = !strcmp(key, "q") ? cfg.quality : !strcmp(key, "fs") ? cfg.frameSize : !strcmp(key, "fec") ? cfg.fec
          : !strcmp(key, "pace") ? cfg.paceUs : cfg.camMode;
  if (now != before) LOG("VIDEO", "ajuste %s: %d -> %d", key, before, now);
  return true;
}

Info info() {
  return {currentFps, cfg.quality, cfg.frameSize, cfg.fec, cfg.paceUs, cfg.camMode, lostPackets};
}

float fps() { return currentFps; }

}  // namespace video_udp
