#include <Wire.h>
#include <Adafruit_ADS1X15.h>

Adafruit_ADS1115 ads;

#define I2C_SDA 8
#define I2C_SCL 9
#define ADS_ADDR 0x48

#define PH_CHANNEL 0

float PH_SLOPE  = -7.518;  // m
float PH_OFFSET = 26.579;  // b

const int NUM_SAMPLES = 31;  
const float EMA_ALPHA = 0.1;
float phEMA = NAN;

void setup() {
  Serial.begin(115200);
  delay(500);

  Wire.begin(I2C_SDA, I2C_SCL);

  if (!ads.begin(ADS_ADDR)) {
    Serial.println("[ADS1115] TIDAK TERDETEKSI! Cek wiring I2C & alamat.");
    while (1) delay(100);
  }

  ads.setGain(GAIN_ONE);

  Serial.println("=== Baca pH via ADS1115 ===");
  Serial.println("Format: Volt | pH");
  Serial.println("(saat kalibrasi, fokus ke kolom Volt)");
  Serial.println("---------------------------");
}

float readVoltage() {
  float v[NUM_SAMPLES];
  for (int i = 0; i < NUM_SAMPLES; i++) {
    v[i] = ads.computeVolts(ads.readADC_SingleEnded(PH_CHANNEL));
    delay(8);
  }

  for (int i = 1; i < NUM_SAMPLES; i++) {
    float key = v[i];
    int j = i - 1;
    while (j >= 0 && v[j] > key) { v[j + 1] = v[j]; j--; }
    v[j + 1] = key;
  }

  int lo = NUM_SAMPLES / 4;
  int hi = NUM_SAMPLES - lo;
  float sum = 0;
  int   cnt = 0;
  for (int i = lo; i < hi; i++) { sum += v[i]; cnt++; }
  return sum / cnt;
}

void loop() {
  float voltage = readVoltage();
  float ph = PH_SLOPE * voltage + PH_OFFSET;

  if (isnan(phEMA)) phEMA = ph;
  else              phEMA = EMA_ALPHA * ph + (1.0 - EMA_ALPHA) * phEMA;
  Serial.printf("V: %.4f V  |  pH(raw): %.2f  |  pH(halus): %.2f\n",
                voltage, ph, phEMA);
  delay(500);
}
