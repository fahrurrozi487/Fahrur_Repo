# Panduan Migrasi ke Akun Supabase & Cloudflare Baru

Urut dari atas. **Jangan matikan setup lama** sampai yang baru terverifikasi (hindari kehilangan data).

> ⚠️ Yang OTOMATIS BERUBAH saat ganti akun:
> - **URL Worker** → akun Cloudflare baru = subdomain `*.workers.dev` baru → `WORKER_URL` di 4 sketch wajib diganti.
> - **Kunci Supabase** (URL, anon, service_role) → beda project = beda kunci.
> - **Secret & R2** tidak ikut pindah → set ulang di akun baru.

---

## BAGIAN 1 — Supabase Baru

1. Login ke akun Supabase baru → **New project** (region Singapore, simpan DB password).
2. **SQL Editor** → jalankan isi `supabase_setup.sql` (tabel + index + RLS + realtime).
   - Opsi B (Worker): boleh abaikan policy `allow_insert_anon`.
3. **Settings → API**, catat:
   - `Project URL` (baru)
   - `anon key` (baru) — untuk Flutter
   - `service_role key` (baru) — untuk Worker
4. Cek **Table Editor → readings** sudah ada.

---

## BAGIAN 2 — Cloudflare Baru

1. Logout akun lama, login akun baru:
   ```bash
   wrangler logout
   wrangler login
   ```
2. Set subdomain workers.dev (kalau diminta pertama kali) di dashboard akun baru.
3. Aktifkan **R2** di dashboard akun baru, lalu buat bucket:
   ```bash
   cd cloudflare
   wrangler r2 bucket create sensor-archive
   ```
4. Set **3 secret** (pakai kunci Supabase BARU):
   ```bash
   wrangler secret put SUPABASE_URL          # Project URL Supabase BARU
   wrangler secret put SUPABASE_SERVICE_KEY  # service_role BARU
   wrangler secret put DEVICE_API_KEY        # boleh sama: "esp32s3"
   ```
5. Deploy:
   ```bash
   wrangler deploy
   ```
   → **CATAT URL Worker baru**: `https://sensor-monitor.<SUBDOMAIN-BARU>.workers.dev`

6. Test:
   ```bash
   curl -H "X-API-Key: esp-02" https://sensor-monitor.<SUBDOMAIN-BARU>.workers.dev/api/sensor
   # harus {"error":"No data yet"} = Worker hidup & nyambung Supabase baru
   ```

---

## BAGIAN 3 — Update 4 Sketch ESP

Di tiap `firmware/esp-0X/esp-0X.ino`, ganti **1 baris**:
```cpp
const char* WORKER_URL = "https://sensor-monitor.<SUBDOMAIN-BARU>.workers.dev/api/sensor";
```
- `DEVICE_API_KEY` tetap (kalau secret-nya sama).
- `DEVICE_ID` & konstanta pH **tidak berubah**.
- WiFi tidak berubah.

Lalu **re-flash keempat ESP**. Serial Monitor harus kembali `[HTTP] batch 10 rekaman OK (code 201)`.

---

## BAGIAN 4 — (Opsional) Pindahkan Data Lama

**Data Supabase lama** (hanya ~2 hari karena auto-purge) — biasanya tidak penting dipindah.
Kalau perlu: Table Editor lama → Export CSV → import ke project baru.

**Arsip CSV di R2 lama** (ini arsip utama):
```bash
# unduh dari akun lama (login lama dulu)
wrangler r2 object get sensor-archive/data_2026-06-21.csv --file data_2026-06-21.csv
# ... ulangi tiap file ...
# upload ke akun baru (login baru dulu)
wrangler r2 object put sensor-archive/data_2026-06-21.csv --file data_2026-06-21.csv
```
Atau lewat dashboard R2: download semua file dari bucket lama → upload ke bucket baru.

---

## BAGIAN 5 — Beri Tahu Tim Flutter

Aplikasi Flutter perlu kredensial baru:
- Kalau baca **langsung Supabase**: kasih `Project URL` + `anon key` BARU.
- Kalau baca **lewat Worker**: kasih `WORKER_URL` baru.

---

## BAGIAN 6 — Matikan Setup Lama (setelah verifikasi)

Setelah data baru masuk normal selama beberapa jam:
- Hapus/pause project Supabase lama.
- Hapus Worker + bucket R2 lama (atau biarkan sampai yakin).

---

## Checklist Singkat
- [ ] Supabase baru: project + SQL + catat 3 kunci
- [ ] Cloudflare baru: login + R2 + bucket
- [ ] 3 secret di-set (pakai Supabase baru)
- [ ] `wrangler deploy` → catat URL baru
- [ ] Test `/api/sensor` → "No data yet"
- [ ] Ganti `WORKER_URL` di 4 sketch → re-flash
- [ ] Serial Monitor `code 201`
- [ ] (opsional) pindah arsip CSV lama
- [ ] Flutter dikasih kredensial baru
- [ ] Setup lama dimatikan
