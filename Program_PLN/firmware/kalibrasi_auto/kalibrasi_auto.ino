// Kalibrasi pH otomatis via Serial. Baca Po lewat ADS1115 A0 (domain v = SAMA dgn firmware:
// ph = SLOPE*v + OFFSET, jadi konstanta hasil bisa langsung ditempel).
// Metode: regresi linear least-squares. Utk sensor melengkung, MASUKKAN 2 buffer rentang kerja
// saja (6.86 & 9.18) -> 2 titik = garis eksak. Titik pH4 sengaja tak dipakai.
// Pin sama dgn esp-02: I2C_SDA=8, I2C_SCL=9, ADS 0x48, A0, GAIN_ONE, trimmed-mean 31.
//
// Pakai (Serial 115200):
//   1. Celup probe ke buffer, tunggu kolom 'live v' diam.
//   2. Ketik nilai pH buffer (mis. 6.86) + Enter -> script ukur ~20 dtk, rekam (pH, v).
//   3. Ulang utk 9.18.
//   4. Ketik 'hitung' -> cetak PH_SLOPE & PH_OFFSET.  'reset' -> hapus titik.
#include <Wire.h>
#include <Adafruit_ADS1X15.h>

#define I2C_SDA 8
#define I2C_SCL 9
#define ADS_ADDR 0x48
#define PH_CHANNEL 0
#define PH_NUM_SAMPLES 31
#define MEAS_MS 20000        // durasi rata-rata per titik (redam noise ~0.02V)
#define MAX_POINTS 8

Adafruit_ADS1115 ads;
float phBuf[MAX_POINTS], vBuf[MAX_POINTS];
int nPoints = 0;

// Trimmed-mean 31 sampel, identik firmware.
float readV() {
  float v[PH_NUM_SAMPLES];
  for (int i = 0; i < PH_NUM_SAMPLES; i++) { v[i] = ads.computeVolts(ads.readADC_SingleEnded(PH_CHANNEL)); delay(6); }
  for (int i = 1; i < PH_NUM_SAMPLES; i++) { float k=v[i]; int j=i-1; while(j>=0&&v[j]>k){v[j+1]=v[j];j--;} v[j+1]=k; }
  int lo = PH_NUM_SAMPLES/4, hi = PH_NUM_SAMPLES-(PH_NUM_SAMPLES/4);
  float s=0; int c=0; for (int i=lo;i<hi;i++){s+=v[i];c++;} return s/c;
}

// Regresi linear least-squares: ph = m*v + b. Utk n=2 -> garis eksak.
void fit(float* vs, float* phs, int n, float& m, float& b) {
  float sv=0, sp=0, svp=0, svv=0;
  for (int i=0;i<n;i++){ sv+=vs[i]; sp+=phs[i]; svp+=vs[i]*phs[i]; svv+=vs[i]*vs[i]; }
  m = (n*svp - sv*sp) / (n*svv - sv*sv);
  b = (sp - m*sv) / n;
}

// Self-check: data esp-02 nyata harus reproduksi konstanta tertulis.
void selfTest() {
  float tv[2]={1.672,1.515}, tp[2]={6.86,9.18}, m,b;
  fit(tv,tp,2,m,b);
  bool ok = fabs(m-(-14.777))<0.01 && fabs(b-31.567)<0.01;
  Serial.printf("[selftest] fit->m=%.3f b=%.3f  %s\n", m, b, ok?"OK":"FAIL");
}

void doMeasure(float ph) {
  if (nPoints >= MAX_POINTS) { Serial.println("[!] penuh, ketik reset"); return; }
  Serial.printf("[ukur] pH %.2f selama %d dtk...\n", ph, MEAS_MS/1000);
  float sum=0, mn=9, mx=-9; int c=0;
  unsigned long t0 = millis();
  while (millis()-t0 < MEAS_MS) { float v=readV(); sum+=v; c++; if(v<mn)mn=v; if(v>mx)mx=v; }
  float avg = sum/c;
  phBuf[nPoints]=ph; vBuf[nPoints]=avg; nPoints++;
  Serial.printf("[rekam #%d] pH=%.2f  v=%.6f  (spread %.4f)\n", nPoints, ph, avg, mx-mn);
}

void doCalc() {
  if (nPoints < 2) { Serial.println("[!] butuh >=2 titik"); return; }
  float m,b; fit(vBuf,phBuf,nPoints,m,b);
  Serial.println("==================================");
  Serial.printf("const float PH_SLOPE  = %.3f;\n", m);
  Serial.printf("const float PH_OFFSET = %.3f;\n", b);
  Serial.println("==================================");
  for (int i=0;i<nPoints;i++) Serial.printf("  cek: v=%.4f -> pH %.2f (aktual %.2f)\n", vBuf[i], m*vBuf[i]+b, phBuf[i]);
}

void setup() {
  Serial.begin(115200); delay(500);
  Wire.begin(I2C_SDA, I2C_SCL);
  if (!ads.begin(ADS_ADDR)) { Serial.println("[ADS1115] TIDAK TERDETEKSI!"); while(1) delay(100); }
  ads.setGain(GAIN_ONE);
  selfTest();
  Serial.println("Ketik nilai pH buffer (mis 6.86) utk rekam | 'hitung' | 'reset'");
}

unsigned long lastLive = 0;
void loop() {
  if (Serial.available()) {
    String s = Serial.readStringUntil('\n'); s.trim();
    if (s.equalsIgnoreCase("hitung")) doCalc();
    else if (s.equalsIgnoreCase("reset")) { nPoints=0; Serial.println("[reset] titik dihapus"); }
    else if (s.length()) { float ph = s.toFloat(); if (ph>0) doMeasure(ph); else Serial.println("[!] input tak dikenal"); }
  }
  if (millis()-lastLive > 500) { lastLive=millis(); Serial.printf("live v=%.6f\n", readV()); }
}
