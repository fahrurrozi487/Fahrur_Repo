/**
 * esp-test — UJI DASHBOARD TANPA SENSOR
 *
 * Sketch ini TIDAK butuh DS18B20 / ADS1115 / pH probe / TDS sensor sama sekali.
 * Tujuannya: membuktikan rantai ESP32 -> Worker -> Supabase -> Dashboard
 * bekerja, dengan mengirim data PALSU (dummy) yang realistis.
 *
 * Sama persis dengan firmware asli: format batch JSON, timestamp NTP,
 * header X-API-Key. Yang dibuang hanya pembacaan sensor.
 *
 * Cara pakai:
 *   1. Isi WIFI_SSID / WIFI_PASSWORD bila beda.
 *   2. (opsional) ganti DEVICE_ID -> "esp-test" agar tidak menimpa unit asli,
 *      ATAU pakai "esp-01" untuk meniru unit nyata.
 *   3. Upload ke ESP32-S3 (board: ESP32S3 Dev Module). Tidak perlu wiring apa pun.
 *   4. Serial Monitor 115200 -> lihat "[HTTP] batch ... OK (code 201)".
 *   5. Buka dashboard -> kartu Live Monitor untuk device ini harus bergerak.
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>

// =====================================================
//  KONFIGURASI
// =====================================================
const char* WIFI_SSID     = "AXL";
const char* WIFI_PASSWORD = "GunNRoses";

const char* WORKER_URL     = "https://sensor-monitor.aquaponic.workers.dev/api/sensor";
const char* DEVICE_API_KEY = "PeTImlPYZj4exKFr7X7YQv9H3XdvSDOT68Hryi0+tos=";   // kunci bersama (SAMA di semua unit, cocok secret Worker)

const char* DEVICE_ID = "esp-05";        // <- pakai "esp-test" agar tidak ganggu data asli
                                           //    (ingat: tambahkan ke DEVICES[] di dashboard
                                           //     agar kartunya muncul — lihat useSensorData.js)

const int SAMPLE_INTERVAL_MS = 1000;  // buat 1 rekaman/detik
const int BATCH_SIZE         = 10;    // kirim tiap 10 rekaman (=10 detik)
// =====================================================

const char* NTP_SERVER1 = "pool.ntp.org";
const char* NTP_SERVER2 = "time.google.com";

struct Reading { time_t ts; float temp; float ph; float water_quality; };
Reading buf[BATCH_SIZE];
int bufCount = 0;
unsigned long lastSample = 0;

// nilai awal untuk "random walk" supaya grafik terlihat alami
float fakeTemp = 27.0;
float fakePh   = 7.0;
float fakeWaterQuality = 450.0; // ppm

void connectWiFi() {
  Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.printf("\n[WiFi] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("================================");
  Serial.printf ("  esp [%s] - MODE UJI (data palsu)\n", DEVICE_ID);
  Serial.println("  Tanpa sensor. Hanya kirim dummy.");
  Serial.println("================================");

  connectWiFi();

  configTime(0, 0, NTP_SERVER1, NTP_SERVER2);   // UTC
  Serial.print("[NTP] sync");
  struct tm ti;
  while (!getLocalTime(&ti, 1000)) Serial.print(".");
  Serial.println(" ok");
}

// Random walk kecil + dijaga dalam rentang wajar.
void makeFakeReading(float &t, float &ph, float &waterQuality) {
  fakeTemp += (random(-100, 101) / 1000.0);   // ±0.10 °C tiap detik
  fakePh   += (random(-50, 51) / 1000.0);     // ±0.05 pH tiap detik
  fakeWaterQuality += (random(-500, 501) / 100.0); // ±5 ppm tiap detik
  if (fakeTemp < 24) fakeTemp = 24;  if (fakeTemp > 32) fakeTemp = 32;
  if (fakePh   < 6.0) fakePh   = 6.0; if (fakePh   > 8.5) fakePh   = 8.5;
  if (fakeWaterQuality < 250) fakeWaterQuality = 250;
  if (fakeWaterQuality > 900) fakeWaterQuality = 900;
  t  = fakeTemp;
  ph = fakePh;
  waterQuality = fakeWaterQuality;
}

bool sendBatch() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Disconnected, reconnecting...");
    WiFi.reconnect();
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, WORKER_URL)) {
    Serial.println("[HTTP] begin() gagal");
    return false;
  }
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-API-Key", DEVICE_API_KEY);

  // {"device":"esp-test","readings":[{"t":"...","temperature":..,"ph":..,"water_quality":..},...]}
  String body = String("{\"device\":\"") + DEVICE_ID + "\",\"readings\":[";
  char tsbuf[25];
  for (int i = 0; i < bufCount; i++) {
    struct tm g; gmtime_r(&buf[i].ts, &g);
    strftime(tsbuf, sizeof(tsbuf), "%Y-%m-%dT%H:%M:%SZ", &g);
    if (i) body += ",";
    body += "{\"t\":\"" + String(tsbuf) + "\",\"temperature\":";
    body += String(buf[i].temp, 2);
    body += ",\"ph\":" + String(buf[i].ph, 2);
    body += ",\"water_quality\":" + String(buf[i].water_quality, 2) + "}";
  }
  body += "]}";

  int code = http.POST(body);
  bool ok = (code == 200 || code == 201);
  if (ok) Serial.printf("[HTTP] batch %d rekaman OK (code %d)\n", bufCount, code);
  else    Serial.printf("[HTTP] batch GAGAL (code %d): %s\n", code, http.getString().c_str());
  http.end();
  return ok;
}

void loop() {
  if (millis() - lastSample >= (unsigned long)SAMPLE_INTERVAL_MS) {
    lastSample = millis();

    float t, ph, waterQuality;
    makeFakeReading(t, ph, waterQuality);
    time_t now = time(nullptr);

    buf[bufCount].ts = now;
    buf[bufCount].temp = t;
    buf[bufCount].ph = ph;
    buf[bufCount].water_quality = waterQuality;
    bufCount++;

    Serial.printf("[rec %d/%d] t=%.2f C  ph=%.2f  water_quality=%.2f ppm (palsu)\n",
                  bufCount, BATCH_SIZE, t, ph, waterQuality);

    if (bufCount >= BATCH_SIZE) {
      sendBatch();
      bufCount = 0;
    }
  }
  delay(5);
}
