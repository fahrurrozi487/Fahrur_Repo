-- =====================================================
--  Supabase Setup — Sensor Monitor (DS18B20 + pH)
--  Jalankan di Supabase Dashboard > SQL Editor
-- =====================================================

-- 1. Tabel utama
create table if not exists readings (
  id            bigint generated always as identity primary key,
  temperature   real,
  ph            real,
  water_quality real,           -- TDS (ppm), ditambahkan 2026-07
  device        text,
  created_at    timestamptz default now()
);

-- 1b. Migrasi untuk DB yang sudah ada sebelum kolom TDS (aman diulang)
alter table readings add column if not exists water_quality real;

-- 2. Index untuk query history (urut waktu terbaru)
create index if not exists idx_readings_created_at
  on readings (created_at desc);

-- 2b. Index untuk query per-device (mis. history 1 ESP) — penting untuk 4 unit
create index if not exists idx_readings_device_time
  on readings (device, created_at desc);

-- 3. Aktifkan Row Level Security
alter table readings enable row level security;

-- =====================================================
--  KEBIJAKAN AKSES (RLS POLICIES)
-- =====================================================

-- 4. Izinkan SELECT untuk anon key (dipakai Flutter membaca history)
create policy "allow_read_anon"
  on readings for select
  to anon
  using (true);

-- 5. INSERT:
--    OPSI A (ESP langsung) -> ESP pakai anon key, jadi anon perlu izin INSERT.
--    OPSI B (via Worker)   -> Worker pakai service_role (bypass RLS),
--                             jadi policy INSERT untuk anon TIDAK diperlukan.
--
--    Aktifkan policy di bawah HANYA jika pakai OPSI A:
create policy "allow_insert_anon"
  on readings for insert
  to anon
  with check (true);

-- =====================================================
--  REALTIME (opsional, untuk live update Flutter nanti)
-- =====================================================

-- 6. Tambahkan tabel ke publikasi realtime
alter publication supabase_realtime add table readings;

-- =====================================================
--  CATATAN KEAMANAN
-- =====================================================
-- - anon key boleh tersebar (read-only aman karena RLS).
-- - service_role key JANGAN PERNAH ditaruh di ESP atau Flutter.
--   Hanya boleh di server/Worker (Opsi B).
-- - Untuk Opsi A, pertimbangkan menonaktifkan "allow_insert_anon"
--   dan ganti dengan mekanisme auth bila perangkat dipakai publik.
