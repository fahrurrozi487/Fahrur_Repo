/*
  Project: Automatic Pet Feeder
  Contents:
   1) Arduino/ESP32 firmware (WiFi AP/STA web API, servo, DS3231 RTC, keypad, OLED animation, buzzer)
   2) Single-file React dashboard (frontend) that talks to the ESP32 API

  NOTE: Paste the Arduino section into Arduino IDE (select ESP32 board).
  For the React dashboard, drop the component into a React app (e.g. create-react-app) or embed in an HTML page with slight adjustments.

  Coding conventions used: 2-space indent, semicolons, use const where value not reassigned.
*/

/*********************
 * 1) ESP32 Firmware
 *********************/

/* Required libraries:
   - RTClib
   - ESP32Servo
   - Adafruit_SSD1306
   - Adafruit_GFX
   - Keypad
   - WiFi
   - WebServer
   Install via Arduino Library Manager where needed.
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <RTClib.h>
#include <ESP32Servo.h>
#include <Keypad.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ===== CONFIG =====
const char* WIFI_SSID = "FEEDER_AP"; // change if you want station mode
const char* WIFI_PASS = "password123";

// choose mode: true = AP mode (ESP32 hosts network), false = STA mode (connect to existing WiFi)
const bool USE_AP_MODE = true;

// Pins
const int SERVO_PIN = 15;
const int BUZZER_PIN = 27; // PWM capable
const int OLED_RST = -1; // not used for I2C

// Keypad pins (adjust to your wiring)
const byte ROWS = 4;
const byte COLS = 4;
byte rowPins[ROWS] = { 5, 18, 19, 21 };
byte colPins[COLS] = { 22, 23, 13, 12 };

// OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 oled(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RST);

RTC_DS3231 rtc;
Servo servoMotor;
WebServer server(80);

// Keypad setup
char keys[ROWS][COLS] = {
  { '1','2','3','A' },
  { '4','5','6','B' },
  { '7','8','9','C' },
  { '*','0','#','D' }
};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// State
bool isOpen = false;

// Three schedules: morning, noon, night
int feedHourMorning = -1;
int feedMinuteMorning = -1;

int feedHourNoon = -1;
int feedMinuteNoon = -1;

int feedHourNight = -1;
int feedMinuteNight = -1;

// Feeding duration (ms) servo open time
const unsigned long FEED_OPEN_MS = 1500;

// Debounce / last feed seconds to prevent multiple triggers
int lastAutoFeedSecond = -1;

// ----------------- helpers -----------------
void displayHeader() {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0, 0);
}

void drawOLEDMain(const DateTime& now) {
  displayHeader();
  // time
  char buf[32];
  sprintf(buf, "Time: %02d:%02d:%02d", now.hour(), now.minute(), now.second());
  oled.println(buf);

  // schedules
  sprintf(buf, "P:%s S:%s N:%s",
    (feedHourMorning == -1 ? "--:--" : String(feedHourMorning).c_str()),
    (feedHourNoon == -1 ? "--:--" : String(feedHourNoon).c_str()),
    (feedHourNight == -1 ? "--:--" : String(feedHourNight).c_str()));
  // above prints hours only; print full padded below instead
  oled.println();
  oled.print("P:");
  if (feedHourMorning == -1) oled.print("--:--"); else { oled.printf("%02d:%02d", feedHourMorning, feedMinuteMorning); }
  oled.print("  S:");
  if (feedHourNoon == -1) oled.print("--:--"); else { oled.printf("%02d:%02d", feedHourNoon, feedMinuteNoon); }
  oled.print("\nN:");
  if (feedHourNight == -1) oled.print("--:--"); else { oled.printf("%02d:%02d", feedHourNight, feedMinuteNight); }

  oled.setCursor(0, 48);
  oled.print("Servo: ");
  oled.print(isOpen ? "OPEN" : "CLOSED");
  oled.display();
}

// Simple LED-like animation on OLED before feeding: moving bar
void preFeedAnimation() {
  for (int i = 0; i < 3; i++) {
    for (int x = 0; x <= SCREEN_WIDTH - 8; x += 8) {
      oled.clearDisplay();
      oled.setCursor(0, 0);
      oled.println("Feeding soon...");
      oled.fillRect(x, 30, 6, 6, SSD1306_WHITE);
      oled.display();
      delay(80);
    }
  }
}

// buzzer beep sequence
void buzzerAlert() {
  const int toneFreq = 2000;
  const int beepMs = 120;
  for (int i = 0; i < 3; i++) {
    ledcWriteTone(0, toneFreq);
    delay(beepMs);
    ledcWriteTone(0, 0);
    delay(80);
  }
}

void openServo() {
  servoMotor.write(90);
  isOpen = true;
  unsigned long t0 = millis();
  while (millis() - t0 < FEED_OPEN_MS) {
    // keep open
    delay(10);
  }
  servoMotor.write(0);
  isOpen = false;
}

// Web API handlers
void handleStatus() {
  DateTime now = rtc.now();
  String json = "{";
  json += "\"time\": \"" + String(now.timestamp()) + "\",";
  json += "\"hour\": " + String(now.hour()) + ",";
  json += "\"minute\": " + String(now.minute()) + ",";
  json += "\"servo_open\": " + String(isOpen ? "true" : "false") + ",";
  json += "\"schedules\":{";
  json += "\"morning\":\"" + (feedHourMorning == -1 ? "--:--" : String(feedHourMorning) + ":" + (feedMinuteMorning < 10 ? "0" : "") + String(feedMinuteMorning)) + "\",";
  json += "\"noon\":\"" + (feedHourNoon == -1 ? "--:--" : String(feedHourNoon) + ":" + (feedMinuteNoon < 10 ? "0" : "") + String(feedMinuteNoon)) + "\",";
  json += "\"night\":\"" + (feedHourNight == -1 ? "--:--" : String(feedHourNight) + ":" + (feedMinuteNight < 10 ? "0" : "") + String(feedMinuteNight)) + "\"}";
  json += "}";

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
}

// allow CORS preflight
void handleOptions() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  server.send(204, "text/plain", "");
}

// manual feed trigger
void handleFeedNow() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", "{\"result\":\"ok\"}");
  // run feed sequence async-ish
  buzzerAlert();
  preFeedAnimation();
  openServo();
}

// POST /schedule expecting JSON: { "slot": "morning|noon|night", "hour": 7, "minute": 30 }
void handleSetSchedule() {
  if (server.method() == HTTP_OPTIONS) { handleOptions(); return; }

  if (server.hasArg("plain") == false) {
    server.send(400, "application/json", "{\"error\":\"no body\"}");
    return;
  }

  String body = server.arg("plain");

  // very small and simple parser (avoid heavy JSON lib)
  // find slot
  int idxSlot = body.indexOf("\"slot\"");
  int idxHour = body.indexOf("\"hour\"");
  int idxMin = body.indexOf("\"minute\"");

  if (idxSlot == -1 || idxHour == -1 || idxMin == -1) {
    server.send(400, "application/json", "{\"error\":\"bad format\"}");
    return;
  }

  // extract slot string
  int q1 = body.indexOf('"', idxSlot + 6);
  int q2 = body.indexOf('"', q1 + 1);
  String slot = body.substring(q1 + 1, q2);

  // extract numbers naively
  int colHour = body.indexOf(':', idxHour);
  int colMin = body.indexOf(':', idxMin);
  // fallback simple extraction: read numbers with sscanf-like
  int hour = 0;
  int minute = 0;
  sscanf(body.c_str() + idxHour, "\"hour\"%*[^0-9]%d", &hour);
  sscanf(body.c_str() + idxMin, "\"minute\"%*[^0-9]%d", &minute);

  if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
    server.send(400, "application/json", "{\"error\":\"invalid time\"}");
    return;
  }

  if (slot == "morning") {
    feedHourMorning = hour;
    feedMinuteMorning = minute;
  } else if (slot == "noon") {
    feedHourNoon = hour;
    feedMinuteNoon = minute;
  } else if (slot == "night") {
    feedHourNight = hour;
    feedMinuteNight = minute;
  } else {
    server.send(400, "application/json", "{\"error\":\"unknown slot\"}");
    return;
  }

  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", "{\"result\":\"ok\"}");
}

void setupWebServer() {
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/feed", HTTP_POST, handleFeedNow);
  server.on("/schedule", HTTP_POST, handleSetSchedule);
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/plain", "Automatic Pet Feeder API");
  });
  server.on("/", HTTP_OPTIONS, handleOptions);
  server.on("/schedule", HTTP_OPTIONS, handleOptions);
  server.on("/feed", HTTP_OPTIONS, handleOptions);
  server.begin();
}

void setup() {
  Serial.begin(115200);
  Wire.begin();

  // RTC
  if (!rtc.begin()) {
    Serial.println("RTC not found");
    while (1) delay(10);
  }

  // Servo
  servoMotor.attach(SERVO_PIN);
  servoMotor.write(0);

  // buzzer PWM channel setup
  ledcSetup(0, 2000, 8); // channel 0, 2kHz, 8-bit
  ledcAttachPin(BUZZER_PIN, 0);

  // OLED
  if (!oled.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED init failed");
    while (1) delay(10);
  }
  oled.clearDisplay();
  oled.display();

  // WiFi
  if (USE_AP_MODE) {
    WiFi.softAP(WIFI_SSID, WIFI_PASS);
    IPAddress ip = WiFi.softAPIP();
    Serial.print("AP IP address: ");
    Serial.println(ip);
  } else {
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("Connecting to WiFi");
    int tries = 0;
    while (WiFi.status() != WL_CONNECTED && tries < 20) {
      delay(500);
      Serial.print('.');
      tries++;
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("IP: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("WiFi failed, starting AP fallback");
      WiFi.softAP(WIFI_SSID, WIFI_PASS);
    }
  }

  setupWebServer();

  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.println("Automatic Pet Feeder");
  oled.println("Web dashboard ready");
  oled.display();
  delay(1200);
}

// read two-digit from keypad (used if you keep keypad features)
int readTwoDigitNumber(const String& label) {
  int digits[2] = { -1, -1 };
  int index = 0;
  oled.clearDisplay();
  oled.setCursor(0, 0);
  oled.println(label);
  oled.display();

  while (index < 2) {
    char key = keypad.getKey();
    if (key >= '0' && key <= '9') {
      digits[index] = key - '0';
      index++;
      oled.setCursor(0, 16);
      oled.printf("%d%d", digits[0] == -1 ? 0 : digits[0], digits[1] == -1 ? 0 : digits[1]);
      oled.display();
    }
  }
  return digits[0] * 10 + digits[1];
}

void loop() {
  server.handleClient();

  DateTime now = rtc.now();
  drawOLEDMain(now);

  // check auto feeding for three schedules; guard with last second
  int s = now.second();
  if (s != lastAutoFeedSecond) {
    lastAutoFeedSecond = s;

    if (feedHourMorning == now.hour() && feedMinuteMorning == now.minute()) {
      // pre-alert and feed
      buzzerAlert();
      preFeedAnimation();
      openServo();
    }
    if (feedHourNoon == now.hour() && feedMinuteNoon == now.minute()) {
      buzzerAlert();
      preFeedAnimation();
      openServo();
    }
    if (feedHourNight == now.hour() && feedMinuteNight == now.minute()) {
      buzzerAlert();
      preFeedAnimation();
      openServo();
    }
  }

  // keypad handling for setting schedules manually (optional)
  char key = keypad.getKey();
  if (key) {
    if (key == 'B') {
      // choose slot
      oled.clearDisplay();
      oled.println("Set Jadwal: 1 Pagi 2 Siang 3 Malam");
      oled.display();
      int choice = -1;
      while (choice == -1) {
        char k = keypad.getKey();
        if (k == '1' || k == '2' || k == '3') {
          choice = k - '0';
        }
      }
      int hour = readTwoDigitNumber("Jam (00-23)");
      int minute = readTwoDigitNumber("Menit (00-59)");
      if (choice == 1) { feedHourMorning = hour; feedMinuteMorning = minute; }
      if (choice == 2) { feedHourNoon = hour; feedMinuteNoon = minute; }
      if (choice == 3) { feedHourNight = hour; feedMinuteNight = minute; }
      oled.clearDisplay();
      oled.println("Jadwal disimpan");
      oled.display();
      delay(900);
    }
    else if (key == '#') {
      // manual feed
      buzzerAlert();
      preFeedAnimation();
      openServo();
    }
  }

  delay(150);
}


/**************************************
 * 2) React Dashboard (single-file component)
 *
 * Usage: Copy this component into a React app and render <FeederDashboard />.
 * It will poll the ESP32 at the configured host and allow setting schedules.
 **************************************/

// Paste below into a .jsx / .js file in your React app
/**/
const ReactDashboardSource = 
import React, { useEffect, useState } from 'react';

// Adjust this to the IP of your ESP32. If using AP mode, connect to FEEDER_AP then use 192.168.4.1
const ESP_HOST = 'http://192.168.4.1';

export default function FeederDashboard() {
  const [status, setStatus] = useState(null);
  const [loading, setLoading] = useState(false);

  useEffect(() => {
    const id = setInterval(fetchStatus, 3000);
    fetchStatus();
    return () => clearInterval(id);
  }, []);

  async function fetchStatus() {
    try {
      const res = await fetch(`${ESP_HOST}/status`);
      const json = await res.json();
      setStatus(json);
    } catch (e) {
      // network error
      setStatus(null);
    }
  }

  async function triggerFeed() {
    setLoading(true);
    try {
      await fetch(`${ESP_HOST}/feed`, { method: 'POST' });
      // brief refresh
      setTimeout(fetchStatus, 1000);
    } catch (e) {
      console.error(e);
    }
    setLoading(false);
  }

  async function setSchedule(slot) {
    const time = prompt(`Masukkan waktu untuk ${slot} (HH:MM)`, '07:30');
    if (!time) return;
    const parts = time.split(':');
    if (parts.length !== 2) return alert('Format salah');
    const hour = parseInt(parts[0], 10);
    const minute = parseInt(parts[1], 10);
    if (isNaN(hour) || isNaN(minute)) return alert('Format salah');

    setLoading(true);
    try {
      await fetch(`${ESP_HOST}/schedule`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ slot, hour, minute })
      });
      fetchStatus();
    } catch (e) {
      console.error(e);
      alert('Gagal set schedule');
    }
    setLoading(false);
  }

  return (
    <div style={{ fontFamily: 'sans-serif', padding: 24 }}>
      <h2>Automatic Pet Feeder Dashboard</h2>
      <div style={{ display: 'flex', gap: 24 }}>
        <div style={{ flex: 1 }}>
          <div style={{ padding: 12, border: '1px solid #ddd', borderRadius: 8 }}>
            <h3>Current</h3>
            {status ? (
              <div>
                <p>Time: {status.hour}:{('0'+status.minute).slice(-2)}</p>
                <p>Servo: {status.servo_open ? 'OPEN' : 'CLOSED'}</p>
                <p>Schedules:</p>
                <ul>
                  <li>Morning: {status.schedules ? status.schedules.morning : '--:--'}</li>
                  <li>Noon: {status.schedules ? status.schedules.noon : '--:--'}</li>
                  <li>Night: {status.schedules ? status.schedules.night : '--:--'}</li>
                </ul>
              </div>
            ) : (
              <div>Not connected to feeder</div>
            )}

            <div style={{ marginTop: 12 }}>
              <button onClick={triggerFeed} disabled={loading} style={{ padding: '8px 12px' }}>{loading ? 'Sending...' : 'Feed Now'}</button>
            </div>
          </div>
        </div>

        <div style={{ width: 260 }}>
          <div style={{ padding: 12, border: '1px solid #ddd', borderRadius: 8 }}>
            <h3>Set Schedule</h3>
            <button onClick={() => setSchedule('morning')} style={{ display: 'block', width: '100%', marginBottom: 8 }}>Set Morning</button>
            <button onClick={() => setSchedule('noon')} style={{ display: 'block', width: '100%', marginBottom: 8 }}>Set Noon</button>
            <button onClick={() => setSchedule('night')} style={{ display: 'block', width: '100%' }}>Set Night</button>
          </div>

          <div style={{ padding: 12, marginTop: 12, border: '1px solid #ddd', borderRadius: 8 }}>
            <h3>Notes</h3>
            <p>Connect your PC/mobile to the ESP32 WiFi ("FEEDER_AP") or put correct IP for STA mode.</p>
            <p>ESP32 default IP in AP mode: <strong>192.168.4.1</strong></p>
          </div>
        </div>
      </div>

      <style>{`
        /* small animation for feed button */
        button:active { transform: translateY(1px); }
      `}</style>
    </div>
  );
}
`;

// We include the React source as plain text here so the user can copy it easily.

// End of document
*/