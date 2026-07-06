/**
 * Cloudflare Worker — jembatan ESP32 -> Supabase + arsip CSV harian ke R2
 *
 * Secret (wrangler secret put ...):
 *   SUPABASE_URL          : https://xxxx.supabase.co
 *   SUPABASE_SERVICE_KEY  : service_role key (RAHASIA, bypass RLS)
 *   DEVICE_API_KEY        : kunci validasi ESP
 *
 * Binding (wrangler.toml):
 *   ARCHIVE : R2 bucket (sensor-archive) untuk file CSV harian
 *
 * Cron (wrangler.toml): tiap 10 menit -> flush data baru ke CSV + purge >2 hari.
 *
 * Endpoint:
 *   POST /api/sensor   -> terima data ESP (batch ATAU tunggal), insert ke Supabase
 *   GET  /api/sensor   -> data terbaru
 *   GET  /api/history  -> riwayat (untuk Flutter)
 *   GET  /api/flush    -> trigger manual flush+purge (butuh X-API-Key) untuk test
 */

const RETENTION_DAYS = 2;          // simpan di Supabase 2 hari (sisanya hanya di CSV)
const WIB_OFFSET_MS  = 7 * 3600e3; // UTC+7
const MARKER_KEY     = "_state/last_flush.txt";

const CORS = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
  "Access-Control-Allow-Headers": "Content-Type, X-API-Key",
};

export default {
  async fetch(request, env) {
    const url = new URL(request.url);

    if (request.method === "OPTIONS") return new Response(null, { headers: CORS });

    if (request.method === "POST" && url.pathname === "/api/sensor") return postSensor(request, env);
    if (request.method === "GET"  && url.pathname === "/api/sensor")  return getLatest(env);
    if (request.method === "GET"  && url.pathname === "/api/history") return getHistory(url, env);

    if (request.method === "GET" && url.pathname === "/api/flush") {
      if (request.headers.get("X-API-Key") !== env.DEVICE_API_KEY) return json({ error: "Unauthorized" }, 401);
      const f = await flushToCSV(env);
      const p = await purgeOld(env);
      return json({ flush: f, purge: p });
    }

    return new Response("Not Found", { status: 404, headers: CORS });
  },

  // Dijalankan otomatis oleh Cron Trigger (lihat wrangler.toml)
  async scheduled(event, env, ctx) {
    ctx.waitUntil((async () => {
      const f = await flushToCSV(env);
      const p = await purgeOld(env);
      console.log("cron:", JSON.stringify({ f, p }));
    })());
  },
};

// ─── POST /api/sensor (batch atau tunggal) ───────────────────────────
async function postSensor(request, env) {
  if (request.headers.get("X-API-Key") !== env.DEVICE_API_KEY) {
    return json({ error: "Unauthorized" }, 401);
  }

  let body;
  try { body = await request.json(); }
  catch { return json({ error: "Invalid JSON" }, 400); }

  let rows;
  if (Array.isArray(body.readings)) {
    // Batch: {device, readings:[{t, temperature, ph, water_quality}, ...]}
    const device = body.device || "unknown";
    rows = body.readings.map(r => ({
      temperature: r.temperature === undefined ? null : r.temperature,
      ph: r.ph,
      water_quality: r.water_quality === undefined ? null : r.water_quality,
      device,
      created_at: r.t,   // timestamp ISO dari ESP (NTP)
    }));
  } else {
    // Tunggal (kompatibilitas lama)
    if (body.ph === undefined) return json({ error: "Missing ph" }, 400);
    rows = [{
      temperature: body.temperature === undefined ? null : body.temperature,
      ph: body.ph,
      water_quality: body.water_quality === undefined ? null : body.water_quality,
      device: body.device || "unknown",
    }];
  }

  if (!rows.length) return json({ error: "No readings" }, 400);

  const res = await fetch(`${env.SUPABASE_URL}/rest/v1/readings`, {
    method: "POST",
    headers: {
      "Content-Type": "application/json",
      apikey: env.SUPABASE_SERVICE_KEY,
      Authorization: `Bearer ${env.SUPABASE_SERVICE_KEY}`,
      Prefer: "return=minimal",
    },
    body: JSON.stringify(rows),  // array -> bulk insert
  });

  if (!res.ok) {
    const detail = await res.text();
    return json({ error: "Supabase insert failed", detail }, 502);
  }
  return json({ success: true, inserted: rows.length }, 201);
}

// ─── GET /api/sensor (terbaru) ───────────────────────────────────────
async function getLatest(env) {
  const res = await fetch(
    `${env.SUPABASE_URL}/rest/v1/readings?select=*&order=created_at.desc&limit=1`,
    { headers: readHeaders(env) }
  );
  const data = await res.json();
  if (!data.length) return json({ error: "No data yet" }, 404);
  return json(data[0]);
}

// ─── GET /api/history?device=eq.esp-01&limit=50&from=&to= ────────────
async function getHistory(url, env) {
  const limit = url.searchParams.get("limit") || "50";
  const device = url.searchParams.get("device");
  const from = url.searchParams.get("from");
  const to   = url.searchParams.get("to");

  let q = `select=*&order=created_at.desc&limit=${encodeURIComponent(limit)}`;
  if (device) q += `&device=eq.${encodeURIComponent(device)}`;
  if (from)   q += `&created_at=gte.${encodeURIComponent(from)}`;
  if (to)     q += `&created_at=lte.${encodeURIComponent(to)}`;

  const res = await fetch(`${env.SUPABASE_URL}/rest/v1/readings?${q}`, { headers: readHeaders(env) });
  return json(await res.json());
}

// ─── CRON: flush baris baru -> CSV harian di R2 ──────────────────────
async function flushToCSV(env) {
  if (!env.ARCHIVE) return { error: "R2 binding ARCHIVE belum diset" };

  // marker = created_at terakhir yang sudah diarsipkan
  const markerObj = await env.ARCHIVE.get(MARKER_KEY);
  const since = markerObj ? (await markerObj.text()).trim() : "1970-01-01T00:00:00Z";

  // ambil baris baru (urut naik), batasi agar ringan
  const q = `select=created_at,device,temperature,ph,water_quality` +
            `&created_at=gt.${encodeURIComponent(since)}` +
            `&order=created_at.asc&limit=20000`;
  const res = await fetch(`${env.SUPABASE_URL}/rest/v1/readings?${q}`, { headers: readHeaders(env) });
  if (!res.ok) return { error: "query gagal", detail: await res.text() };
  const rows = await res.json();
  if (!rows.length) return { flushed: 0 };

  // kelompokkan per tanggal WIB -> baris CSV
  const groups = {};
  for (const r of rows) {
    const date = wibDate(r.created_at);
    const line = `${r.created_at},${r.device},${r.temperature ?? ""},${r.ph ?? ""},${r.water_quality ?? ""}\n`;
    (groups[date] ||= []).push(line);
  }

  // tambahkan ke file harian (read-modify-write)
  for (const [date, lines] of Object.entries(groups)) {
    const key = `data_${date}.csv`;
    const existing = await env.ARCHIVE.get(key);
    let content = existing ? await existing.text() : "timestamp,device,temperature,ph,water_quality\n";
    content += lines.join("");
    await env.ARCHIVE.put(key, content);
  }

  const last = rows[rows.length - 1].created_at;
  await env.ARCHIVE.put(MARKER_KEY, last);
  return { flushed: rows.length, until: last, files: Object.keys(groups) };
}

// ─── CRON: hapus baris >RETENTION_DAYS hari (yang SUDAH diarsipkan) ───
async function purgeOld(env) {
  const markerObj = env.ARCHIVE ? await env.ARCHIVE.get(MARKER_KEY) : null;
  const marker = markerObj ? (await markerObj.text()).trim() : null;
  if (!marker) return { purged: "skip (belum ada arsip)" };

  const twoDaysAgo = new Date(Date.now() - RETENTION_DAYS * 86400e3).toISOString();
  // cutoff = lebih lama antara (2 hari lalu) & marker -> jangan hapus yang belum diarsip
  const cutoff = marker < twoDaysAgo ? marker : twoDaysAgo;

  const res = await fetch(
    `${env.SUPABASE_URL}/rest/v1/readings?created_at=lt.${encodeURIComponent(cutoff)}`,
    { method: "DELETE", headers: readHeaders(env) }
  );
  return { purged_before: cutoff, ok: res.ok };
}

// ─── Helpers ─────────────────────────────────────────────────────────
function wibDate(iso) {
  const wib = new Date(new Date(iso).getTime() + WIB_OFFSET_MS);
  return wib.toISOString().slice(0, 10);  // YYYY-MM-DD (tanggal WIB)
}

function readHeaders(env) {
  return {
    apikey: env.SUPABASE_SERVICE_KEY,
    Authorization: `Bearer ${env.SUPABASE_SERVICE_KEY}`,
  };
}

function json(data, status = 200) {
  return new Response(JSON.stringify(data), {
    status,
    headers: { "Content-Type": "application/json", ...CORS },
  });
}
