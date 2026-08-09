#!/usr/bin/env bash
set -euo pipefail

IP="${1:?Gunakan: $0 <IP_ESP32> [detik_observasi] [user:pass]}"
WAIT_SEC="${2:-10}"
AUTH="${3:-}"

URL="http://${IP}"
CURL=(curl -s --max-time 10)
if [[ -n "$AUTH" ]]; then
  CURL+=(-u "$AUTH")
fi

if ! command -v python3 >/dev/null 2>&1; then
  echo "ERROR: python3 tidak ditemukan (dibutuhkan untuk parsing JSON)" >&2
  exit 1
fi

req_status() { "${CURL[@]}" "${URL}/status"; }
req_list()   { "${CURL[@]}" "${URL}/list"; }

echo "== Cek status $IP =="
S="$(req_status)"
if [[ -z "$S" || "$S" != *"motion"* ]]; then
  echo "GAGAL: tidak bisa terhubung atau login gagal (cek IP, user:pass, dan WiFi)" >&2
  exit 1
fi
echo "$S" | python3 -c "
import json, sys
j = json.load(sys.stdin)
print('SD terdeteksi :', 'ya' if j.get('sd') else 'TIDAK')
print('Motion saat ini:', 'TERDETEKSI' if j.get('motion') else 'aman')
if j.get('sdFreeMB') is not None:
    print('Sisa ruang SD :', j['sdFreeMB'], 'MB')
    print('SD penuh      :', 'YA - rekaman berhenti' if j.get('sdFull') else 'tidak')
"

echo
echo "== Cek foto tersimpan =="
L1="$(req_list)"
count1=$(echo "$L1" | python3 -c "import json,sys; print(len(json.load(sys.stdin)))")
echo "Jumlah foto di /cctv : $count1"

if [[ "$count1" -eq 0 ]]; then
  echo "Belum ada foto. Jalan di depan kamera sambil menjalankan skrip ini."
  echo "(Foto baru hanya direkam saat ada gerakan, min. jeda 5 detik antar foto)"
  exit 1
fi

L2="$L1"
if [[ "$WAIT_SEC" -gt 0 ]]; then
  echo
  echo "Observasi selama ${WAIT_SEC} detik - jalanlah di depan kamera..."
  sleep "$WAIT_SEC"
  L2="$(req_list)"
  count2=$(echo "$L2" | python3 -c "import json,sys; print(len(json.load(sys.stdin)))")
  echo "Jumlah foto setelah observasi : $count2"
  if [[ "$count2" -gt "$count1" ]]; then
    echo "OK: deteksi gerak + rekaman ke SD bekerja (+$((count2 - count1)) foto baru)"
  else
    echo "INFO: tidak ada foto baru (mungkin tidak ada gerakan, atau masih dalam jeda 5 detik)"
  fi
fi

NEWEST=$(echo "$L2" | python3 -c "
import json, sys
a = json.load(sys.stdin)
print(sorted(a, key=lambda x: x['name'])[-1]['name'] if a else '')
")
if [[ -n "$NEWEST" ]]; then
  echo
  echo "== Verifikasi foto terbaru: $NEWEST =="
  tmp="/tmp/opencode/esp_verify_${NEWEST}"
  "${CURL[@]}" "${URL}/photo?f=${NEWEST}" -o "$tmp"
  magic=$(od -An -tx1 -N3 "$tmp" | tr -d ' \n')
  if [[ "$magic" == "ffd8ff" ]]; then
    sz=$(stat -c %s "$tmp")
    echo "OK: JPEG valid ($sz byte) -> $tmp"
  else
    echo "GAGAL: file bukan JPEG (magic bytes: $magic)" >&2
    exit 1
  fi
fi

echo
echo "== Verifikasi selesai =="
