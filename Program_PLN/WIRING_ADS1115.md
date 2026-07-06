# Wiring pH via ADS1115 (16-bit ADC) — ESP32-S3 N16R8 @ 3.3V

ADS1115 = ADC eksternal 16-bit via I2C. Lebih presisi & stabil dibanding ADC
internal ESP32-S3. Semua di 3.3V → **tidak perlu voltage divider**.

---

## Skema Wiring

```
  ESP32-S3 N16R8            ADS1115              PH-4502C board
 ┌──────────────┐        ┌──────────┐          ┌──────────────┐
 │ 3V3          │───┬───▶│ VDD      │          │              │
 │              │   └───────────────────────-─▶│ V+           │
 │ GND          │───┬───▶│ GND      │          │              │
 │              │   └───────────────────────-─▶│ GND (G)      │
 │ GPIO 9 (SCL) │──────▶ │ SCL      │          │              │
 │ GPIO 8 (SDA) │──────▶ │ SDA      │          │              │
 │              │        │ ADDR ────┼── GND     │              │
 │              │        │ A0 ◀─────┼───────────│ Po (sinyal)  │
 │              │        │ A1 A2 A3 │ (kosong)  │              │
 └──────────────┘        └──────────┘          └──────────────┘

 DS18B20 tetap di GPIO 4 (tidak berubah).
```

### Tabel sambungan

| ESP32-S3 | ADS1115 | Keterangan |
|----------|---------|------------|
| 3V3 | VDD | power ADS1115 |
| GND | GND | ground |
| GPIO 9 | SCL | I2C clock |
| GPIO 8 | SDA | I2C data |
| — | ADDR → GND | alamat I2C = **0x48** |

| ADS1115 | PH-4502C | Keterangan |
|---------|----------|------------|
| A0 | Po | sinyal analog pH |

| ESP32-S3 | PH-4502C | Keterangan |
|----------|----------|------------|
| 3V3 | V+ | power board pH |
| GND | G (GND) | ground bersama |

> **GND wajib common**: ESP32, ADS1115, dan board pH harus berbagi GND yang sama.
> Tanpa ini, pembacaan ADS1115 akan ngawur.

---

## Catatan Penting

1. **Tidak perlu voltage divider** — selama board pH diberi 3.3V, Po ≤ 3.3V (aman).
   - ⚠️ Kalau nanti board pH diberi **5V**, Po bisa >3.3V → JANGAN colok langsung ke
     ADS1115 yang VDD-nya 3.3V. Saat itu baru butuh divider, atau power ADS1115 di 5V.

2. **Pull-up I2C**: modul ADS1115 biasanya sudah ada pull-up 10k di SDA/SCL. Tidak
   perlu tambah resistor.

3. **Alamat I2C** (pin ADDR):
   | ADDR ke | Alamat |
   |---------|--------|
   | GND | 0x48 (default) |
   | VDD | 0x49 |
   | SDA | 0x4A |
   | SCL | 0x4B |

4. **Pin I2C ESP32-S3**: di sketch dipakai SDA=GPIO8, SCL=GPIO9. Bisa diganti pin
   lain lewat `Wire.begin(SDA, SCL)`.

5. **Gain (PGA)**: pakai `GAIN_ONE` → rentang ±4.096V, resolusi 0.125 mV/bit.
   Lebih dari cukup untuk sinyal pH 0–3.3V.

6. **Board pH harus bertenaga** — LED PWR menyala. ADS1115 tidak memberi daya ke
   board pH; ia hanya membaca output Po.

---

## Library yang dibutuhkan
- **Adafruit ADS1X15** (Library Manager)
- OneWire + DallasTemperature (untuk DS18B20, sudah dari sebelumnya)

## Kalibrasi (sama prinsip seperti sebelumnya)
1. Flash sketch `baca_ph_ads1115.ino`
2. Short BNC → catat Volt (ini pH 7.00) = V7
3. Celup buffer pH 4.0 → catat Volt = V4
4. Hitung: `m = (7-4)/(V7-V4)` ; `b = 7 - m*V7`
5. Masukkan ke `PH_SLOPE` & `PH_OFFSET`.
