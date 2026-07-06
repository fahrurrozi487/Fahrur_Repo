/**
 * esp-01_nowifi — VERSI TES DIAGNOSTIK (WiFi DIMATIKAN)
 *
 * TUJUAN: membuktikan apakah selisih bacaan pH antara esp-01.ino (WiFi ON)
 *         dan kalibrasi_ph.ino (tanpa WiFi) disebabkan oleh noise WiFi pada
 *         rail 3.3V / ground board pH.
 *
 * Sketch ini SAMA PERSIS dengan esp-01.ino di jalur baca pH:
 *   - konstanta kalibrasi sama (PH_SLOPE / PH_OFFSET)
 *   - GAIN_ONE, trimmed-mean 31 sampel, delay(6) antar-sampel
 *   - EMA_ALPHA 0.1, interval 1 detik
 * BEDANYA HANYA: WiFi OFF + NTP/HTTP dihapus, dan mencetak raw & halus
 *   seperti kalibrasi_ph.ino agar apple-to-apple.
 *
 * CARA PAKAI:
 *   1. Flash sketch ini ke unit esp-01.
 *   2. Celup probe ke buffer yang sama (mis. pH 9.18), tunggu ~30 detik
 *      agar kolom pH(halus) settle.
 *   3. Bandingkan pH(halus) di sini vs pH(halus) di kalibrasi_ph.ino.
 *      - Jika sekarang ~SAMA (mis. 9.13)  -> WiFi terbukti penyebab pergeseran.
 *      - Jika masih beda                  -> penyebab lain (lapor ke saya).
 *
 *   Setelah selesai tes, kembali flash esp-01.ino yang asli untuk operasi normal.
 */

#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// --- Konstanta kalibrasi pH (HARUS sama dengan esp-01.ino) ---
const float PH_SLOPE  = -7.341;  // m
const float PH_OFFSET = 26.415;  // b

// --- Moving average (EMA) — sama dengan esp-01.ino ---
const float EMA_ALPHA = 0.1;
float phEMA = NAN;

const int SAMPLE_INTERVAL_MS = 1000;  // sama dengan esp-01 (per-detik)

// --- DS18B20 (GPIO 4) — tetap dibaca agar beban I2C/OneWire identik esp-01 ---
#define ONE_WIRE_BUS 4
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature tempSensor(&oneWire);

// --- ADS1115 (I2C) untuk pH ---
Adafruit_ADS1115 ads;
#define I2C_SDA     8
#define I2C_SCL     9
#define ADS_ADDR    0x48
#define PH_CHANNEL  0
const int PH_NUM_SAMPLES = 31;

unsigned long lastSample = 0;

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("==========================================");
  Serial.println("  esp-01 TES DIAGNOSTIK - WiFi DIMATIKAN");
  Serial.println("==========================================");

  // ---- MATIKAN WiFi TOTAL (inti dari tes ini) ----
  WiFi.disconnect(true);   // putus + hapus kredensial dari RAM
  WiFi.mode(WIFI_OFF);     // matikan stack WiFi
  Serial.println("[WiFi] OFF (mode diagnostik)");

  tempSensor.begin();
  tempSensor.setResolution(10);
  Serial.println("[DS18B20] initialized.");

  Wire.begin(I2C_SDA, I2C_SCL);
  if (!ads.begin(ADS_ADDR)) {
    Serial.println("[ADS1115] TIDAK TERDETEKSI! Cek wiring I2C & alamat.");
    while (1) delay(100);
  }
  ads.setGain(GAIN_ONE);
  Serial.println("[ADS1115] initialized.");

  Serial.println("Format: V | pH(raw) | pH(halus)  -- bandingkan kolom pH(halus)");
  Serial.println("------------------------------------------------------------");
}

// Trimmed-mean: 31 sampel, buang 25% tepi, rata-rata sisanya. (SAMA esp-01)
float readPHVoltage() {
  float v[PH_NUM_SAMPLES];
  for (int i = 0; i < PH_NUM_SAMPLES; i++) {
    v[i] = ads.computeVolts(ads.readADC_SingleEnded(PH_CHANNEL));
    delay(6);
  }
  for (int i = 1; i < PH_NUM_SAMPLES; i++) {
    float key = v[i]; int j = i - 1;
    while (j >= 0 && v[j] > key) { v[j + 1] = v[j]; j--; }
    v[j + 1] = key;
  }
  int lo = PH_NUM_SAMPLES / 4, hi = PH_NUM_SAMPLES - (PH_NUM_SAMPLES / 4);
  float sum = 0; int cnt = 0;
  for (int i = lo; i < hi; i++) { sum += v[i]; cnt++; }
  return sum / cnt;
}

void loop() {
  if (millis() - lastSample >= (unsigned long)SAMPLE_INTERVAL_MS) {
    lastSample = millis();

    float v  = readPHVoltage();
    float ph = PH_SLOPE * v + PH_OFFSET;
    if (isnan(phEMA)) phEMA = ph;
    else              phEMA = EMA_ALPHA * ph + (1.0 - EMA_ALPHA) * phEMA;

    // baca suhu juga (agar beban kerja loop identik dgn esp-01)
    tempSensor.requestTemperatures();
    (void)tempSensor.getTempCByIndex(0);

    Serial.printf("V: %.4f V  |  pH(raw): %.2f  |  pH(halus): %.2f\n",
                  v, ph, phEMA);
  }
  delay(5);
}
