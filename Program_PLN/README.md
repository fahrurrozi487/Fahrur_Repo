# Sensor Monitor — DS18B20 + pH (ESP32-S3 N16R8)

Sistem monitoring **suhu air (DS18B20)** + **pH (via ADS1115)** untuk **4 unit ESP32-S3**,
menyimpan data ke **Supabase** melalui **Cloudflare Worker**.

Arsitektur final (Opsi B):
```
ESP32 (x4) ──POST (X-API-Key)──> Cloudflare Worker ──service_role──> Supabase
                                                                        │
                                                   Flutter App ──baca───┘
```

---

## Struktur Folder

```
Program_PLN/
├── README.md                  # File ini
├── PANDUAN_SETUP.md           # Panduan setup Supabase + Cloudflare dari nol
├── kalibrasi_log.md           # Arsip konstanta kalibrasi pH tiap probe
├── WIRING_ADS1115.md          # Skema wiring sensor (ADS1115)
├── ARSIP_CSV.md               # Arsip CSV per-detik ke R2 (setup + cara pakai)
├── MIGRASI.md                 # Panduan pindah akun Supabase & Cloudflare
├── RENCANA_APP.md             # Rencana aplikasi Flutter (fitur, tabel, query)
├── DEV_HANDOFF.md             # ⭐ Acuan konektivitas untuk tim developer app
├── contoh_flutter/            # Service Dart siap-pakai (sensor_service.dart)
├── supabase_setup.sql         # SQL pembuat tabel readings (sudah dijalankan)
├── app_setup.sql              # SQL untuk app: rollup + auth RLS + get_series + thresholds
│
├── firmware/                  # ⭐ Sketch ESP — satu folder per unit
│   ├── esp-01/esp-01.ino
│   ├── esp-02/esp-02.ino
│   ├── esp-03/esp-03.ino
│   ├── esp-04/esp-04.ino
│   └── kalibrasi_ph/kalibrasi_ph.ino   # alat kalibrasi pH
│
├── cloudflare/                # ⭐ Cloudflare Worker (sudah deploy)
│   ├── worker.js
│   └── wrangler.toml
│
└── arsip/                     # File lama / tidak dipakai (boleh diabaikan)
```

> 🧹 **Sisa pembersihan:** folder `baca_ph_ads1115/`, `opsi_A_langsung/`, dan
> `opsi_B_worker/` belum bisa dipindah otomatis karena **terbuka di Arduino IDE**.
> Setelah menutup Arduino IDE, pindahkan ketiganya ke `arsip/` secara manual
> (drag-and-drop). Isinya sudah digantikan oleh `firmware/` & `cloudflare/`.

---

## Konfigurasi 4 Unit ESP

Tiap unit pakai sketch sendiri di `firmware/esp-0X/`. Yang **sudah terisi** otomatis:

| File | DEVICE_ID | PH_SLOPE | PH_OFFSET | Probe |
|------|-----------|----------|-----------|-------|
| `firmware/esp-01/esp-01.ino` | esp-01 | -7.736 | 28.081 | Sensor #1 |
| `firmware/esp-02/esp-02.ino` | esp-02 | -6.811 | 25.383 | Sensor #2 |
| `firmware/esp-03/esp-03.ino` | esp-03 | -6.525 | 24.035 | Sensor #3 |
| `firmware/esp-04/esp-04.ino` | esp-04 | -7.051 | 25.482 | Sensor #4 |

Sudah terisi juga (sama untuk keempatnya):
- `WIFI_SSID = "AXL"`, `WIFI_PASSWORD = "GunNRoses"`
- `WORKER_URL = https://sensor-monitor.fahrurrohzi42.workers.dev/api/sensor`
- `DEVICE_API_KEY = "esp-02"` ← **kunci bersama** (cocok dengan secret di Worker)
- Mode **per-detik + batch**: rekam tiap 1 detik, kirim 10 rekaman/10 detik (timestamp NTP).
  Arsip CSV harian → lihat `ARSIP_CSV.md`.

> ⚠️ **Penting — beda DEVICE_ID vs DEVICE_API_KEY:**
> - `DEVICE_ID` = identitas unik tiap unit (esp-01…esp-04). BEDA tiap ESP.
> - `DEVICE_API_KEY` = kunci rahasia bersama untuk lolos ke Worker. **SAMA** di semua unit
>   (harus cocok dengan secret `DEVICE_API_KEY` di Cloudflare). Saat ini bernilai `"esp-02"`.

### Cara flash tiap unit
1. Pasang **label fisik** di tiap probe & ESP (#1–#4) agar tidak tertukar.
2. Buka `firmware/esp-0X/esp-0X.ino` di Arduino IDE.
3. Pilih board **ESP32S3 Dev Module**, colok unit yang sesuai, **Upload**.
4. Serial Monitor 115200 → cek `[ADS1115] initialized` lalu `[HTTP] OK (code 201)`.
5. Verifikasi di Supabase **Table Editor → readings** (kolom `device` = esp-0X).

---

## Wiring Ringkas (tiap unit)

| Komponen | Koneksi |
|----------|---------|
| DS18B20 | GPIO 4 (+ pull-up 4.7kΩ) |
| ADS1115 VDD / GND | 3V3 / GND |
| ADS1115 SDA / SCL | GPIO 8 / GPIO 9 |
| ADS1115 ADDR | GND (alamat 0x48) |
| pH board Po | ADS1115 **A0** |
| pH board V+ / GND | 3V3 / GND |

Detail lengkap: `WIRING_ADS1115.md`. Semua di 3.3V → tanpa voltage divider.

---

## Library Arduino (Library Manager)
- OneWire (Paul Stoffregen)
- DallasTemperature (Miles Burton)
- Adafruit ADS1X15

---

## Cloudflare Worker (sudah deploy)

URL: `https://sensor-monitor.fahrurrohzi42.workers.dev`

Untuk update/deploy ulang Worker:
```bash
cd cloudflare
wrangler deploy
```
Secret yang tersimpan di Cloudflare (tidak ada di file): `SUPABASE_URL`,
`SUPABASE_SERVICE_KEY`, `DEVICE_API_KEY`. Lihat ulang/atur via:
```bash
wrangler secret list
wrangler secret put <NAMA>
```

Endpoint Worker:
| Method | Path | Fungsi |
|--------|------|--------|
| POST | `/api/sensor` | ESP kirim data (butuh header X-API-Key) |
| GET  | `/api/sensor` | data terbaru |
| GET  | `/api/history?device=eq.esp-01&limit=50` | riwayat (untuk Flutter) |

---

## Kalibrasi Ulang pH

Bila suatu probe terasa meleset, kalibrasi ulang:
1. Buka `firmware/kalibrasi_ph/kalibrasi_ph.ino`, upload.
2. Celup ke buffer 4.00 / 6.86 / 9.18, catat tegangan `V:` tiap buffer.
3. Hitung regresi 3 titik → `m` (PH_SLOPE) & `b` (PH_OFFSET).
4. Update di `firmware/esp-0X/` + catat di `kalibrasi_log.md`.

---

## Keamanan
- `service_role key` HANYA di Cloudflare Worker (sebagai secret). Jangan di ESP/Flutter.
- `anon key` boleh di Flutter (read-only via RLS).
- `DEVICE_API_KEY` kunci bersama ESP↔Worker — rahasiakan.
