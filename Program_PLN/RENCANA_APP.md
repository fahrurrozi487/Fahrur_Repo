# Rencana Aplikasi Flutter — Monitoring Sensor

Fitur: **monitoring live + grafik mingguan/bulanan + notifikasi alert**, dengan **login**.

## Arsitektur Data untuk App

```
ESP ─> Worker ─> Supabase:
   ├── readings         (per-detik, 2 hari)    ─> LIVE (realtime)
   ├── readings_minute  (per-menit, 30 hari)   ─> grafik PER-MENIT
   └── readings_hourly  (per-jam, permanen)    ─> grafik JAM/HARI/BULAN/TAHUN
 R2: CSV per-detik (arsip mentah, TIDAK dipakai app)
```

Pemetaan level grafik → sumber:

| Level grafik | Sumber | Cara |
|--------------|--------|------|
| Per menit | `readings_minute` | langsung (maks 30 hari) |
| Per jam | `readings_hourly` | langsung |
| Per hari / bulan / tahun | `readings_hourly` | diagregasi (date_trunc) |

Prinsip: **app baca langsung dari Supabase** (dengan login), via fungsi `get_series`
(satu fungsi untuk semua level). Hari/bulan/tahun cukup diturunkan dari data per-jam.

---

## A. Yang Ditambah di Supabase

### A1. Tabel agregat bertingkat + rollup otomatis
Jalankan di SQL Editor:
```sql
-- ===== PER-JAM (permanen, kecil) =====
create table if not exists readings_hourly (
  device text, hour timestamptz,
  temp_avg real, temp_min real, temp_max real,
  ph_avg real, ph_min real, ph_max real, n int,
  primary key (device, hour)
);

-- ===== PER-MENIT (retensi 30 hari) =====
create table if not exists readings_minute (
  device text, minute timestamptz,
  temp_avg real, temp_min real, temp_max real,
  ph_avg real, ph_min real, ph_max real, n int,
  primary key (device, minute)
);

create or replace function rollup_minute() returns void language sql as $$
  insert into readings_minute (device, minute, temp_avg, temp_min, temp_max, ph_avg, ph_min, ph_max, n)
  select device, date_trunc('minute', created_at),
         avg(temperature), min(temperature), max(temperature),
         avg(ph), min(ph), max(ph), count(*)
  from readings
  where created_at >= date_trunc('minute', now()) - interval '2 minute'
    and created_at <  date_trunc('minute', now())
  group by device, date_trunc('minute', created_at)
  on conflict (device, minute) do update set
    temp_avg=excluded.temp_avg, temp_min=excluded.temp_min, temp_max=excluded.temp_max,
    ph_avg=excluded.ph_avg, ph_min=excluded.ph_min, ph_max=excluded.ph_max, n=excluded.n;
$$;

create or replace function rollup_hourly() returns void language sql as $$
  insert into readings_hourly (device, hour, temp_avg, temp_min, temp_max, ph_avg, ph_min, ph_max, n)
  select device, date_trunc('hour', created_at),
         avg(temperature), min(temperature), max(temperature),
         avg(ph), min(ph), max(ph), count(*)
  from readings
  where created_at >= date_trunc('hour', now()) - interval '1 hour'
    and created_at <  date_trunc('hour', now())
  group by device, date_trunc('hour', created_at)
  on conflict (device, hour) do update set
    temp_avg=excluded.temp_avg, temp_min=excluded.temp_min, temp_max=excluded.temp_max,
    ph_avg=excluded.ph_avg, ph_min=excluded.ph_min, ph_max=excluded.ph_max, n=excluded.n;
$$;

-- Jadwal (pg_cron)
create extension if not exists pg_cron;
select cron.schedule('rollup-minute', '* * * * *', 'select rollup_minute()');  -- tiap menit
select cron.schedule('rollup-hourly', '5 * * * *', 'select rollup_hourly()');  -- tiap jam
select cron.schedule('purge-minute',  '15 0 * * *',
  $$delete from readings_minute where minute < now() - interval '30 days'$$);   -- buang >30 hari
```
> `readings_hourly` permanen (kecil); `readings_minute` hanya 30 hari.
> Rollup jalan sebelum purge → tidak ada data hilang.

### A1b. Fungsi serbaguna grafik `get_series` (semua level)
```sql
create or replace function get_series(p_device text, p_bucket text, p_from timestamptz, p_to timestamptz)
returns table(t timestamptz, temp_avg real, temp_min real, temp_max real,
              ph_avg real, ph_min real, ph_max real)
language sql stable as $$
  select date_trunc(p_bucket, s.t),
         (sum(s.temp_avg*s.n)/nullif(sum(s.n),0))::real,
         min(s.temp_min)::real, max(s.temp_max)::real,
         (sum(s.ph_avg*s.n)/nullif(sum(s.n),0))::real,
         min(s.ph_min)::real, max(s.ph_max)::real
  from (
    select minute as t, temp_avg, temp_min, temp_max, ph_avg, ph_min, ph_max, n
      from readings_minute
     where p_bucket='minute' and device=p_device and minute>=p_from and minute<p_to
    union all
    select hour as t, temp_avg, temp_min, temp_max, ph_avg, ph_min, ph_max, n
      from readings_hourly
     where p_bucket in ('hour','day','month','year') and device=p_device and hour>=p_from and hour<p_to
  ) s
  group by 1 order by 1;
$$;
grant execute on function get_series(text,text,timestamptz,timestamptz) to authenticated;
```
> App cukup panggil 1 fungsi ini dengan `p_bucket` = `minute`/`hour`/`day`/`month`/`year`.

### A2. Aktifkan Auth (login)
Dashboard Supabase → **Authentication → Providers → Email** (aktifkan).
- Buat user lewat **Authentication → Users → Add user** (atau aktifkan sign-up).
- Flutter pakai email+password.

### A3. Ubah RLS: hanya user login yang bisa baca
```sql
-- readings: ganti baca anon -> authenticated
drop policy if exists "allow_read_anon" on readings;
create policy "read_auth" on readings for select to authenticated using (true);

-- readings_hourly & readings_minute: aktifkan RLS + izin baca authenticated
alter table readings_hourly enable row level security;
create policy "read_hourly_auth" on readings_hourly for select to authenticated using (true);
alter table readings_minute enable row level security;
create policy "read_minute_auth" on readings_minute for select to authenticated using (true);
```
> Insert/purge tetap lewat Worker (service_role, bypass RLS) — tidak terpengaruh.

### A4. (Opsional) Tabel ambang batas alert
```sql
create table if not exists thresholds (
  device   text primary key,
  ph_min   real default 6.0, ph_max   real default 8.5,
  temp_min real default 0,   temp_max real default 40
);
alter table thresholds enable row level security;
create policy "thr_read" on thresholds for select to authenticated using (true);
-- isi default 4 device:
insert into thresholds(device) values ('esp-01'),('esp-02'),('esp-03'),('esp-04')
  on conflict do nothing;
```

---

## B. Worker
**Tidak ada perubahan.** App baca semua dari Supabase. R2/CSV tetap arsip mentah.

---

## C. Flutter (untuk tim app)

### Package (pubspec.yaml)
- `supabase_flutter` — koneksi + auth + realtime
- `fl_chart` — grafik
- `flutter_local_notifications` — notifikasi alert (di HP)

### Konfigurasi
- `Supabase.initialize(url: SUPABASE_URL, anonKey: ANON_KEY)`
- Kredensial: minta **Project URL + anon key** dari akun Supabase yang dipakai.

### Contoh query

**Login:**
```dart
await supabase.auth.signInWithPassword(email: e, password: p);
```

**Live per device (realtime) — ditampilkan sebagai ANGKA/gauge, bukan grafik:**
```dart
supabase.from('readings')
  .stream(primaryKey: ['id'])
  .eq('device', 'esp-01')
  .order('created_at', ascending: false)
  .limit(1);   // baris terbaru -> tampilkan suhu & pH sebagai angka besar
```
> Cadence: ESP kirim batch tiap **10 detik**, jadi angka live **refresh ~10 detik sekali**
> (data tetap beresolusi per-detik, hanya tibanya per 10 detik). Cukup untuk monitoring.
> Opsional: sparkline 60 detik terakhir → `readings ... limit(60)`.

**Grafik semua level (minute/hour/day/month/year) — 1 fungsi:**
```dart
final rows = await supabase.rpc('get_series', params: {
  'p_device': 'esp-01',
  'p_bucket': 'day',          // 'minute' | 'hour' | 'day' | 'month' | 'year'
  'p_from': from.toIso8601String(),
  'p_to':   to.toIso8601String(),
});
// tiap baris: {t, temp_avg, temp_min, temp_max, ph_avg, ph_min, ph_max}
// pakai *_avg untuk garis; *_min/*_max untuk area band (rentang)
```

**Ambang batas alert:**
```dart
final thr = await supabase.from('thresholds').select().eq('device','esp-01').single();
// bandingkan nilai live dgn thr['ph_min'], thr['ph_max'], dst.
```

### Struktur layar (saran)
1. Login
2. Beranda: 4 kartu device (suhu+pH live, indikator normal/alert)
3. Detail device: grafik (pilih rentang 1 hari / 7 hari / 30 hari) + status
4. Pengaturan ambang batas (opsional)

---

## D. Notifikasi — Sub-keputusan

| Pendekatan | Cara kerja | Kelebihan | Kekurangan |
|------------|-----------|-----------|------------|
| **In-app (lokal)** | App cek nilai vs ambang, munculkan notif lokal | Simpel, tanpa server tambahan | Hanya jalan saat app terbuka/background |
| **Push server (FCM)** | Server kirim push walau app tertutup | Selalu sampai | Perlu setup Firebase + simpan token + logika server |

**Saran:** mulai **In-app (lokal)** dulu (cepat), naik ke **FCM** kalau perlu alert saat app tertutup. FCM bisa dipicu dari Worker cron atau Supabase Edge Function (fase 2).

---

## Checklist
- [ ] A1: tabel `readings_minute` (30 hari) + `readings_hourly` (permanen) + rollup pg_cron
- [ ] A1b: fungsi `get_series` (grafik semua level)
- [ ] A2: aktifkan Auth + buat user
- [ ] A3: ubah RLS ke authenticated (readings, minute, hourly)
- [ ] A4: (opsional) tabel thresholds
- [ ] C: kasih tim Flutter — Supabase URL + anon key + skema + contoh query
- [ ] D: pilih pendekatan notifikasi (lokal dulu)
