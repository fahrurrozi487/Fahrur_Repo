# Panduan Setup dari Nol — Supabase & Cloudflare Worker

Panduan untuk pemula total. Ikuti urut dari atas.

---

## Bagian 0 — Pahami Dulu (1 menit baca)

| Komponen | Fungsi | Wajib? |
|----------|--------|--------|
| **Supabase** | Database tempat menyimpan data sensor | ✅ WAJIB (kedua opsi) |
| **Cloudflare Worker** | Perantara antara ESP & Supabase | ⚠️ Hanya untuk **Opsi B** |

> 💡 **Saran untuk pemula:** kerjakan **Opsi A** dulu (cuma Supabase, tanpa Cloudflare).
> Setelah data sensor berhasil masuk, baru pertimbangkan Opsi B kalau butuh keamanan ekstra.

Bedanya:
- **Opsi A:** ESP32 → langsung ke Supabase. Simpel.
- **Opsi B:** ESP32 → Cloudflare Worker → Supabase. Lebih aman (key rahasia tidak di ESP).

---

# BAGIAN 1 — SUPABASE (WAJIB)

## 1.1 Daftar Akun

1. Buka https://supabase.com
2. Klik **Start your project** (kanan atas)
3. Login pakai **GitHub** (paling mudah) atau email
4. Gratis, tidak perlu kartu kredit

## 1.2 Buat Project Baru

1. Setelah login, klik **New project**
2. Isi:
   - **Name**: `sensor-monitor` (bebas)
   - **Database Password**: buat password kuat → **SIMPAN BAIK-BAIK** (dipakai kalau akses database langsung)
   - **Region**: pilih **Southeast Asia (Singapore)** ← terdekat dari Indonesia
3. Klik **Create new project**
4. Tunggu ~2 menit sampai project selesai disiapkan (ada loading)

## 1.3 Buat Tabel (jalankan SQL)

1. Di menu kiri, klik ikon **SQL Editor** (logo `</>`)
2. Klik **+ New query**
3. Buka file `supabase_setup.sql` di komputermu, **copy semua isinya**
4. **Paste** ke kotak editor
5. **PENTING — sesuaikan dulu:**
   - Kalau pakai **Opsi A**: biarkan semua (policy `allow_insert_anon` aktif)
   - Kalau pakai **Opsi B**: hapus blok `create policy "allow_insert_anon" ...`
     (Worker pakai service_role yang bypass aturan ini)
6. Klik **Run** (atau Ctrl+Enter)
7. Harus muncul **Success. No rows returned** → berarti berhasil

## 1.4 Ambil Kredensial (kunci API)

1. Di menu kiri, klik ikon **gear/Settings** (paling bawah)
2. Klik **API**
3. Catat 3 hal ini (klik Copy):

   | Nama di Supabase | Dipakai untuk | Rahasia? |
   |------------------|---------------|----------|
   | **Project URL** | semua | tidak |
   | **anon / public** key | Opsi A & Flutter | tidak (aman tersebar) |
   | **service_role** key | Opsi B (Worker saja) | 🔴 SANGAT RAHASIA |

   > 🔴 **service_role key JANGAN PERNAH** ditaruh di kode ESP atau Flutter.
   > Hanya boleh di Cloudflare Worker.

## 1.5 Cek Tabel Sudah Ada

1. Menu kiri → **Table Editor**
2. Harus ada tabel **readings** dengan kolom: id, temperature, ph, device, created_at
3. Masih kosong → normal, nanti terisi saat ESP kirim data

---

## ✅ Kalau pakai OPSI A — Supabase selesai!

Tinggal isi ke sketch `opsi_A_langsung/Sensor_Monitor_Supabase.ino`:
```cpp
const char* SUPABASE_URL      = "https://xxxx.supabase.co";   // Project URL
const char* SUPABASE_ANON_KEY = "eyJhbGci...";                // anon key
```
Lalu isi WiFi, DEVICE_ID, konstanta pH → upload. **Lewati Bagian 2.**

---

# BAGIAN 2 — CLOUDFLARE WORKER (hanya untuk OPSI B)

## 2.1 Daftar Akun Cloudflare

1. Buka https://dash.cloudflare.com/sign-up
2. Daftar pakai email + password
3. Verifikasi email
4. Gratis, tidak perlu kartu kredit

## 2.2 Install Node.js (sekali saja)

Wrangler (alat deploy Worker) butuh Node.js.

1. Buka https://nodejs.org
2. Download versi **LTS** (tombol kiri)
3. Install seperti biasa (Next → Next → Finish)
4. Cek berhasil — buka terminal/PowerShell, ketik:
   ```bash
   node --version
   ```
   Harus muncul angka versi (mis. v20.x.x)

## 2.3 Install Wrangler

Di terminal:
```bash
npm install -g wrangler
```
Cek:
```bash
wrangler --version
```

## 2.4 Login ke Cloudflare

```bash
wrangler login
```
- Browser akan terbuka otomatis
- Klik **Allow** untuk memberi izin
- Kembali ke terminal, harus muncul "Successfully logged in"

> 💡 Di Claude Code, kamu bisa menjalankan perintah ini dengan mengetik
> `! wrangler login` di prompt agar outputnya langsung muncul di sesi.

## 2.5 Masuk ke Folder Worker

```bash
cd "C:/Users/MUHAMMAD FAHRUR ROZI/OneDrive/Documents/Program_PLN/cloudflare"
```

## 2.6 Set 3 Secret (kunci rahasia)

Jalankan satu per satu. Tiap perintah akan minta kamu paste nilainya:

```bash
wrangler secret put SUPABASE_URL
# paste: https://xxxx.supabase.co  (Project URL dari langkah 1.4)

wrangler secret put SUPABASE_SERVICE_KEY
# paste: service_role key dari langkah 1.4

wrangler secret put DEVICE_API_KEY
# paste: buat password bebas, mis. "rahasia-esp-2026" — INI yang dipakai ESP
```

> ⚠️ Saat pertama `wrangler secret put`, mungkin diminta konfirmasi membuat Worker
> baru bernama `sensor-monitor` → ketik **y** / Enter.

## 2.7 Deploy Worker

```bash
wrangler deploy
```
- Tunggu beberapa detik
- Setelah sukses, muncul URL seperti:
  ```
  https://sensor-monitor.NAMA-KAMU.workers.dev
  ```
- **CATAT URL ini** — ini alamat Worker-mu

## 2.8 Isi ke Sketch ESP (Opsi B)

Di `firmware/esp-0X/esp-0X.ino` (tiap unit):
```cpp
const char* WORKER_URL     = "https://sensor-monitor.NAMA-KAMU.workers.dev/api/sensor";
const char* DEVICE_API_KEY = "rahasia-esp-2026";  // SAMA dengan secret di 2.6
```

## 2.9 Test Worker (opsional, tanpa ESP)

Buka URL ini di browser:
```
https://sensor-monitor.NAMA-KAMU.workers.dev/api/sensor
```
- Kalau muncul `{"error":"No data yet"}` → Worker hidup & terhubung Supabase ✅
- (Data sensor belum ada, jadi wajar kosong)

---

# BAGIAN 3 — Checklist Akhir

## Opsi A
- [ ] Project Supabase dibuat
- [ ] SQL dijalankan (tabel `readings` ada)
- [ ] Project URL + anon key dicatat
- [ ] Diisi ke sketch + WiFi + DEVICE_ID + konstanta pH
- [ ] Upload → Serial Monitor `[HTTP] OK (code 201)`
- [ ] Data muncul di Table Editor

## Opsi B (tambahan)
- [ ] Akun Cloudflare dibuat
- [ ] Node.js + wrangler terinstall
- [ ] `wrangler login` sukses
- [ ] 3 secret di-set (URL, service_role, device key)
- [ ] `wrangler deploy` sukses → dapat URL
- [ ] URL + device key diisi ke sketch
- [ ] Upload → data muncul di Table Editor

---

# Troubleshooting Umum

| Masalah | Solusi |
|---------|--------|
| `[HTTP] GAGAL (code 401)` | Key salah. Opsi A: cek anon key. Opsi B: cek DEVICE_API_KEY sama persis |
| `[HTTP] GAGAL (code 404)` | URL salah. Pastikan diakhiri `/rest/v1/readings` (A) atau `/api/sensor` (B) |
| Data tidak masuk tabel | Opsi A: policy `allow_insert_anon` belum aktif → jalankan ulang SQL-nya |
| `wrangler: command not found` | Node.js/wrangler belum terinstall (lihat 2.2–2.3) |
| Worker error 502 | service_role key salah di secret → ulangi `wrangler secret put SUPABASE_SERVICE_KEY` |
| Lupa anon/service key | Supabase → Settings → API (bisa dilihat ulang kapan saja) |

---

# Ringkasan Alur Data

**Opsi A:**
```
ESP32 --(anon key)--> Supabase REST API --> tabel readings --> Flutter baca
```

**Opsi B:**
```
ESP32 --(device key)--> Cloudflare Worker --(service_role)--> Supabase --> Flutter baca
```
