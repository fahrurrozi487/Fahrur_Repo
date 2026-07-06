-- =====================================================
--  app_setup.sql — Konfigurasi Supabase untuk Aplikasi
--  Jalankan SEKALI di Supabase Dashboard > SQL Editor.
--  Aman dijalankan ulang (idempotent).
--
--  Mencakup:
--   1. Tabel agregat: readings_minute (30 hari) & readings_hourly (permanen)
--   2. Fungsi rollup + jadwal pg_cron (menit, jam, purge)
--   3. Fungsi get_series (grafik semua level: minute/hour/day/month/year)
--   4. RLS: baca hanya untuk user login (authenticated)
--   5. Tabel thresholds (ambang batas alert)
--
--  CATATAN: mengaktifkan Auth + membuat user dilakukan di
--  Dashboard > Authentication (tidak bisa lewat SQL).
-- =====================================================

-- ===== 1. TABEL AGREGAT =====
create table if not exists readings_minute (
  device text, minute timestamptz,
  temp_avg real, temp_min real, temp_max real,
  ph_avg real, ph_min real, ph_max real, n int,
  primary key (device, minute)
);

create table if not exists readings_hourly (
  device text, hour timestamptz,
  temp_avg real, temp_min real, temp_max real,
  ph_avg real, ph_min real, ph_max real, n int,
  primary key (device, hour)
);

-- ===== 2. FUNGSI ROLLUP =====
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

-- ===== 3. JADWAL pg_cron =====
create extension if not exists pg_cron;

-- hapus jadwal lama bila ada (agar tidak dobel saat dijalankan ulang)
select cron.unschedule('rollup-minute') where exists (select 1 from cron.job where jobname='rollup-minute');
select cron.unschedule('rollup-hourly') where exists (select 1 from cron.job where jobname='rollup-hourly');
select cron.unschedule('purge-minute')  where exists (select 1 from cron.job where jobname='purge-minute');

select cron.schedule('rollup-minute', '* * * * *', 'select rollup_minute()');   -- tiap menit
select cron.schedule('rollup-hourly', '5 * * * *', 'select rollup_hourly()');   -- tiap jam
select cron.schedule('purge-minute',  '15 0 * * *',
  $$delete from readings_minute where minute < now() - interval '30 days'$$);    -- buang >30 hari

-- ===== 4. FUNGSI GRAFIK get_series (semua level) =====
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

-- ===== 5. RLS: baca hanya untuk user login =====
-- readings: ganti baca anon -> authenticated
drop policy if exists "allow_read_anon" on readings;
drop policy if exists "read_auth" on readings;
create policy "read_auth" on readings for select to authenticated using (true);

alter table readings_minute enable row level security;
drop policy if exists "read_minute_auth" on readings_minute;
create policy "read_minute_auth" on readings_minute for select to authenticated using (true);

alter table readings_hourly enable row level security;
drop policy if exists "read_hourly_auth" on readings_hourly;
create policy "read_hourly_auth" on readings_hourly for select to authenticated using (true);

-- ===== 6. TABEL THRESHOLDS (ambang batas alert) =====
create table if not exists thresholds (
  device   text primary key,
  ph_min   real default 6.0, ph_max   real default 8.5,
  temp_min real default 0,   temp_max real default 40
);
alter table thresholds enable row level security;
drop policy if exists "thr_read" on thresholds;
create policy "thr_read" on thresholds for select to authenticated using (true);

insert into thresholds(device) values ('esp-01'),('esp-02'),('esp-03'),('esp-04')
  on conflict do nothing;

-- =====================================================
--  SELESAI. Langkah manual yang TIDAK bisa lewat SQL:
--   - Dashboard > Authentication > Providers > Email: aktifkan
--   - Dashboard > Authentication > Users > Add user: buat akun untuk app
-- =====================================================
