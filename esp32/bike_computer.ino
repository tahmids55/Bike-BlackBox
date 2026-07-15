// ============================================================
//  Bike Computer v2 – GPS + Hall + MPU6050 + OLED + Flask Server
//  Enhanced: sends all sensor data, polls display templates,
//  supports brightness control and custom messages.
//  For ESP32 (Arduino IDE)
// ============================================================

#include <WiFi.h>
#include <HTTPClient.h>
#include <TinyGPS++.h>
#include <Wire.h>
#include <Adafruit_SH110X.h>
#include <ArduinoJson.h>   // Install via Library Manager

// ==================== WiFi & Server ====================
const char* ssid     = "LSH_515";
const char* password = "87827446";

// Flask server IP (change to your PC's local IP)
const char* serverBase = "http://192.168.0.50:5000";
String updateUrl    = String(serverBase) + "/update";
String accidentUrl  = String(serverBase) + "/accident";
String displayUrl   = String(serverBase) + "/display";

// ==================== Hall Sensor (A3144) ====================
const byte hallPin = 4;
volatile unsigned long pulseCount = 0;
volatile unsigned long lastPulseTime = 0;
volatile unsigned long pulseInterval = 0;
unsigned long lastHallCalc = 0;
const float radiusMeters = 0.015;     // 1.5 cm – magnet distance from fan centre
float rpm = 0.0;
float hallSpeedKmh = 0.0;

void IRAM_ATTR hallISR() {
  unsigned long now = millis();
  unsigned long dt = now - lastPulseTime;
  if (dt > 15) { // 15ms debounce (max ~4000 RPM)
    pulseInterval = dt;
    lastPulseTime = now;
    pulseCount++;
  }
}

// ==================== GPS (NEO-6M) ====================
HardwareSerial gpsSerial(2);          // RX=16, TX=17
TinyGPSPlus gps;
unsigned long lastGpsSend = 0;
const unsigned long gpsInterval = 1000;

// ==================== MPU6050 ====================
const int MPU_ADDR = 0x68;
float ax_g, ay_g, az_g, totalAcc;
float tiltAngle = 0.0;
bool mpuOK = false;

// Accident thresholds
const float TILT_THRESHOLD   = 80.0;   // 80+ degrees for accident
const float WARNING_TILT     = 45.0;   // warning level
const unsigned long TILT_PERSIST_TIME = 4000; // must stay fallen for 4 seconds
unsigned long tiltStartTime = 0;

unsigned long lastAccidentTime = 0;
const unsigned long accidentCooldown = 10000;
bool accidentActive = false;
int accidentMessageCount = 0; // limit messages per accident
unsigned long accidentDisplayUntil = 0;

// ==================== OLED (SH1106) ====================
Adafruit_SH1106G display(128, 64, &Wire, -1);
unsigned long lastOLEDUpdate = 0;

// ==================== Display Control ====================
String currentTemplate = "default";
int displayBrightness = 255;
String customMessage = "";
unsigned long lastDisplayPoll = 0;
const unsigned long displayPollInterval = 2000;  // poll every 2 sec

// ==================== Status ====================
String bikeStatus = "safe";   // "safe", "warning", "danger", "accident"

// ==================== Setup ====================
void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  Wire.setClock(100000);

  // --- OLED ---
  if (!display.begin(0x3C, true)) {
    Serial.println("OLED not found");
    while (1) delay(100);
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(10, 25);
  display.println("Booting...");
  display.display();

  // --- WiFi ---
  WiFi.begin(ssid, password);
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 15000) {
    delay(500);
  }
  Serial.println(WiFi.status() == WL_CONNECTED ? "WiFi OK" : "WiFi failed");

  // --- Hall Sensor ---
  pinMode(hallPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(hallPin), hallISR, FALLING);

  // --- GPS ---
  gpsSerial.begin(9600, SERIAL_8N1, 16, 17);

  // --- MPU6050 ---
  initMPU();

  // --- Start screen ---
  display.clearDisplay();
  display.setCursor(10, 15);
  display.println("BIKE COMPUTER v2");
  display.setCursor(10, 30);
  display.println("ALL SYSTEMS GO");
  display.setCursor(10, 45);
  display.print("IP: ");
  display.println(WiFi.localIP());
  display.display();
  delay(2000);
}

// ==================== Main Loop ====================
void loop() {
  // 1. Feed GPS
  while (gpsSerial.available()) {
    gps.encode(gpsSerial.read());
  }

  // 2. Read MPU6050 and detect accident
  readMPU6050();
  if (accidentActive && millis() > accidentDisplayUntil) {
    accidentActive = false;
    bikeStatus = "safe";
  }

  // 3. Hall sensor – RPM and speed (interval-based)
  if (millis() - lastHallCalc >= 200) { // Update calculations at 5Hz
    lastHallCalc = millis();
    noInterrupts();
    unsigned long currentInterval = pulseInterval;
    unsigned long timeSinceLast = millis() - lastPulseTime;
    interrupts();

    if (timeSinceLast > 2000) {
      // Stopped if no pulse for 2 seconds
      rpm = 0.0;
      hallSpeedKmh = 0.0;
    } else if (currentInterval > 0) {
      rpm = 60000.0 / (float)currentInterval;
      float circumference = 2.0 * PI * radiusMeters;
      hallSpeedKmh = (circumference * rpm * 60.0) / 1000.0;
    }
  }

  // 4. Send ALL telemetry to server every 3 seconds
  if (millis() - lastGpsSend >= gpsInterval) {
    lastGpsSend = millis();
    if (WiFi.status() == WL_CONNECTED) {
      sendTelemetry();
    }
  }

  // 5. Poll display config every 2 seconds
  if (millis() - lastDisplayPoll >= displayPollInterval) {
    lastDisplayPoll = millis();
    if (WiFi.status() == WL_CONNECTED) {
      pollDisplayConfig();
    }
  }

  // 6. Update OLED ~4 times/sec
  if (millis() - lastOLEDUpdate >= 250) {
    lastOLEDUpdate = millis();
    renderDisplay();
  }
}

// ==================== MPU6050 Functions ====================
void initMPU() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B); Wire.write(0x80);
  Wire.endTransmission();
  delay(100);
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B); Wire.write(0x00);
  Wire.endTransmission();
  delay(100);
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1C); Wire.write(0x10);   // ±8g range (was ±2g) – less sensitive to vibration
  Wire.endTransmission();
  delay(10);
  Serial.println("MPU6050 ready.");
}

void readMPU6050() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  if (Wire.endTransmission(false) != 0) { mpuOK = false; return; }
  Wire.requestFrom(MPU_ADDR, 6);
  if (Wire.available() != 6) { mpuOK = false; return; }

  int16_t ax_raw = Wire.read() << 8 | Wire.read();
  int16_t ay_raw = Wire.read() << 8 | Wire.read();
  int16_t az_raw = Wire.read() << 8 | Wire.read();

  ax_g = ax_raw / 4096.0;    // ±8g range → 4096 LSB/g
  ay_g = ay_raw / 4096.0;
  az_g = az_raw / 4096.0;
  totalAcc = sqrt(ax_g*ax_g + ay_g*ay_g + az_g*az_g);

  float clamped_ay = constrain(ay_g, -1.0, 1.0);
  tiltAngle = acos(clamped_ay) * 180.0 / PI;
  mpuOK = true;

  // ── Determine status ──
  bool fallen  = (tiltAngle > TILT_THRESHOLD);
  bool tilted  = (tiltAngle > WARNING_TILT);

  if (fallen) { 
    if (tiltStartTime == 0) tiltStartTime = millis(); 
  } else { 
    tiltStartTime = 0; 
  }

  if (fallen && (millis() - tiltStartTime >= TILT_PERSIST_TIME)) {
    if (millis() - lastAccidentTime > accidentCooldown) {
      lastAccidentTime = millis();
      accidentActive = true;
      accidentDisplayUntil = millis() + 5000;
      tiltStartTime = 0;
      bikeStatus = "accident";
      Serial.println("*** ACCIDENT DETECTED ***");
      
      // Limit to 5 messages per continuous accident event
      if (accidentMessageCount < 5) {
        sendAccidentAlert();
        accidentMessageCount++;
      }
    }
  } else if (tilted) {
    if (!accidentActive) bikeStatus = "warning";
  } else {
    if (!accidentActive) bikeStatus = "safe";
    // Reset the counter once the bike is picked back up
    accidentMessageCount = 0;
  }
}

// ==================== Server Communication ====================
void sendTelemetry() {
  HTTPClient http;
  http.begin(updateUrl);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(3000);

  // Build JSON with ALL sensor data
  String json = "{";
  if (gps.location.isValid()) {
    json += "\"lat\":" + String(gps.location.lat(), 6) + ",";
    json += "\"lng\":" + String(gps.location.lng(), 6) + ",";
    json += "\"speed\":" + String(gps.speed.kmph(), 1) + ",";
  } else {
    json += "\"lat\":0,\"lng\":0,\"speed\":0,";
  }
  json += "\"satellites\":" + String(gps.satellites.value()) + ",";
  json += "\"hall_speed\":" + String(hallSpeedKmh, 1) + ",";
  json += "\"rpm\":" + String(rpm, 0) + ",";
  json += "\"tilt_angle\":" + String(tiltAngle, 1) + ",";
  json += "\"total_accel\":" + String(totalAcc, 2) + ",";
  json += "\"status\":\"" + bikeStatus + "\"";
  json += "}";

  int code = http.POST(json);
  if (code > 0) {
    // Parse display config from response
    String resp = http.getString();
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, resp) == DeserializationError::Ok) {
      if (doc.containsKey("display")) {
        JsonObject disp = doc["display"];
        if (disp.containsKey("template"))
          currentTemplate = disp["template"].as<String>();
        if (disp.containsKey("brightness"))
          displayBrightness = disp["brightness"].as<int>();
        if (disp.containsKey("custom_message"))
          customMessage = disp["custom_message"].as<String>();
      }
    }
  }
  http.end();
}

void sendAccidentAlert() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(accidentUrl);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(3000);

  String json = "{";
  if (gps.location.isValid()) {
    json += "\"lat\":" + String(gps.location.lat(), 6) + ",";
    json += "\"lng\":" + String(gps.location.lng(), 6);
  } else {
    json += "\"lat\":0.0,";
    json += "\"lng\":0.0";
  }
  json += "}";
  
  http.POST(json);
  http.end();
  Serial.println("Accident alert sent");
}

void pollDisplayConfig() {
  HTTPClient http;
  http.begin(displayUrl);
  http.setTimeout(2000);

  int code = http.GET();
  if (code == 200) {
    String resp = http.getString();
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, resp) == DeserializationError::Ok) {
      if (doc.containsKey("template"))
        currentTemplate = doc["template"].as<String>();
      if (doc.containsKey("brightness"))
        displayBrightness = doc["brightness"].as<int>();
      if (doc.containsKey("custom_message"))
        customMessage = doc["custom_message"].as<String>();
    }
  }
  http.end();
}

// ==================== Display Rendering ====================
void renderDisplay() {
  // Handle "off" template
  if (currentTemplate == "off") {
    display.clearDisplay();
    display.display();
    return;
  }

  // Set brightness (SH1106 contrast register)
  display.setContrast(displayBrightness);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  // ── Template: default ──
  if (currentTemplate == "default") {
    display.setCursor(0, 0);
    display.printf("RPM:%.0f  Hall:%.1fkm/h", rpm, hallSpeedKmh);
    display.setCursor(0, 10);
    if (gps.location.isValid()) {
      display.printf("GPS:%.1fkm/h  Sats:%d", gps.speed.kmph(), gps.satellites.value());
    } else {
      display.print("GPS: No fix");
    }
    display.setCursor(0, 20);
    if (!mpuOK) { display.print("MPU: ERROR"); }
    else { display.printf("Tilt:%+4.0f deg", tiltAngle); }
    display.setCursor(0, 30);
    if (!mpuOK) { display.print("Acc: --"); }
    else { display.printf("Acc: %.2f g", totalAcc); }
    display.drawLine(0, 40, 128, 40, SH110X_WHITE);
    display.setCursor(0, 44);
    display.print(WiFi.status() == WL_CONNECTED ? "WiFi:OK" : "WiFi:DOWN");
    // Status badge
    display.setCursor(70, 44);
    display.print("[");
    display.print(bikeStatus.c_str());
    display.print("]");
  }

  // ── Template: speed_only ──
  else if (currentTemplate == "speed_only") {
    display.setTextSize(3);
    display.setCursor(10, 5);
    display.printf("%.0f", hallSpeedKmh);
    display.setTextSize(1);
    display.setCursor(90, 15);
    display.print("km/h");
    display.setCursor(10, 40);
    display.print("Hall Speed");
    display.setCursor(10, 52);
    display.printf("GPS: %.1f km/h", gps.location.isValid() ? gps.speed.kmph() : 0.0);
  }

  // ── Template: gps_only ──
  else if (currentTemplate == "gps_only") {
    display.setCursor(0, 0);
    if (gps.location.isValid()) {
      display.printf("Lat: %.6f", gps.location.lat());
      display.setCursor(0, 12);
      display.printf("Lng: %.6f", gps.location.lng());
      display.setCursor(0, 28);
      display.printf("Speed: %.1f km/h", gps.speed.kmph());
      display.setCursor(0, 40);
      display.printf("Sats: %d", gps.satellites.value());
    } else {
      display.print("GPS: Searching...");
      display.setCursor(0, 14);
      display.printf("Sats: %d", gps.satellites.value());
    }
  }

  // ── Template: minimal ──
  else if (currentTemplate == "minimal") {
    display.setTextSize(4);
    display.setCursor(15, 8);
    display.printf("%.0f", hallSpeedKmh);
    display.setTextSize(1);
    display.setCursor(48, 50);
    display.print("km/h");
  }

  // ── Template: custom ──
  else if (currentTemplate == "custom") {
    display.setTextSize(2);
    int x = max(0, (128 - (int)customMessage.length() * 12) / 2);
    display.setCursor(x, 22);
    display.print(customMessage.c_str());
  }

  // Accident overlay (on any template)
  if (accidentActive) {
    display.fillRect(0, 48, 128, 16, SH110X_WHITE);
    display.setTextColor(SH110X_BLACK);
    display.setTextSize(1);
    display.setCursor(25, 52);
    display.print("!! ACCIDENT !!");
    display.setTextColor(SH110X_WHITE);
  }

  display.display();
}
