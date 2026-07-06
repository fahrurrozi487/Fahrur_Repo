/**
 * esp-01_phonly — TES ISOLASI #2 (tanpa WiFi, tanpa DS18B20)
 *
 * TUJUAN: setelah WiFi terbukti BUKAN penyebab, tes ini membuang DS18B20
 *         untuk melihat apakah pembacaan sensor suhu yang menggeser tegangan
 *         board pH (beban arus pada rel 3.3V).
 *
 * Ini menyisakan jalur baca pH esp-01 PERSIS (GAIN_ONE, trimmed-mean 31,
 * delay(6), EMA 0.1) tapi TANPA WiFi & TANPA baca suhu. Praktis = kalibrasi_ph
 * dengan kode baca esp-01.
 *
 * CARA PAKAI:
 *   1. Flash ke esp-01, celup ke buffer pH 9.18, tunggu ~30 dtk (EMA settle).
 *   2. Lihat kolom pH(halus):
 *      - ~9.13 (sama kalibrasi_ph) -> DS18B20 penyebabnya (beban rel 3.3V).
 *      - masih ~9.35              -> bukan DS18B20; sisa beda hanya delay(6vs8)
 *                                    -> lapor V & pH ke saya.
 */

#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>

// --- Konstanta kalibrasi pH (sama esp-01) ---
const float PH_SLOPE  = -7.341;  // m
const float PH_OFFSET = 26.415;  // b

const float EMA_ALPHA = 0.1;
float phEMA = NAN;

const int SAMPLE_INTERVAL_MS = 1000;

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
  Serial.println("  esp-01 TES ISOLASI #2 - tanpa WiFi & DS18B20");
  Serial.println("==========================================");

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("[WiFi] OFF");
  Serial.println("[DS18B20] TIDAK dibaca (sengaja)");

  Wire.begin(I2C_SDA, I2C_SCL);
  if (!ads.begin(ADS_ADDR)) {
    Serial.println("[ADS1115] TIDAK TERDETEKSI! Cek wiring I2C & alamat.");
    while (1) delay(100);
  }
  ads.setGain(GAIN_ONE);
  Serial.println("[ADS1115] initialized.");
  Serial.println("Format: V | pH(raw) | pH(halus)");
  Serial.println("------------------------------------------------------------");
}

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

    Serial.printf("V: %.4f V  |  pH(raw): %.2f  |  pH(halus): %.2f\n",
                  v, ph, phEMA);
  }
  delay(5);
}
