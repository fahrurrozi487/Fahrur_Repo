// sensor_service.dart
// Service siap-pakai untuk akses data sensor dari Supabase.
// Dependensi: supabase_flutter (^2.x)
//
// Inisialisasi di main() sebelum runApp():
//   await Supabase.initialize(url: SUPABASE_URL, anonKey: SUPABASE_ANON_KEY);

import 'package:supabase_flutter/supabase_flutter.dart';

/// Resolusi grafik. Nilai .api dikirim ke fungsi SQL get_series.
enum Bucket { minute, hour, day, month, year }

extension on Bucket {
  String get api => switch (this) {
        Bucket.minute => 'minute',
        Bucket.hour => 'hour',
        Bucket.day => 'day',
        Bucket.month => 'month',
        Bucket.year => 'year',
      };
}

/// Satu titik data live (dari tabel readings).
class Reading {
  final String device;
  final double? temperature; // bisa null jika sensor lepas
  final double? ph;
  final DateTime createdAt; // UTC

  Reading({
    required this.device,
    required this.temperature,
    required this.ph,
    required this.createdAt,
  });

  factory Reading.fromMap(Map<String, dynamic> m) => Reading(
        device: m['device'] as String,
        temperature: (m['temperature'] as num?)?.toDouble(),
        ph: (m['ph'] as num?)?.toDouble(),
        createdAt: DateTime.parse(m['created_at'] as String).toUtc(),
      );
}

/// Satu titik data grafik (dari get_series).
class SeriesPoint {
  final DateTime t; // UTC, awal bucket
  final double? tempAvg, tempMin, tempMax;
  final double? phAvg, phMin, phMax;

  SeriesPoint.fromMap(Map<String, dynamic> m)
      : t = DateTime.parse(m['t'] as String).toUtc(),
        tempAvg = (m['temp_avg'] as num?)?.toDouble(),
        tempMin = (m['temp_min'] as num?)?.toDouble(),
        tempMax = (m['temp_max'] as num?)?.toDouble(),
        phAvg = (m['ph_avg'] as num?)?.toDouble(),
        phMin = (m['ph_min'] as num?)?.toDouble(),
        phMax = (m['ph_max'] as num?)?.toDouble();
}

/// Ambang batas alert per device.
class Threshold {
  final String device;
  final double phMin, phMax, tempMin, tempMax;

  Threshold.fromMap(Map<String, dynamic> m)
      : device = m['device'] as String,
        phMin = (m['ph_min'] as num).toDouble(),
        phMax = (m['ph_max'] as num).toDouble(),
        tempMin = (m['temp_min'] as num).toDouble(),
        tempMax = (m['temp_max'] as num).toDouble();

  bool isPhAlert(double? ph) => ph != null && (ph < phMin || ph > phMax);
  bool isTempAlert(double? t) => t != null && (t < tempMin || t > tempMax);
}

class SensorService {
  final SupabaseClient _db = Supabase.instance.client;

  static const devices = ['esp-01', 'esp-02', 'esp-03', 'esp-04'];

  // ---------- AUTH ----------
  Future<void> signIn(String email, String password) =>
      _db.auth.signInWithPassword(email: email, password: password);

  Future<void> signOut() => _db.auth.signOut();

  bool get isLoggedIn => _db.auth.currentSession != null;

  // ---------- LIVE (realtime, tampil sebagai angka) ----------
  /// Stream 1 nilai terbaru untuk satu device. Refresh ~10 detik (batch ESP).
  Stream<Reading?> liveReading(String device) {
    return _db
        .from('readings')
        .stream(primaryKey: ['id'])
        .eq('device', device)
        .order('created_at', ascending: false)
        .limit(1)
        .map((rows) => rows.isEmpty ? null : Reading.fromMap(rows.first));
  }

  /// Sparkline: ~60 detik terakhir (untuk mini-grafik di samping angka live).
  Future<List<Reading>> sparkline(String device, {int limit = 60}) async {
    final rows = await _db
        .from('readings')
        .select()
        .eq('device', device)
        .order('created_at', ascending: false)
        .limit(limit);
    return (rows as List)
        .map((m) => Reading.fromMap(m as Map<String, dynamic>))
        .toList()
        .reversed
        .toList();
  }

  // ---------- GRAFIK (semua level) ----------
  /// Ambil deret grafik. [bucket] menentukan resolusi.
  /// Catatan: Bucket.minute hanya tersedia maksimal 30 hari ke belakang.
  Future<List<SeriesPoint>> getSeries({
    required String device,
    required Bucket bucket,
    required DateTime from,
    required DateTime to,
  }) async {
    final rows = await _db.rpc('get_series', params: {
      'p_device': device,
      'p_bucket': bucket.api,
      'p_from': from.toUtc().toIso8601String(),
      'p_to': to.toUtc().toIso8601String(),
    });
    return (rows as List)
        .map((m) => SeriesPoint.fromMap(m as Map<String, dynamic>))
        .toList();
  }

  // ---------- THRESHOLDS ----------
  Future<Threshold?> threshold(String device) async {
    final m = await _db
        .from('thresholds')
        .select()
        .eq('device', device)
        .maybeSingle();
    return m == null ? null : Threshold.fromMap(m);
  }
}

/* ----------------------------------------------------------------
   CONTOH PEMAKAIAN

   final svc = SensorService();
   await svc.signIn('user@email.com', 'password');

   // Live angka:
   StreamBuilder<Reading?>(
     stream: svc.liveReading('esp-01'),
     builder: (ctx, snap) {
       final r = snap.data;
       if (r == null) return Text('—');
       return Text('Suhu ${r.temperature?.toStringAsFixed(1)} °C  '
                   'pH ${r.ph?.toStringAsFixed(2)}');
     },
   );

   // Grafik 7 hari (per hari):
   final now = DateTime.now();
   final pts = await svc.getSeries(
     device: 'esp-01', bucket: Bucket.day,
     from: now.subtract(const Duration(days: 7)), to: now,
   );

   // Grafik 1 jam terakhir (per menit):
   final mins = await svc.getSeries(
     device: 'esp-01', bucket: Bucket.minute,
     from: now.subtract(const Duration(hours: 1)), to: now,
   );

   // Cek alert:
   final thr = await svc.threshold('esp-01');
   final r = await svc.liveReading('esp-01').first;
   if (thr != null && r != null && thr.isPhAlert(r.ph)) {
     // tampilkan notifikasi pH di luar batas
   }
---------------------------------------------------------------- */
