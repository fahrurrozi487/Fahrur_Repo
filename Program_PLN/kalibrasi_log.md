# Log Kalibrasi Sensor pH

Catatan konstanta kalibrasi tiap probe pH (ADS1115 @ 3.3V, ESP32-S3 N16R8).
Rumus: `pH = m × Volt + b`  →  `PH_SLOPE = m`, `PH_OFFSET = b`.

Buffer standar yang dipakai: **pH 4.01 / 6.86 / 9.18** (buffer baru per 2026-06-24).
Konstanta dihitung via regresi linear 3 titik (titik tengah rentang bacaan).

> Probe pH wajar mengalami drift seiring pemakaian. Kalibrasi ulang berkala
> (mis. tiap 2–4 minggu, atau saat bacaan terasa meleset).

---

## Sensor #1

| Tanggal | V@pH4 | V@pH6.86 | V@pH9.18 | m (slope) | b (offset) | Error | Status |
|---------|-------|----------|----------|-----------|-----------|-------|--------|
| (awal)     | —      | —      | —      | -7.654 | 27.530 | <0.05 | drift  |
| 2026-06-19 | 3.0750 | 2.7350 | 2.4350 | -8.101 | 28.943 | <0.07 | diganti |
| 2026-06-20 | 3.1100 | 2.7480 | 2.4400 | -7.736 | 28.081 | <0.04 | buffer kadaluarsa |
| 2026-06-24 | 2.9710 | 2.6350 | 2.3450 | -8.265 | 28.587 | <0.03 (R²=0.9997) | drift (V@pH4 meleset) |
| 2026-06-25 | 3.0481 | 2.6724 | 2.3432 | -7.341 | 26.415 | tanpa beban | ❌ meleset di operasi |
| 2026-06-25 op | 2.9174 | 2.6230 | 2.3144 | **-7.518** | **26.579** | pH6.86/9.18 pas; pH4→4.6 | ✅ aktif (fit 2-titik, kondisi operasi) |

Catatan baris **2026-06-25 op** (PENTING):
- Tegangan diukur pada **KONDISI OPERASI** (esp-01.ino asli: WiFi nyala + DS18B20 aktif),
  dari kalibrasi_ulang_sensor1/S1PH4.png, S1PH6.png, S1PH9.png. Data sudah settle (SD <8 mV).
- **Temuan:** di bawah beban, suplai 3.3V board pH *sag* tak-merata → tegangan probe turun
  paling besar di pH4 (−120 mV) dan kecil di pH9 (−20 mV). Akibatnya respon **tidak linier**
  (slope 0.103 vs 0.133 V/pH), R² fit 3-titik turun ke 0.995 dan meleset −0.22 di pH6.86.
- Karena itu dipakai **fit 2-titik (pH6.86 & 9.18)** = rentang kerja aquaponik. Hasil: pH6.86→6.86
  dan pH9.18→9.18 (pas), tetapi buffer asam pH4 terbaca ~4.6 (sengaja dikorbankan, tak relevan
  untuk aquaponik yang beroperasi di pH 6–8).
- Konstanta lama −7.341/26.415 (kalibrasi tanpa-beban) DIBATALKAN karena membuat bacaan
  operasi meleset jauh (pH4→4.92, pH6.86→7.07, pH9.18→9.36).
- **Catatan untuk esp-02/03/04:** kemungkinan mengalami sag suplai yang sama. Jika bacaan operasi
  meleset, kalibrasi ulang dengan metode kondisi-operasi yang sama (lihat di atas).
- **Perbaikan akar masalah (opsional):** beri board pH catu 3.3V terfilter (kapasitor 100µF+100nF
  dekat V+/G, atau catu terpisah) → probe akan linier lagi & akurat di semua rentang.

---

## Sensor #2

| Tanggal | V@pH4 | V@pH6.86 | V@pH9.18 | m (slope) | b (offset) | Error | Status |
|---------|-------|----------|----------|-----------|-----------|-------|--------|
| 2026-06-19 | 3.1200 | 2.7300 | 2.3850 | -7.054 | 26.043 | <0.07 | diganti |
| 2026-06-20 | 3.1370 | 2.7250 | 2.3760 | -6.811 | 25.383 | <0.04 | buffer kadaluarsa |
| 2026-06-24 | 3.1380 | 2.7130 | 2.3530 | **-6.590** | **24.704** | <0.03 (R²=0.9999) | ✅ aktif (buffer baru) |

Catatan: baris 2026-06-24 dari TEGANGAN ASLI buffer SEGAR (Sensor2ph4.01/6.86/9.18.png),
buffer pH 4.01. Kalibrasi 2026-06-20 dibatalkan karena buffer berubah nilai.

---

## Sensor #3

| Tanggal | V@pH4 | V@pH6.86 | V@pH9.18 | m (slope) | b (offset) | Error | Status |
|---------|-------|----------|----------|-----------|-----------|-------|--------|
| 2026-06-19 | 3.0750 | 2.6408 | 2.2705 | -6.443 | 23.833 | <0.04 | balik-hitung |
| 2026-06-20 | 3.0700 | 2.6330 | 2.2760 | -6.525 | 24.035 | <0.01 | buffer kadaluarsa |
| 2026-06-24 | 3.0880 | 2.6330 | 2.2530 | **-6.194** | **23.147** | <0.02 (R²=0.99995) | ✅ aktif (buffer baru) |

Catatan: baris 2026-06-24 dihitung dari TEGANGAN ASLI buffer SEGAR (Sensor3ph4.01/6.86/9.18.png),
buffer pH 4.01. Fit terbaik dari semua sensor (R²=0.99995).
Kalibrasi 2026-06-20 dibatalkan karena buffer berubah nilai.

---

## Sensor #4

| Tanggal | V@pH4 | V@pH6.86 | V@pH9.18 | m (slope) | b (offset) | Error | Status |
|---------|-------|----------|----------|-----------|-----------|-------|--------|
| 2026-06-19 | 3.0394 | 2.6398 | 2.3169 | -7.169 | 25.788 | <0.02 | balik-hitung |
| 2026-06-20 | 3.0450 | 2.6450 | 2.3100 | -7.051 | 25.482 | <0.03 | buffer kadaluarsa |
| 2026-06-24 | 3.0570 | 2.6630 | 2.3190 | **-7.011** | **25.470** | <0.04 (R²=0.9996) | ✅ aktif (buffer baru) |

Catatan: baris 2026-06-24 dihitung dari TEGANGAN ASLI buffer SEGAR (Sensor4ph4.01/6.86/9.18.png),
buffer pH 4.01. Kalibrasi 2026-06-20 dibatalkan karena buffer berubah nilai.

---

> **2026-07-02 (esp-02, arsitektur catu baru):** Board pH tipe ini native **5V**, sebelumnya
> keliru dicatu 3.3V → op-amp mepet rail → output terjepit 2.5–3.3V, non-linier, itu sumber
> "sag" yang didokumentasikan 2026-06-25. Perbaikan: **board pH dicatu 5V**, output `Po`
> lewat **divider 10k/10k (/2)** sebelum masuk ADS1115 (ADS tetap 3.3V, GAIN_ONE cukup krn
> maks ~1.3V). Pot offset esp-02 mentok di Po=2.6V (short BNC) — masih di dalam jendela kerja,
> **jangan sentuh pot lagi**.
>
> Data (v = bacaan ADS setelah divider, saat settle): pH4.01→1.836, pH6.86→1.672, pH9.18→1.515.
> Melengkung spt esp-01 → fit 2-titik rentang kerja (6.86 & 9.18), titik 4.01 dikorbankan.
> Konstanta: **PH_SLOPE=-14.777, PH_OFFSET=31.567**. Diambil via `ukur_tegangan/ukur_tegangan.ino`
> (screenshot di `firmware/hasil_sensor2/`). ⚠️ Noise ±0.02V (≈±0.30 pH mentah, EMA redam ~±0.07)
> — board ini paling butuh kapasitor 100µF∥100nF di V+/G bila operasi nyata goyang.
>
> **esp-03 & esp-04 belum** dikalibrasi ulang dgn arsitektur 5V+divider — konstanta lama
> (3.3V, tanpa divider) HANGUS, wajib ulang metode yang sama.

---

## Pemetaan Sensor → Device (untuk rencana 4 ESP)

Isi saat tiap unit dirakit. Tiap ESP di-flash dengan m & b probe-nya sendiri.

| Device ID | Sensor | m (slope) | b (offset) | Lokasi | Catatan |
|-----------|--------|-----------|-----------|--------|---------|
| esp-01 | #1 | -7.518 | 26.579 | (isi)  | fit 2-titik kondisi operasi 2026-06-25 |
| esp-02 | #2 | -14.777 | 31.567 | (isi)  | fit 2-titik operasi + divider /2, 2026-07-02 |
| esp-03 | #3 | (isi ulang) | (isi ulang) | (isi)  | auto-calib 2026-07-02 hasil -16.270/34.893 (5V+divider, blm verifikasi); firmware di-placeholder |
| esp-04 | #4 | -7.011 | 25.470 | (isi)  | kalibrasi 2026-06-24 |

> **2026-06-24:** Keempat sensor dikalibrasi ulang dengan buffer SEGAR (pH 4.01 / 6.86 / 9.18)
> karena buffer lama sudah berubah nilai (terbuka di udara terlalu lama). Semua firmware
> esp-01..04 sudah di-update dengan konstanta di atas — **WAJIB re-flash keempat ESP**.
>
> Selain itu, firmware kini menerapkan **EMA (moving average) `EMA_ALPHA = 0.1`** pada pH
> sebelum disimpan ke buffer batch, sehingga nilai pH yang dikirim ke Supabase sudah halus
> (bukan pH mentah per-detik). Trimmed-mean 31 sampel tetap dipakai (meredam noise dalam 1 detik);
> EMA meredam fluktuasi antar-detik.

---

## Cara kalibrasi (ringkas)

1. Flash `baca_ph_ads1115/baca_ph_ads1115.ino`, buka Serial Monitor 115200.
2. Celup probe ke tiap buffer (4.00 → 6.86 → 9.18), bilas+keringkan tiap pindah.
3. Tunggu kolom `V:` stabil, catat tegangannya.
4. Hitung regresi linear 3 titik → dapat m & b.
5. Masukkan ke `PH_SLOPE` & `PH_OFFSET`, catat di tabel atas.

Rumus 2 titik (bila hanya pH 4 & pH 9.18):
```
m = (4.00 - 9.18) / (V4 - V9)
b = 4.00 - m × V4
```
