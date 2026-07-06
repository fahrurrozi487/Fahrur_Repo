// Ukur tegangan Po board pH lewat ADS1115 A0. Tanpa WiFi/suhu.
// Pakai untuk cek modul hidup & baca v mentah saat kalibrasi.
// Pin sama dgn esp-02: I2C_SDA=8, I2C_SCL=9, ADS 0x48, A0.
#include <Wire.h>
#include <Adafruit_ADS1X15.h>

#define I2C_SDA 8
#define I2C_SCL 9
#define ADS_ADDR 0x48
#define PH_CHANNEL 0

Adafruit_ADS1115 ads;

void setup() {
  Serial.begin(115200);
  delay(500);
  Wire.begin(I2C_SDA, I2C_SCL);
  if (!ads.begin(ADS_ADDR)) {
    Serial.println("[ADS1115] TIDAK TERDETEKSI! Cek wiring I2C & alamat.");
    while (1) delay(100);
  }
  ads.setGain(GAIN_ONE);  // sama dgn esp-02, maks +-4.096V
  Serial.println("[ADS1115] ok. Short BNC harusnya v~1.25 (Po~2.5V lewat divider /2).");
}

void loop() {
  float v = ads.computeVolts(ads.readADC_SingleEnded(PH_CHANNEL));
  Serial.printf("v=%.6f  (Po~%.3f jika divider /2)\n", v, v * 2.0);
  delay(500);
}
