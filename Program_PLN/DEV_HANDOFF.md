# Handoff Developer App — Konektivitas Data

Dokumen acuan untuk tim Flutter agar terhubung ke data sensor.
App membaca **langsung dari Supabase** (dengan login). Tidak perlu akses Cloudflare.

---

## 0. Kredensial yang Dibutuhkan (isi sebelum dibagikan)

Minta ke pemilik proyek, isi di sini, lalu bagikan **secara aman** (jangan commit ke git publik):

| Item | Nilai | Dari mana |
|------|-------|-----------|
| `SUPABASE_URL` | `https://________.supabase.co` | Supabase → Settings → API → Project URL |
| `SUPABASE_ANON_KEY` | `eyJhbGci...` | Supabase → Settings → API → `anon public` |
| Akun test (email) | `________` | Supabase → Authentication → Users |
| Akun test (password) | `________` | dibuat saat Add user |

> `anon key` aman dipakai di app (read-only via RLS). **service_role key TIDAK boleh** ada di app.

---

## 1. Arsitektur Data

```
                     Supabase (Postgres)
   ┌──────────────────────────────────────────────┐
   │ readings         per-detik, 2 hari   -> LIVE   │
   │ readings_minute  per-menit, 30 hari  -> grafik │
   │ readings_hourly  per-jam, permanen   -> grafik │
   │ thresholds       ambang batas alert            │
   └──────────────────────────────────────────────┘
        ▲ baca (login)                 ▲ realtime
        └──────────── App Flutter ─────┘
```

- **Live** (angka realtime) ← `readings`
- **Grafik** (menit/jam/hari/bulan/tahun) ← fungsi `get_series` (otomatis pilih tabel)
- **Alert** ← bandingkan nilai live dengan `thresholds`

---

## 2. Daftar Device

`esp-01`, `esp-02`, `esp-03`, `esp-04` (kolom `device`).

---

## 3. Model Data

### `readings` (live, per-detik, retensi 2 hari)
| kolom | tipe | catatan |
|-------|------|---------|
| id | int8 | PK |
| device | text | esp-01..04 |
| temperature | float | °C (bisa null jika sensor error) |
| ph | float | |
| created_at | timestamptz | **UTC** |

### `readings_minute` (30 hari) & `readings_hourly` (permanen)
| kolom | tipe | catatan |
|-------|------|---------|
| device | text | |
| minute / hour | timestamptz | **UTC**, awal bucket |
| temp_avg, temp_min, temp_max | float | |
| ph_avg, ph_min, ph_max | float | |
| n | int | jumlah sampel di bucket |

### `thresholds`
| kolom | tipe | default |
|-------|------|---------|
| device | text (PK) | |
| ph_min, ph_max | float | 6.0 / 8.5 |
| temp_min, temp_max | float | 0 / 40 |

> Semua timestamp **UTC**. Konversi ke WIB (UTC+7) saat ditampilkan.

---

## 4. Autentikasi (WAJIB)

RLS aktif → **harus login** untuk membaca apa pun. Tanpa sesi login, query mengembalikan kosong.

```dart
await supabase.auth.signInWithPassword(email: email, password: password);
```

---

## 5. Pola Akses Data (Kontrak)

### 5a. Live (realtime) — tampil sebagai ANGKA
```dart
supabase.from('readings')
  .stream(primaryKey: ['id'])
  .eq('device', 'esp-01')
  .order('created_at', ascending: false)
  .limit(1);
```
> Refresh ~10 detik sekali (ESP kirim batch 10 dtk). Untuk sparkline: `.limit(60)`.

### 5b. Grafik semua level — fungsi `get_series`
```dart
final rows = await supabase.rpc('get_series', params: {
  'p_device': 'esp-01',
  'p_bucket': 'day',                       // minute | hour | day | month | year
  'p_from': from.toUtc().toIso8601String(),
  'p_to':   to.toUtc().toIso8601String(),
});
// tiap baris: { t, temp_avg, temp_min, temp_max, ph_avg, ph_min, ph_max }
// garis = *_avg ; area band = *_min..*_max
```
Aturan `p_bucket`:
| p_bucket | sumber | jangkauan |
|----------|--------|-----------|
| `minute` | readings_minute | maks 30 hari ke belakang |
| `hour` / `day` / `month` / `year` | readings_hourly | tak terbatas |

### 5c. Ambang batas (alert)
```dart
final thr = await supabase.from('thresholds').select().eq('device','esp-01').maybeSingle();
// alert jika ph < thr['ph_min'] || ph > thr['ph_max'] || suhu di luar temp_min..temp_max
```

---

## 6. Aturan & Batasan Penting
- **Wajib login** (RLS). Anon tidak bisa baca.
- **Timestamp UTC** — konversi ke WIB untuk tampilan; kirim `p_from`/`p_to` dalam UTC.
- **Live cadence ~10 detik** (bukan 1 detik), walau resolusi data per-detik.
- **Per-menit hanya 30 hari**. Jam/hari/bulan/tahun tak terbatas.
- `temperature` bisa `null` (sensor DS18B20 lepas) — tangani di UI.
- Realtime sudah aktif di tabel `readings`.

---

## 7. Setup Flutter

`pubspec.yaml`:
```yaml
dependencies:
  supabase_flutter: ^2.5.0
  fl_chart: ^0.68.0
  flutter_local_notifications: ^17.0.0
```

`main.dart`:
```dart
await Supabase.initialize(
  url: 'SUPABASE_URL',
  anonKey: 'SUPABASE_ANON_KEY',
);
final supabase = Supabase.instance.client;
```

---

## 8. Service Siap-Pakai

Lihat **`contoh_flutter/sensor_service.dart`** — kelas `SensorService` lengkap:
login, live stream, sparkline, `getSeries`, thresholds, + model data. Tinggal salin.

---

## 9. Saran Struktur Layar
1. **Login**
2. **Beranda** — 4 kartu device (suhu+pH live, indikator normal/alert)
3. **Detail device** — grafik (pilih: menit/jam/hari/bulan/tahun) + status
4. (opsional) **Pengaturan ambang batas**
