// ==== Konfigurasi Firmware ESP32 - Smart Temperature AI Agent ====
// Salin file ini menjadi config.h lalu isi nilainya. JANGAN commit config.h berisi kredensial ke repo publik.
#pragma once

// --- WiFi (siapkan juga hotspot HP sebagai cadangan, PRD 7 & 14) ---
#define WIFI_SSID      "FC"
#define WIFI_PASSWORD  "11111111"

// --- Gateway ---
// Ganti IP dengan alamat komputer yang menjalankan gateway (cek: ipconfig / ifconfig).
#define GATEWAY_URL    "http://192.168.137.140:8000/ingest"  // Ganti XXX dengan IP device OpenClaw
#define DEVICE_ID      "esp32-01"
#define DEVICE_TOKEN   "esp32-temperature-system"   // harus sama dengan .env gateway

// --- Pin (sesuaikan dengan wiring; hindari GPIO strapping seperti 0,2,15) ---
#define PIN_DHT        4    // data DHT11
#define PIN_LED_HIJAU  16
#define PIN_LED_KUNING 17
#define PIN_LED_MERAH  18
#define PIN_BUZZER     19   // buzzer aktif

// --- Ambang suhu (°C). HARUS sinkron dengan gateway .env (PRD FR-3) ---
// hijau <SUHU_KUNING, kuning [SUHU_KUNING, SUHU_MERAH), merah >=SUHU_MERAH
#define SUHU_KUNING     40.0
#define SUHU_MERAH      45.0

// --- Interval kirim data (ms) ---
#define INTERVAL_KIRIM  5000
