# Arsip CSV Harian (per-detik) — Cloudflare R2

Sistem merekam pH+suhu **tiap detik** dan mengarsipkannya ke **CSV harian** di
Cloudflare R2. Supabase hanya jadi penyangga **2 hari** (untuk Flutter realtime).

## Alur

```
ESP (rekam tiap 1 detik, kirim batch 10 rekaman/10 detik, timestamp NTP)
        │
        ▼
Cloudflare Worker ──bulk insert──> Supabase (rolling 2 hari)
        │
        └── Cron tiap 10 menit:
              1. flush baris baru -> append ke R2: data_YYYY-MM-DD.csv (tanggal WIB)
              2. purge baris >2 hari yang SUDAH diarsipkan
```

Format CSV: `timestamp,device,temperature,ph` (1 file/hari, semua ESP jadi satu).

---

## Setup (sekali saja)

### 1. Aktifkan R2 & buat bucket

```bash
cd cloudflare
wrangler r2 bucket create sensor-archive
```

> ⚠️ **Catatan:** mengaktifkan R2 pertama kali biasanya minta **menambahkan kartu**
> di dashboard Cloudflare (https://dash.cloudflare.com → R2). Free tier R2 = **10 GB
> gratis + tanpa biaya egress**, jadi tidak akan ada tagihan selama di bawah batas.
> Kalau tidak mau pakai kartu, lihat bagian "Alternatif tanpa R2" di bawah.

### 2. Deploy Worker (dengan cron + R2)

```bash
cd cloudflare
wrangler deploy
```
Output harus menampilkan `Schedule: */10 * * * *` dan binding `ARCHIVE`.

### 3. Flash ulang ESP

Sketch `firmware/esp-0X/` versi terbaru sudah mode per-detik+batch.
Upload ke tiap unit. Cek Serial Monitor:
```
[NTP] sync ok
[rec 1/10] ... [rec 10/10] ...
[HTTP] batch 10 rekaman OK (code 201)
```

---

## Test Tanpa Menunggu Cron

Trigger manual flush+purge (butuh header X-API-Key):
```bash
curl -H "X-API-Key: esp-02" https://sensor-monitor.fahrurrohzi42.workers.dev/api/flush
```
Balasan contoh: `{"flush":{"flushed":120,"files":["data_2026-06-21.csv"]},...}`

## Mengunduh / Melihat CSV

```bash
# list semua file CSV
wrangler r2 object get sensor-archive --help     # lihat opsi
# unduh satu file
wrangler r2 object get sensor-archive/data_2026-06-21.csv --file data_2026-06-21.csv
```
Atau lewat dashboard: **R2 → sensor-archive → klik file → Download**.

---

## Estimasi Ukuran
- 4 ESP × per-detik = ~345.600 baris/hari
- CSV ≈ **~14 MB/hari** (~5 GB/tahun) → muat nyaman di R2 (10 GB gratis)
- Supabase tetap kecil (~2 hari data, sisanya di-purge)

---

## Alternatif Tanpa R2 (jika tak mau pakai kartu)

Ganti penyimpanan CSV ke **Supabase Storage** (1 GB gratis, ~70 hari per-detik).
Butuh ubah Worker agar upload via Supabase Storage API alih-alih R2. Kabari kalau
mau versi ini — strukturnya mirip, hanya beda target simpan.

---

## Catatan
- Timestamp memakai **NTP (UTC)**; nama file CSV pakai **tanggal WIB (UTC+7)**.
- Purge aman: hanya menghapus baris yang **sudah** masuk CSV (tidak akan kehilangan data
  walau flush sempat gagal).
- Ubah retensi Supabase di `worker.js` (`RETENTION_DAYS`), interval cron di `wrangler.toml`.
