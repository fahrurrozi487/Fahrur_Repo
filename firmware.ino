/*
 * Firmware ESP32 - Smart Temperature AI Agent
 * Baca DHT11, kontrol LED (hijau/kuning/merah) + buzzer via AMBANG LOKAL (deterministik),
 * kirim data ke gateway via HTTP POST, jalankan perintah balik (buzzer_off / buzzer_on).
 *
 * Revisi: tidak lagi pakai relay/kipas. Buzzer aktif otomatis saat level kuning
 * (bip berjeda) dan merah (bunyi terus), bisa di-override manual dari Telegram.
 *
 * Kontrol LED/buzzer sengaja di sini, bukan di LLM (PRD v1.1) -> aman & cepat saat demo.
 *
 * Library (Arduino IDE > Library Manager):
 *   - "DHT11" by Dhruba Saha   (BUKAN "DHT sensor library" Adafruit - beda API)
 *   - ArduinoJson  by Benoit Blanchon
 * Board: pilih board ESP32 kamu (mis. "ESP32 Dev Module").
 *
 * Salin config.example.h -> config.h dan isi sebelum compile.
 *
 * Asumsi buzzer: modul buzzer AKTIF (on/off lewat digitalWrite, bukan piezo pasif
 * yang butuh tone()). Kalau punya buzzer pasif, ganti digitalWrite(PIN_BUZZER, HIGH)
 * jadi tone(PIN_BUZZER, 2000) dan LOW jadi noTone(PIN_BUZZER).
 */
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <DHT11.h>
#include "config.h"

DHT11 dht11(PIN_DHT);

unsigned long lastKirim = 0;
const char *levelSekarang = "hijau";   // level suhu terakhir (dipakai buzzer tiap loop)

// ---- Override manual buzzer dari Telegram (/buzzer_off, /buzzer_on) ----
enum BuzzerOverride { OVERRIDE_NONE, OVERRIDE_FORCE_ON, OVERRIDE_FORCE_OFF };
BuzzerOverride buzzerOverride = OVERRIDE_NONE;
// Catatan desain: override otomatis balik ke OVERRIDE_NONE saat level kembali
// ke "hijau" (normal) - biar mute/tes tidak "nyangkut" ke episode warning berikutnya.

unsigned long buzzerToggleAt = 0;
bool buzzerBeepState = false;
const unsigned long BUZZER_BEEP_MS = 200;  // durasi on/off pola bip kuning (ms)

void setLed(const char *lvl) {
  digitalWrite(PIN_LED_HIJAU,  strcmp(lvl, "hijau")  == 0);
  digitalWrite(PIN_LED_KUNING, strcmp(lvl, "kuning") == 0);
  digitalWrite(PIN_LED_MERAH,  strcmp(lvl, "merah")  == 0);
}

const char *levelSuhu(float suhu) {
  if (suhu >= SUHU_MERAH) return "merah";
  if (suhu >= SUHU_KUNING) return "kuning";
  return "hijau";
}

// Dipanggil TIAP iterasi loop() (bukan cuma tiap INTERVAL_KIRIM) supaya pola
// bip kuning yang berjeda bisa jalan mulus tanpa nge-block pembacaan sensor.
void updateBuzzer() {
  if (buzzerOverride == OVERRIDE_FORCE_OFF) {
    digitalWrite(PIN_BUZZER, LOW);
    return;
  }
  if (buzzerOverride == OVERRIDE_FORCE_ON) {
    digitalWrite(PIN_BUZZER, HIGH);
    return;
  }
  // OVERRIDE_NONE -> ikut level otomatis
  if (strcmp(levelSekarang, "merah") == 0) {
    digitalWrite(PIN_BUZZER, HIGH);              // bunyi terus, tanpa putus
  } else if (strcmp(levelSekarang, "kuning") == 0) {
    if (millis() - buzzerToggleAt >= BUZZER_BEEP_MS) {
      buzzerToggleAt = millis();
      buzzerBeepState = !buzzerBeepState;
      digitalWrite(PIN_BUZZER, buzzerBeepState ? HIGH : LOW);  // bip-bip-bip berjeda
    }
  } else {
    digitalWrite(PIN_BUZZER, LOW);                // hijau -> mati
    buzzerBeepState = false;
  }
}

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Menghubungkan WiFi");
  unsigned long mulai = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - mulai < 20000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println(WiFi.status() == WL_CONNECTED ? " tersambung" : " GAGAL");
  if (WiFi.status() == WL_CONNECTED) Serial.println(WiFi.localIP());
}

void jalankanPerintah(const char *cmd) {
  if (strcmp(cmd, "buzzer_off") == 0) {
    buzzerOverride = OVERRIDE_FORCE_OFF;
    Serial.println("Perintah: buzzer dibisukan manual.");
  } else if (strcmp(cmd, "buzzer_on") == 0) {
    buzzerOverride = OVERRIDE_FORCE_ON;
    Serial.println("Perintah: buzzer dinyalakan manual (mode tes).");
  }
}

void kirimData(float suhu, float hum, const char *lvl) {
  if (WiFi.status() != WL_CONNECTED) {
    connectWifi();
    if (WiFi.status() != WL_CONNECTED) return;
  }

  StaticJsonDocument<256> doc;
  doc["device_id"] = DEVICE_ID;
  doc["suhu_c"] = suhu;
  doc["kelembapan"] = hum;
  doc["level"] = lvl;
  String body;
  serializeJson(doc, body);

  HTTPClient http;
  http.begin(GATEWAY_URL);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-Token", DEVICE_TOKEN);
  http.setTimeout(8000);

  int code = http.POST(body);
  if (code == 200) {
    StaticJsonDocument<256> resp;
    if (deserializeJson(resp, http.getString()) == DeserializationError::Ok) {
      const char *cmd = resp["command"] | "";
      if (strlen(cmd) > 0) jalankanPerintah(cmd);
    }
  } else {
    Serial.printf("POST gagal, kode: %d\n", code);
  }
  http.end();
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED_HIJAU, OUTPUT);
  pinMode(PIN_LED_KUNING, OUTPUT);
  pinMode(PIN_LED_MERAH, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);
  setLed("hijau");
  connectWifi();
}

void loop() {
  updateBuzzer();   // jalan tiap iterasi, ringan & non-blocking

  if (millis() - lastKirim < INTERVAL_KIRIM) return;
  lastKirim = millis();

  int suhuInt = 0, humInt = 0;
  int hasil = dht11.readTemperatureHumidity(suhuInt, humInt);
  if (hasil != 0) {
    Serial.print("Gagal baca DHT11: ");
    Serial.println(DHT11::getErrorString(hasil));
    return;
  }
  float suhu = suhuInt;
  float hum = humInt;

  levelSekarang = levelSuhu(suhu);
  if (strcmp(levelSekarang, "hijau") == 0) {
    buzzerOverride = OVERRIDE_NONE;   // reset override begitu kembali normal
  }
  setLed(levelSekarang);

  Serial.printf("Suhu=%.0f Hum=%.0f Level=%s\n", suhu, hum, levelSekarang);

  kirimData(suhu, hum, levelSekarang);
}
