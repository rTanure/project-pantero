#ifdef TESTBED
// Banco de testes: mesmo pipeline de vídeo da produção, mas com TODOS os parâmetros mutáveis em
// tempo de execução via GET /cfg?k=v (porta 83), para medir cada variável sem regravar o firmware.
//   Câmera/envio : q, fs, mode (0=chunked 1=raw), nodelay, maxfps, ondemand
//   Câmera (reinit): reinit=1 &fb= &grab=(0=when_empty 1=latest) &xclk=
//   Wi-Fi        : bw (20|40), proto (bgn|gn|n|bg|b), tx (8..84, passos de 0,25 dBm), ps, ch
// GET /stats devolve heap, estações (RSSI) e contadores.
#include "testbed.h"
#include "log.h"
#include <Arduino.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include "esp_camera.h"
#include "esp_http_server.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <errno.h>

#ifndef TB_STREAM_CORE
#define TB_STREAM_CORE 1  // núcleo do servidor httpd que envia o vídeo
#endif
#ifndef TB_CAP_CORE
#define TB_CAP_CORE 1     // núcleo da task que copia os frames
#endif

struct Cfg {
  volatile int q = 15;
  volatile int fs = FRAMESIZE_QVGA;
  volatile int mode = 0;      // 0 = chunked (httpd_resp_send_chunk), 1 = raw (um send() por frame)
  volatile int nodelay = 1;   // TCP_NODELAY
  volatile int maxfps = 0;    // 0 = sem limite
  volatile int ondemand = 0;  // 1 = só captura depois que o envio anterior terminou
  volatile int pause = 0;
};
static Cfg cfg;

struct Shared {
  uint8_t *buf = nullptr;
  size_t len = 0, cap = 0;
  volatile uint32_t seq = 0;
  volatile uint32_t ts = 0;
};
static Shared sh;
static SemaphoreHandle_t mtx = nullptr;
static volatile int clients = 0;
static volatile int want = 0;
static volatile uint32_t capCount = 0;
static volatile int appliedQ = -1, appliedFs = -1;

static uint8_t *allocBuf(size_t n) {
  uint8_t *b = (uint8_t *)heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return b ? b : (uint8_t *)malloc(n);
}

static void capTask(void *) {
  uint32_t lastCap = 0;
  for (;;) {
    if ((clients == 0 && tbWsClients() == 0) || cfg.pause) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
    if (cfg.ondemand && !want) { vTaskDelay(1); continue; }
    if (cfg.maxfps > 0) {
      uint32_t iv = 1000 / cfg.maxfps, now = millis();
      if (now - lastCap < iv) { vTaskDelay(1); continue; }
      lastCap = now;
    }
    sensor_t *s = esp_camera_sensor_get();
    if (s) {
      if (appliedQ != cfg.q) { s->set_quality(s, cfg.q); appliedQ = cfg.q; }
      if (appliedFs != cfg.fs) { s->set_framesize(s, (framesize_t)cfg.fs); appliedFs = cfg.fs; }
    }
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }

    uint32_t ts = (uint32_t)(esp_timer_get_time() / 1000);
    xSemaphoreTake(mtx, portMAX_DELAY);
    if (fb->len > sh.cap) {
      free(sh.buf);
      sh.cap = fb->len + 4096;
      sh.buf = allocBuf(sh.cap);
      if (!sh.buf) sh.cap = 0;
    }
    if (sh.buf) {
      memcpy(sh.buf, fb->buf, fb->len);
      sh.len = fb->len;
      sh.ts = ts;
      sh.seq = sh.seq + 1;
    }
    uint32_t seq = sh.seq;
    xSemaphoreGive(mtx);
    tbWsPublish(fb->buf, fb->len, seq, ts);
    esp_camera_fb_return(fb);
    want = 0;
    capCount = capCount + 1;
  }
}

static bool sendAll(int fd, const uint8_t *p, size_t n) {
  while (n > 0) {
    int r = send(fd, p, n, 0);
    if (r < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    p += r;
    n -= r;
  }
  return true;
}

static const size_t HDR_MAX = 160;

static esp_err_t streamH(httpd_req_t *req) {
  int fd = httpd_req_to_sockfd(req);
  int nd = cfg.nodelay;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nd, sizeof(nd));
  int mode = cfg.mode;

  if (mode == 0) {
    httpd_resp_set_type(req, "multipart/x-mixed-replace;boundary=frame");
  } else {
    static const char H[] =
        "HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace;boundary=frame\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\n\r\n";
    if (!sendAll(fd, (const uint8_t *)H, sizeof(H) - 1)) return ESP_FAIL;
  }

  __atomic_add_fetch(&clients, 1, __ATOMIC_SEQ_CST);
  uint8_t *out = nullptr;
  size_t outCap = 0;
  char part[HDR_MAX];
  uint32_t lastSeq = sh.seq;
  bool ok = true;
  if (cfg.ondemand) want = 1;

  while (ok) {
    unsigned long w0 = millis();
    while (sh.seq == lastSeq) {
      if (millis() - w0 > 3000) { ok = false; break; }
      vTaskDelay(1);
    }
    if (!ok) break;

    xSemaphoreTake(mtx, portMAX_DELAY);
    size_t len = sh.len;
    uint32_t seq = sh.seq, ts = sh.ts;
    if (HDR_MAX + len > outCap) {
      free(out);
      outCap = HDR_MAX + len + 4096;
      out = allocBuf(outCap);
    }
    if (out) memcpy(out + HDR_MAX, sh.buf, len);
    xSemaphoreGive(mtx);
    if (!out) break;
    lastSeq = seq;

    size_t hlen = snprintf(part, sizeof(part),
                           "\r\n--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\nX-Seq: %u\r\nX-Ts: %u\r\n\r\n",
                           (unsigned)len, (unsigned)seq, (unsigned)ts);
    if (mode == 0) {
      esp_err_t r = httpd_resp_send_chunk(req, part, hlen);
      if (r == ESP_OK) r = httpd_resp_send_chunk(req, (const char *)(out + HDR_MAX), len);
      ok = (r == ESP_OK);
    } else {
      uint8_t *start = out + HDR_MAX - hlen;
      memcpy(start, part, hlen);
      ok = sendAll(fd, start, hlen + len);
    }
    if (cfg.ondemand) want = 1;
  }
  free(out);
  __atomic_sub_fetch(&clients, 1, __ATOMIC_SEQ_CST);
  return ESP_FAIL;
}

// ---------------------------- /cfg e /stats (porta 83) ----------------------------
static bool qInt(const char *qs, const char *k, int *out) {
  char b[16];
  if (httpd_query_key_value(qs, k, b, sizeof(b)) == ESP_OK) { *out = atoi(b); return true; }
  return false;
}
static bool qStr(const char *qs, const char *k, char *b, size_t n) {
  return httpd_query_key_value(qs, k, b, n) == ESP_OK;
}

static framesize_t fsFromName(const char *n) {
  if (!strcmp(n, "qqvga")) return FRAMESIZE_QQVGA;
  if (!strcmp(n, "hqvga")) return FRAMESIZE_HQVGA;
  if (!strcmp(n, "qvga")) return FRAMESIZE_QVGA;
  if (!strcmp(n, "cif")) return FRAMESIZE_CIF;
  return FRAMESIZE_VGA;
}

static void chTask(void *arg) {
  int ch = (int)(intptr_t)arg;
  vTaskDelay(pdMS_TO_TICKS(400));
  WiFi.softAP("Pantero", "12345678", ch);
  esp_wifi_set_max_tx_power(78);
  vTaskDelete(nullptr);
}

static esp_err_t cfgH(httpd_req_t *req) {
  char qs[160] = "";
  httpd_req_get_url_query_str(req, qs, sizeof(qs));
  int v;
  char s[16];
  if (qInt(qs, "q", &v)) cfg.q = v;
  if (qStr(qs, "fs", s, sizeof(s))) cfg.fs = fsFromName(s);
  if (qInt(qs, "mode", &v)) cfg.mode = v;
  if (qInt(qs, "nodelay", &v)) cfg.nodelay = v;
  if (qInt(qs, "maxfps", &v)) cfg.maxfps = v;
  if (qInt(qs, "ondemand", &v)) cfg.ondemand = v;
  if (qInt(qs, "bw", &v)) esp_wifi_set_bandwidth(WIFI_IF_AP, v == 40 ? WIFI_BW_HT40 : WIFI_BW_HT20);
  if (qInt(qs, "tx", &v)) esp_wifi_set_max_tx_power(v);
  if (qStr(qs, "proto", s, sizeof(s))) {
    uint8_t p = 0;
    if (strchr(s, 'b')) p |= WIFI_PROTOCOL_11B;
    if (strchr(s, 'g')) p |= WIFI_PROTOCOL_11G;
    if (strchr(s, 'n')) p |= WIFI_PROTOCOL_11N;
    esp_wifi_set_protocol(WIFI_IF_AP, p);
  }
  char cs[8];
  if (qStr(qs, "cam", cs, sizeof(cs)) && !strcmp(cs, "off")) {  // desliga a câmera por completo
    cfg.pause = 1;
    vTaskDelay(pdMS_TO_TICKS(250));
    esp_camera_deinit();
    LOG("TESTBED", "camera DESLIGADA");
  }
  if (qInt(qs, "reinit", &v) && v) {
    int fb = 0, grab = -1, xclk = 0, fbloc = -1;
    qInt(qs, "fb", &fb);
    qInt(qs, "grab", &grab);
    qInt(qs, "xclk", &xclk);
    qInt(qs, "fbloc", &fbloc);
    cfg.pause = 1;
    vTaskDelay(pdMS_TO_TICKS(250));
    esp_camera_deinit();
    bool ok = cameraInitEx(fb, grab, xclk, fbloc);
    appliedQ = appliedFs = -1;
    cfg.pause = 0;
    LOG("TESTBED", "reinit camera fb=%d grab=%d xclk=%d -> %s", fb, grab, xclk, ok ? "ok" : "ERRO");
  }
  if (qInt(qs, "clkrc", &v)) {  // divisor do clock do sensor OV2640 (reg 0x11, banco 1): fps ~ 1/(1+v)
    sensor_t *sn = esp_camera_sensor_get();
    if (sn) {
      sn->set_reg(sn, 0xff, 0xff, 0x01);
      sn->set_reg(sn, 0x11, 0xff, v);
    }
    LOG("TESTBED", "CLKRC -> %d (%s)", v, sn ? "ok" : "sem sensor");
  }
  if (qInt(qs, "camprio", &v)) {  // prioridade da task interna do driver da câmera (padrão: 23)
    TaskHandle_t h = xTaskGetHandle("cam_task");
    if (h) vTaskPrioritySet(h, v);
    LOG("TESTBED", "cam_task prioridade -> %d (%s)", v, h ? "ok" : "task nao encontrada");
  }
  if (qInt(qs, "ch", &v)) xTaskCreate(chTask, "ch", 4096, (void *)(intptr_t)v, 5, nullptr);

  wifi_bandwidth_t bw = WIFI_BW_HT20;
  uint8_t proto = 0;
  int8_t tx = 0;
  esp_wifi_get_bandwidth(WIFI_IF_AP, &bw);
  esp_wifi_get_protocol(WIFI_IF_AP, &proto);
  esp_wifi_get_max_tx_power(&tx);
  char j[256];
  snprintf(j, sizeof(j),
           "{\"q\":%d,\"fs\":%d,\"mode\":%d,\"nodelay\":%d,\"maxfps\":%d,\"ondemand\":%d,\"bw\":%d,\"proto\":%d,\"tx\":%d}",
           (int)cfg.q, (int)cfg.fs, (int)cfg.mode, (int)cfg.nodelay, (int)cfg.maxfps, (int)cfg.ondemand,
           bw == WIFI_BW_HT40 ? 40 : 20, (int)proto, (int)tx);
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_sendstr(req, j);
}

static esp_err_t statsH(httpd_req_t *req) {
  wifi_sta_list_t sl;
  int rssi = 0, n = 0;
  if (esp_wifi_ap_get_sta_list(&sl) == ESP_OK && sl.num > 0) { rssi = sl.sta[0].rssi; n = sl.num; }
  uint32_t ws, wd;
  tbWsStats(&ws, &wd);
  char j[320];
  snprintf(j, sizeof(j),
           "{\"heap\":%u,\"minheap\":%u,\"psram\":%u,\"stations\":%d,\"rssi\":%d,\"cap\":%u,\"seq\":%u,\"len\":%u,\"wsSent\":%u,\"wsDropped\":%u,\"uptime\":%lu}",
           (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(), (unsigned)ESP.getFreePsram(), n, rssi,
           (unsigned)capCount, (unsigned)sh.seq, (unsigned)sh.len, (unsigned)ws, (unsigned)wd, millis());
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_sendstr(req, j);
}

// GET /tasks: lista as tasks do FreeRTOS (nome, estado, prioridade, pilha livre, núcleo)
static esp_err_t tasksH(httpd_req_t *req) {
  static char buf[2048];
  vTaskList(buf);
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_sendstr(req, buf);
}

void testbedStart() {
  mtx = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(capTask, "tb-cap", 4096, nullptr, 6, nullptr, TB_CAP_CORE);

  httpd_config_t c1 = HTTPD_DEFAULT_CONFIG();
  c1.server_port = 81;
  c1.ctrl_port += 1;
  c1.core_id = TB_STREAM_CORE;
  c1.stack_size = 6144;
  c1.max_open_sockets = 3;
  c1.send_wait_timeout = 3;
  httpd_uri_t u1 = {"/stream", HTTP_GET, streamH, nullptr};
  httpd_handle_t h1 = nullptr;
  if (httpd_start(&h1, &c1) == ESP_OK) httpd_register_uri_handler(h1, &u1);

  httpd_config_t c2 = HTTPD_DEFAULT_CONFIG();
  c2.server_port = 83;
  c2.ctrl_port += 2;
  c2.core_id = 0;
  c2.stack_size = 6144;
  httpd_uri_t u2 = {"/cfg", HTTP_GET, cfgH, nullptr};
  httpd_uri_t u3 = {"/stats", HTTP_GET, statsH, nullptr};
  httpd_handle_t h2 = nullptr;
  if (httpd_start(&h2, &c2) == ESP_OK) {
    httpd_register_uri_handler(h2, &u2);
    httpd_register_uri_handler(h2, &u3);
    httpd_uri_t u4 = {"/tasks", HTTP_GET, tasksH, nullptr};
    httpd_register_uri_handler(h2, &u4);
  }
  LOG("TESTBED", "pronto: /stream :81, /cfg e /stats :83, /wsv :80");
}
#endif
