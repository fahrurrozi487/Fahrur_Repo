#include <WiFi.h>
#include <WiFiClient.h>
#include <esp_camera.h>
#include <FS.h>
#include <SD_MMC.h>
#include <WebServer.h>

#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

#define FLASH_LED_GPIO_NUM 4

const char *WIFI_SSID = "AXL";
const char *WIFI_PASSWORD = "GunNRoses";

const bool AUTH_REQUIRED = true;
const char *AUTH_USER = "admin";
const char *AUTH_PASS = "pw";

const int MOTION_THRESHOLD = 18;
const int MOTION_GRID = 8;
const unsigned long MOTION_INTERVAL_MS = 200;
const unsigned long SAVE_COOLDOWN_MS = 5000;

WebServer server(80);

static bool sdOk = false;
static bool sdFull = false;
static bool streamActive = false;
static bool motionNow = false;
static bool camJpeg = true;

static uint8_t prevCells[MOTION_GRID * MOTION_GRID];
static bool havePrev = false;
static unsigned long lastSample = 0;
static unsigned long lastSave = 0;

static bool checkAuth() {
  if (!AUTH_REQUIRED) return true;
  return server.authenticate(AUTH_USER, AUTH_PASS);
}

static void setCamFormat(pixformat_t format) {
  bool wantJpeg = (format == PIXFORMAT_JPEG);
  if (camJpeg == wantJpeg) return;
  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    s->set_pixformat(s, format);
    camJpeg = wantJpeg;
  }
}

static bool grabJpeg(camera_fb_t **out) {
  setCamFormat(PIXFORMAT_JPEG);
  camera_fb_t *discard = esp_camera_fb_get();
  if (discard) esp_camera_fb_return(discard);
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) return false;
  *out = fb;
  return true;
}

static void saveFrame(const char *path, camera_fb_t *fb) {
  File f = SD_MMC.open(path, FILE_WRITE);
  if (!f) return;
  f.write(fb->buf, fb->len);
  f.close();
}

static void motionTick() {
  unsigned long now = millis();
  if (now - lastSample < MOTION_INTERVAL_MS) return;
  lastSample = now;

  setCamFormat(PIXFORMAT_GRAYSCALE);

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) return;

  if (fb->format == PIXFORMAT_GRAYSCALE) {
    int w = fb->width;
    int h = fb->height;
    uint8_t cells[MOTION_GRID * MOTION_GRID];

    for (int gy = 0; gy < MOTION_GRID; gy++) {
      int y0 = (int)((long)gy * h / MOTION_GRID);
      int y1 = (int)((long)(gy + 1) * h / MOTION_GRID);
      for (int gx = 0; gx < MOTION_GRID; gx++) {
        int x0 = (int)((long)gx * w / MOTION_GRID);
        int x1 = (int)((long)(gx + 1) * w / MOTION_GRID);
        long sum = 0;
        int cnt = 0;
        for (int y = y0; y < y1; y += 4) {
          const uint8_t *row = fb->buf + (size_t)y * w;
          for (int x = x0; x < x1; x += 4) {
            sum += row[x];
            cnt++;
          }
        }
        cells[gy * MOTION_GRID + gx] = (uint8_t)(cnt > 0 ? sum / cnt : 0);
      }
    }

    if (havePrev) {
      long diff = 0;
      for (int i = 0; i < MOTION_GRID * MOTION_GRID; i++) {
        diff += abs((int)cells[i] - (int)prevCells[i]);
      }
      diff /= MOTION_GRID * MOTION_GRID;
      motionNow = diff >= MOTION_THRESHOLD;
    } else {
      motionNow = false;
    }
    memcpy(prevCells, cells, sizeof(cells));
    havePrev = true;

    if (motionNow && sdOk && now - lastSave > SAVE_COOLDOWN_MS) {
      lastSave = now;
      uint64_t freeBytes = SD_MMC.totalBytes() - SD_MMC.usedBytes();
      if (freeBytes < (10ULL * 1024 * 1024)) {
        if (!sdFull) {
          sdFull = true;
          Serial.println("SD penuh, rekaman dihentikan");
        }
      } else {
        if (sdFull) {
          sdFull = false;
          Serial.println("Ruang SD tersedia, rekaman dilanjutkan");
        }
        camera_fb_t *jpg = NULL;
        if (grabJpeg(&jpg)) {
          char path[40];
          snprintf(path, sizeof(path), "/cctv/IMG_%lu.jpg", (unsigned long)(now / 1000));
          saveFrame(path, jpg);
          esp_camera_fb_return(jpg);
        }
      }
    }
  }
  esp_camera_fb_return(fb);
}

static void handleList() {
  if (!checkAuth()) {
    server.requestAuthentication();
    return;
  }
  if (!sdOk) {
    server.send(200, "application/json", "[]");
    return;
  }
  File dir = SD_MMC.open("/cctv");
  if (!dir) {
    server.send(200, "application/json", "[]");
    return;
  }
  String json = "[";
  bool first = true;
  File f = dir.openNextFile();
  while (f) {
    if (!f.isDirectory()) {
      if (!first) json += ",";
      json += "{\"name\":\"" + String(f.name()) + "\",\"size\":" + String(f.size()) + "}";
      first = false;
    }
    f.close();
    f = dir.openNextFile();
  }
  dir.close();
  json += "]";
  server.send(200, "application/json", json);
}

static void handlePhoto() {
  if (!checkAuth()) {
    server.requestAuthentication();
    return;
  }
  if (!sdOk) {
    server.send(404, "text/plain", "SD tidak tersedia");
    return;
  }
  String name = server.arg("f");
  if (name.length() == 0) {
    server.send(400, "text/plain", "parameter f wajib diisi");
    return;
  }
  if (name.indexOf('/') >= 0 || name.indexOf('\\') >= 0) {
    server.send(400, "text/plain", "nama file tidak valid");
    return;
  }
  File f = SD_MMC.open("/cctv/" + name, FILE_READ);
  if (!f) {
    server.send(404, "text/plain", "file tidak ditemukan");
    return;
  }
  server.streamFile(f, "image/jpeg");
  f.close();
}

static void handleRoot() {
  if (!checkAuth()) {
    server.requestAuthentication();
    return;
  }
  String html = R"rawliteral(<!DOCTYPE html>
<html lang="id">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32-CAM CCTV</title>
<style>
body { background:#111; color:#eee; font-family:sans-serif; margin:0; padding:16px; }
img { width:100%; max-width:900px; border-radius:8px; display:block; margin:0 auto; }
.controls { text-align:center; margin-top:12px; }
button { background:#222; color:#fff; border:1px solid #444; border-radius:6px; padding:10px 16px; margin:4px; font-size:15px; cursor:pointer; }
button:hover { background:#333; }
#status { text-align:center; margin-top:12px; color:#9adc9a; font-size:14px; }
</style>
</head>
<body>
<img id="view" src="/stream" alt="stream">
<div class="controls">
<button onclick="setFlash('on')">Lampu ON</button>
<button onclick="setFlash('off')">Lampu OFF</button>
<button onclick="capture()">Ambil Foto</button>
</div>
<div id="status">Memuat status...</div>
<script>
function setFlash(s){ fetch('/flash?state='+s).catch(function(){}); }
function capture(){ window.open('/capture','_blank'); }
function loadStatus(){
 fetch('/status').then(function(r){return r.json();}).then(function(j){
  var el = document.getElementById('status');
  if(j.sdFull){
   el.textContent = 'SD PENUH - REKAMAN BERHENTI (sisa ' + j.sdFreeMB + ' MB)';
   el.style.color = '#ff6b6b';
   return;
  }
  var t = 'Status: ';
  t += j.sd ? 'SD OK' : 'SD TIDAK TERBACA';
  t += ' | Motion: ' + (j.motion ? 'TERDETEKSI' : 'Aman');
  if(j.sdFreeMB !== undefined) t += ' | Sisa SD: ' + j.sdFreeMB + ' MB';
  el.textContent = t;
  el.style.color = '#9adc9a';
 }).catch(function(){});
}
setInterval(loadStatus, 2000);
loadStatus();
</script>
</body>
</html>)rawliteral";
  server.send(200, "text/html", html);
}

static void handleStatus() {
  if (!checkAuth()) {
    server.requestAuthentication();
    return;
  }
  String json = "{\"motion\":" + String(motionNow ? "true" : "false");
  json += ",\"sd\":" + String(sdOk ? "true" : "false");
  json += ",\"sdFull\":" + String(sdFull ? "true" : "false");
  if (sdOk) {
    json += ",\"sdFreeMB\":" + String((uint32_t)((SD_MMC.totalBytes() - SD_MMC.usedBytes()) / (1024 * 1024)));
  }
  json += "}";
  server.send(200, "application/json", json);
}

static void handleFlash() {
  if (!checkAuth()) {
    server.requestAuthentication();
    return;
  }
  bool on = server.arg("state") == "on";
  digitalWrite(FLASH_LED_GPIO_NUM, on ? HIGH : LOW);
  server.send(200, "application/json", on ? "{\"flash\":\"on\"}" : "{\"flash\":\"off\"}");
}

static void handleCapture() {
  if (!checkAuth()) {
    server.requestAuthentication();
    return;
  }
  camera_fb_t *fb = NULL;
  if (!grabJpeg(&fb)) {
    server.send(500, "text/plain", "gagal mengambil gambar");
    return;
  }
  server.sendHeader("Content-Type", "image/jpeg");
  server.sendHeader("Content-Length", String(fb->len));
  server.send(200, "image/jpeg", "");
  server.sendContent((const char *)fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

static void handleStream() {
  if (!checkAuth()) {
    server.requestAuthentication();
    return;
  }
  setCamFormat(PIXFORMAT_JPEG);
  camera_fb_t *discard = esp_camera_fb_get();
  if (discard) esp_camera_fb_return(discard);

  WiFiClient client = server.client();
  client.setTimeout(10);
  client.print("HTTP/1.1 200 OK\r\nContent-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n");
  streamActive = true;
  while (client.connected()) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      delay(20);
      continue;
    }
    client.print("--frame\r\nContent-Type: image/jpeg\r\n\r\n");
    client.write(fb->buf, fb->len);
    client.print("\r\n");
    esp_camera_fb_return(fb);
  }
  streamActive = false;
}

void setup() {
  Serial.begin(115200);
  Serial.println("Memulai ESP32-CAM CCTV...");

  pinMode(FLASH_LED_GPIO_NUM, OUTPUT);
  digitalWrite(FLASH_LED_GPIO_NUM, LOW);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 40) {
    delay(500);
    tries++;
  }

  String ip;
  if (WiFi.status() == WL_CONNECTED) {
    ip = WiFi.localIP().toString();
    Serial.print("Terkoneksi WiFi. IP: http://");
    Serial.println(ip);
  } else {
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP32-CAM-Setup", "12345678");
    ip = WiFi.softAPIP().toString();
    Serial.print("Gagal konek WiFi. Mode AP. IP: http://");
    Serial.println(ip);
  }

  camera_config_t config;
  memset(&config, 0, sizeof(config));
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_SVGA;
  config.jpeg_quality = 12;
  config.fb_count = 1;

  esp_err_t err = ESP_OK;
  for (int attempt = 1; attempt <= 5; attempt++) {
    err = esp_camera_init(&config);
    if (err == ESP_OK) break;
    Serial.printf("Kamera gagal (percobaan %d/5): 0x%x. Mencoba ulang...\n", attempt, err);
    delay(2000);
  }
  if (err != ESP_OK) {
    Serial.println("Kamera gagal diinisialisasi setelah 5 percobaan");
    Serial.println("Cek: ribbon kamera terpasang penuh, power 5V cukup (bukan port USB laptop), modul kamera OV2640?");
    return;
  }

  if (SD_MMC.begin("/sdcard", true)) {
    SD_MMC.mkdir("/cctv");
    sdOk = true;
    Serial.println("SD card siap. Foto tersimpan di /cctv");
  } else {
    Serial.println("SD card tidak terdeteksi (motion tidak merekam)");
  }

  server.on("/", HTTP_GET, handleRoot);
  server.on("/stream", HTTP_GET, handleStream);
  server.on("/capture", HTTP_GET, handleCapture);
  server.on("/flash", HTTP_GET, handleFlash);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/list", HTTP_GET, handleList);
  server.on("/photo", HTTP_GET, handlePhoto);
  server.begin();

  Serial.print("Buka browser: http://");
  Serial.println(ip);
  Serial.println("Login: admin / ganti_password");
}

void loop() {
  server.handleClient();
  if (!streamActive) motionTick();
}
