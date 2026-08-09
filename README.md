# ESP32-CAM CCTV

CCTV DIY berbasis ESP32-CAM dengan fitur:

- Streaming video MJPEG via browser
- Deteksi gerakan (motion detection) tanpa sensor tambahan
- Auto-foto saat ada gerakan (opsional, disimpan ke microSD `/cctv` kalau ada slotnya)
- Lampu flash LED (nyala/mati via tombol)
- Ambil foto manual
- Login dengan username/password (HTTP Basic Auth)
- Bisa diakses dari internet (pakai salah satu cara di bagian "Akses dari Internet")
- Mode fallback: kalau WiFi gagal konek, ESP32 menjadi Access Point `ESP32-CAM-Setup` (password `12345678`), buka `http://192.168.4.1`

## 1. Bahan yang Dibutuhkan

| Bahan | Keterangan |
|---|---|
| ESP32-CAM (OV2640) | Yang ada PSRAM (hampir semua board) |
| USB-to-UART (FTDI / CP2102) | Untuk upload program |
| microSD card *(opsional)* | FAT32, minimal 4GB — **hanya kalau board-mu punya slot** |
| Kabel jumper | 5-6 buah |
| Adaptor 5V 2A | Untuk power stabil saat dipasang lama |

## 2. Wiring (Hubungkan Kabel)

| FTDI/CP2102 | ESP32-CAM |
|---|---|
| 5V | 5V (VCC) |
| GND | GND |
| TX | U0R |
| RX | U0T |

Hanya saat upload program: jumper **IO0 ke GND** (lihat langkah 5).

> Tips: untuk pemakaian lama, jangan power dari port USB laptop saja — gunakan adaptor 5V/2A ke pin 5V, karena ESP32-CAM butuh arus cukup besar terutama saat WiFi + kamera nyala.

## 3. Instalasi Arduino IDE

1. Download & install [Arduino IDE](https://www.arduino.cc/en/software) (versi 2.x).
2. Buka **File → Preferences** → di "Additional boards manager URLs" isi:
   ```
   https://dl.espressif.com/dl/package_esp32_index.json
   ```
3. Buka **Tools → Board → Boards Manager**, cari **esp32**, install **"esp32 by Espressif Systems"** (versi terbaru, tunggu sampai selesai).

> **Library:** tidak perlu install library tambahan. Semua yang dipakai sketch (WiFi, WebServer, esp_camera, SD_MMC, FS) sudah termasuk dalam paket board ESP32 di atas.
4. Colok FTDI ke USB laptop, buka **Tools → Port** dan pilih port COM/`/dev/ttyUSB0` yang muncul.

## 4. Edit Kode

Buka `esp32cam_cctv.ino` di Arduino IDE, lalu ganti bagian ini:

```cpp
const char *WIFI_SSID = "NAMA_WIFI_ANDA";      // ganti nama WiFi
const char *WIFI_PASSWORD = "PASSWORD_WIFI_ANDA"; // ganti password WiFi
```

Dan ganti password login web (WAJIB kalau mau diakses internet):

```cpp
const char *AUTH_USER = "admin";
const char *AUTH_PASS = "ganti_password";      // ganti yang kuat!
```

Pengaturan lain yang bisa disesuaikan:

| Baris | Arti | Nilai yang umum dipakai |
|---|---|---|
| `MOTION_THRESHOLD = 18` | Sensitivitas deteksi gerak (makin kecil = makin sensitif) | 10-30 |
| `SAVE_COOLDOWN_MS = 5000` | Jeda minimal antar penyimpanan foto (ms) | 3000-10000 |
| `config.frame_size = FRAMESIZE_SVGA` | Resolusi foto | `FRAMESIZE_VGA` (640x480, lebih cepat) atau `FRAMESIZE_QVGA` (lebih hemat) |
| `config.jpeg_quality = 12` | Kualitas JPEG (0-63, kecil = bagus) | 8-15 |

## 5. Upload Program

1. Setelah memilih board, atur **Tools → Board → ESP32 Arduino → AI Thinker ESP32-CAM**.
2. **Tools → Partition Scheme**: pilih **Huge APP (3MB No OTA/1MB SPIFFS)**.
3. **Tools → Flash Size**: 4MB. **Tools → Upload Speed**: 921600.
4. Hubungkan jumper **IO0 → GND** pada ESP32-CAM, tekan tombol **RESET (EN)** di board.
5. Klik **Upload** di Arduino IDE.
6. Kalau muncul tulisan `Connecting...` berhenti lama, tekan tombol RESET sekali lagi.
7. Setelah selesai: **cabut jumper IO0→GND**, tekan **RESET** sekali lagi. Program langsung jalan.

> Catatan: tombol upload baru bisa bekerja kalau ESP32 dalam mode flashing (IO0 ke GND saat boot).

## 6. Uji Coba (Lokal)

1. Buka **Tools → Serial Monitor** (baud 115200). ESP32 akan mencetak IP-nya, misalnya `http://192.168.1.50`.
2. Buka IP itu di browser HP/laptop yang **satu WiFi** dengan ESP32.
3. Login dengan `admin` dan password yang tadi kamu set.
4. Kamera langsung tampil. Coba tombol Lampu ON/OFF, Ambil Foto, dan jalankan di depan kamera untuk tes deteksi gerak.

### Endpoint web

| Endpoint | Fungsi |
|---|---|
| `/` | Halaman CCTV (stream + tombol) |
| `/stream` | Stream video MJPEG |
| `/capture` | Foto manual (JPEG) |
| `/flash?state=on|off` | Lampu flash LED |
| `/status` | JSON: motion, sd, sdFull, sdFreeMB |
| `/list` | JSON: daftar foto di `/cctv` |
| `/photo?f=IMG_xxx.jpg` | Unduh foto dari SD |

### Skrip verifikasi `verify.sh`

Bash script untuk memastikan deteksi gerak + rekaman SD bekerja:

```
./verify.sh <IP_ESP32> [detik_observasi] [user:pass]
```

Contoh pemakaian (WAJIB pakai user:pass karena auth aktif):

```
./verify.sh 192.168.1.50 15 admin:ganti_password
```

Yang dilakukan: cek status SD/motion → hitung foto di `/cctv` → observasi 15 detik (jalan di depan kamera) → hitung ulang (pastikan ada foto baru) → unduh foto terbaru & validasi file JPEG. Butuh `curl` dan `python3`.

> **Board tanpa slot microSD:** tidak masalah — semua fitur tetap jalan (streaming, deteksi gerak, lampu, foto manual). Yang hilang hanya auto-simpan foto ke SD. Di Serial Monitor akan muncul "SD card tidak terdeteksi".

> **SD penuh:** saat sisa ruang < 10MB, rekaman foto otomatis berhenti dan halaman web menampilkan peringatan merah "SD PENUH - REKAMAN BERHENTI". Kosongkan folder `/cctv` (pindahkan foto ke laptop), lalu rekaman otomatis lanjut — tidak perlu restart ESP32.

## 6b. Motion Detection vs Object Detection

Istilah ini sering tertukar:

| | Motion detection (kode ini) | Object detection (AI) |
|---|---|---|
| Cara kerja | Membandingkan kecerahan antar frame kamera | Menganalisis gambar dengan AI untuk mengenali objek (muka, orang, dll.) |
| Sensor tambahan | **Tidak perlu** — kamera sudah cukup | **Tidak perlu** — kamera sudah cukup |
| Biaya komputasi | Ringan, jalan lancar di ESP32-CAM | Berat; hanya model kecil yang muat (mis. deteksi muka via library ESP-DL / TFLite Micro), dengan FPS rendah |
| Akurasi | Hanya tahu "ada yang bergerak" (angin/bayangan bisa memicu) | Tahu objek apa yang terlihat |

Jadi kedua-duanya tidak butuh sensor eksternal (PIR, dll.) — kamera adalah satu-satunya sensor. Kalau kamu butuh tahu *siapa/apa* yang lewat (bukan hanya "ada gerakan"), itu butuh object detection AI yang ditulis terpisah.

## 7. Akses dari Internet

**Prasyarat:** kamu butuh satu perangkat yang selalu nyala di rumah dan terhubung ke WiFi yang sama dengan ESP32-CAM (laptop bekas, Raspberry Pi, mini PC, atau HP tua). ESP32-CAM tidak bisa menjalankan software tunnel karena sumber dayanya terbatas — jadi tunnel dijalankan di perangkat itu.

Ganti `192.168.1.50` di bawah dengan IP ESP32-CAM kamu.

### Opsi A — Cloudflare Quick Tunnel (paling mudah, gratis, tanpa daftar akun)

Di perangkat selalu-nyala (Linux/Mac/Windows):

```
cloudflared tunnel --url http://192.168.1.50
```

Outputnya berupa link seperti `https://xyz-abcd.trycloudflare.com` — itu URL CCTV kamu yang bisa dibuka dari mana saja. Link berubah tiap kali tunnel dijalankan ulang.

### Opsi B — ngrok

```
socat TCP-LISTEN:8080,reuseaddr,fork TCP:192.168.1.50:80 &
ngrok tcp 8080
```

ngrok menampilkan alamat seperti `0.tcp.ap.ngrok.io:12345` — buka `http://0.tcp.ap.ngrok.io:12345` di browser.

### Opsi C — Tailscale (terbaik untuk akses pribadi terus-menerus)

1. Install Tailscale di perangkat rumah: `curl -fsSL https://tailscale.com/install.sh | sh` lalu `sudo tailscale up`.
2. Jalankan proxy ke ESP32:
   ```
   socat TCP-LISTEN:8080,reuseaddr,fork TCP:192.168.1.50:80 &
   ```
3. Ekspos ke "tailnet" (jaringan pribadimu sendiri):
   ```
   tailscale serve --bg --set-path=/cam http://127.0.0.1:8080
   ```
4. Dari HP/laptop kamu (yang juga login Tailscale), buka `http://<nama-perangkat>.<tailnet>.ts.net/cam`.
5. Mau link publik (dibuka orang tanpa login)? Tambahkan:
   ```
   tailscale funnel --bg --set-path=/cam 8080
   ```
   → dapat HTTPS `https://<nama-perangkat>.<tailnet>.ts.net/cam`. Hati-hati: ini artinya publik.

### Opsi D — Port Forwarding di Router (tanpa perangkat tambahan)

1. Atur IP statis: di halaman admin router, reservation DHCP → IP ESP32-CAM dibuat tetap.
2. Router → **Port Forwarding / Virtual Server** → TCP port `80` → IP ESP32-CAM.
3. Akses dari luar: `http://<IP_PUBLIK>:80` (cek IP publik di [whatismyip.com](https://www.whatismyip.com/)).

Kekurangan opsi ini:
- Banyak ISP Indonesia memakai **CGNAT** (IP publiknya tidak sampai ke router) → port forwarding tidak berfungsi, harus minta IP publik statis ke ISP.
- Kamu harus siapkan DDNS kalau IP publik berubah-ubah.
- Tanpa password yang kuat, kamera bisa "dicegat" orang lain — **jangan pernah** lakukan ini sebelum mengganti `AUTH_PASS`.

## 8. Keamanan

- **WAJIB ganti** `AUTH_PASS` dari `ganti_password` sebelum diakses internet.
- Sebaiknya gunakan Opsi A/B/C (tunnel) karena memberi koneksi HTTPS otomatis; ESP32-CAM tidak bisa HTTPS sendiri.
- Untuk pemakaian di LAN saja, kamu bisa set `AUTH_REQUIRED = false` agar tidak perlu login.
- Kalau menggunakan port forwarding, jangan pernah expose port lain yang tidak perlu.

## 9. Troubleshooting

| Masalah | Solusi |
|---|---|
| Upload selalu "Connecting..." | Pastikan IO0 → GND terpasang, tekan RESET saat Connecting muncul |
| Compile error | Pastikan board "AI Thinker ESP32-CAM" terpilih |
| "Camera init failed" | Cek power 5V (bukan 3.3V), coba USB yang lebih kuat |
| Gambar berwarna hijau / aneh | PSRAM harus "Enabled" di Tools |
| SD tidak terdeteksi | Format FAT32; jika boot-nya restart terus dengan SD terpasang, tambah resistor 10k dari GPIO12 ke 3.3V |
| Rekaman berhenti tiba-tiba | Cek status web: kemungkinan "SD PENUH" — kosongkan folder `/cctv` (rekaman lanjut otomatis) |
| IP berubah-ubah | Reservation DHCP di router |
| Panas banget | Normal, tapi pasang heatsink/kipas kecil agar awet |
| Stream lambat | Turunkan resolusi ke `FRAMESIZE_VGA` / `FRAMESIZE_QVGA` |

## 10. Ide Pengembangan

- Tambah sensor **PIR** di GPIO 13 untuk deteksi gerak yang lebih hemat daya (ESP32 deep sleep saat tidak ada gerakan).
- **Notifikasi Telegram**: kirim foto hasil deteksi ke bot Telegram (butuh internet ke API Telegram — ESP32 bisa langsung pakai HTTPS ke `api.telegram.org`).
- Ganti foto ke **video klip** (rekam N frame JPEG berurutan lalu gabung).
- Pakai ESP32-CAM kedua dan streaming keduanya dari satu halaman web.
