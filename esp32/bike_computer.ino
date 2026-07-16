#include <WiFi.h>
#include <HTTPClient.h>
#include <TinyGPS++.h>
#include <Wire.h>
#include <Adafruit_SH110X.h>
#include <ArduinoJson.h>

//WiFi
const char* ssid     = "TIGER";
const char* password = "sul2207011";

//Server
const char* serverBase  = "http://192.168.0.50:5000";
const char* updateUrl   = "http://192.168.0.50:5000/update";
const char* accidentUrl = "http://192.168.0.50:5000/accident";
const char* displayUrl  = "http://192.168.0.50:5000/display";

//Hall Sensor
const int   hallPin      = 4;
const float radiusMeters = 0.015f;

volatile unsigned long pulseCount    = 0;
volatile unsigned long lastPulseTime = 0;
volatile unsigned long pulseInterval = 0;

unsigned long lastHallCalc = 0;
float rpm          = 0.0f;
float hallSpeedKmh = 0.0f;

void IRAM_ATTR hallISR() {
  unsigned long now = millis();
  unsigned long dt  = now - lastPulseTime;
  if (dt > 15) {
    pulseInterval = dt;
    lastPulseTime = now;
    pulseCount++;
  }
}

//GPS
HardwareSerial gpsSerial(2);   // RX=16, TX=17
TinyGPSPlus    gps;

//MPU6050
const int MPU_ADDR = 0x68;
float ax_g = 0, ay_g = 0, az_g = 0, totalAcc = 0, tiltAngle = 0;
bool  mpuOK = false;

//Accident Detection
const float        TILT_THRESHOLD     = 55.0f;
const float        WARNING_TILT       = 40.0f;
const unsigned long TILT_PERSIST_TIME = 4000UL;
const unsigned long ACCIDENT_COOLDOWN = 10000UL;
const unsigned long ACCIDENT_SHOW_MS  = 5000UL;
const int          MAX_ACCIDENT_MSG   = 5;

unsigned long tiltStartTime      = 0;
unsigned long lastAccidentTime   = 0;
unsigned long accidentDisplayUntil = 0;
bool          accidentActive     = false;
int           accidentMsgCount   = 0;
String        bikeStatus         = "safe";

//OLED (SH1106, 128×64)
Adafruit_SH1106G display(128, 64, &Wire, -1);
unsigned long lastOLEDUpdate = 0;
bool          oledOK         = false;

//Display Config (from server)
String        currentTemplate  = "default";
int           displayBrightness = 255;
String        customMessage    = "";

//Timing
unsigned long lastTelemetrySend = 0;
unsigned long lastDisplayPoll   = 0;
unsigned long lastWifiCheck     = 0;

const unsigned long TELEMETRY_INTERVAL   = 3000UL;  
const unsigned long DISPLAY_POLL_INTERVAL= 2000UL;
const unsigned long WIFI_CHECK_INTERVAL  = 5000UL;
const unsigned long OLED_INTERVAL        = 250UL;
const unsigned long HALL_CALC_INTERVAL   = 200UL;
const unsigned long HALL_TIMEOUT_MS      = 2000UL;

//HTTP queue 
enum HttpJob { JOB_NONE, JOB_TELEMETRY, JOB_ACCIDENT, JOB_DISPLAY_POLL };
HttpJob pendingJob = JOB_NONE;

//I2C recovery helper
//Manually clocks SCL up to 9 times to free a stuck slave,
//then issues a STOP. Call before Wire.begin() and after any lockup.
void i2cRecovery() {
  const int SDA_PIN = 21;
  const int SCL_PIN = 22;
  pinMode(SDA_PIN, OUTPUT);
  pinMode(SCL_PIN, OUTPUT);
  digitalWrite(SDA_PIN, HIGH);
  for (int i = 0; i < 9; i++) {
    digitalWrite(SCL_PIN, LOW);  delayMicroseconds(5);
    digitalWrite(SCL_PIN, HIGH); delayMicroseconds(5);
  }
  //STOP condition
  digitalWrite(SDA_PIN, LOW);  delayMicroseconds(5);
  digitalWrite(SCL_PIN, HIGH); delayMicroseconds(5);
  digitalWrite(SDA_PIN, HIGH); delayMicroseconds(5);
  //Hand back to Wire
  pinMode(SDA_PIN, INPUT_PULLUP);
  pinMode(SCL_PIN, INPUT_PULLUP);
}

//OLED init with retry
bool initOLED() {
  for (int attempt = 1; attempt <= 3; attempt++) {
    Serial.printf("OLED init attempt %d/3...\n", attempt);
    i2cRecovery();
    Wire.begin(21, 22);
    Wire.setClock(100000);
    delay(50);

    if (display.begin(0x3C, true)) {
      display.clearDisplay();
      display.setTextColor(SH110X_WHITE);
      Serial.println("OLED OK");
      return true;
    }
    Serial.println("OLED not found, retrying...");
    Wire.end();
    delay(200);
  }
  Serial.println("OLED FAILED after 3 attempts – continuing without display");
  return false;
}

//MPU6050 init
bool initMPU() {
  // Reset
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B); Wire.write(0x80);
  if (Wire.endTransmission() != 0) return false;
  delay(150);

  //Wake
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B); Wire.write(0x00);
  if (Wire.endTransmission() != 0) return false;
  delay(50);

  //±8 g range (4096 LSB/g) 
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1C); Wire.write(0x10);
  if (Wire.endTransmission() != 0) return false;

  //Low-pass filter: 44 Hz 
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x1A); Wire.write(0x03);
  Wire.endTransmission();

  Serial.println("MPU6050 OK");
  return true;
}


void setup() {
  Serial.begin(115200);
  delay(100);

  //OLED
  oledOK = initOLED();
  if (oledOK) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(10, 25);
    display.println("Booting...");
    display.display();
  }

  //WiFi
  WiFi.begin(ssid, password);
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 12000) {
    delay(300);
  }
  bool wifiConnected = (WiFi.status() == WL_CONNECTED);
  Serial.println(wifiConnected ? "WiFi OK" : "WiFi FAILED");

  //Hall sensor
  pinMode(hallPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(hallPin), hallISR, FALLING);

  //GPS
  gpsSerial.begin(9600, SERIAL_8N1, 16, 17);

  //MPU6050 
  mpuOK = initMPU();

  //Boot screen
  if (oledOK) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(10, 5);  display.println("MOTO-TRACK PRO v3");
    display.setCursor(10, 18); display.printf("MPU: %s", mpuOK ? "OK" : "ERR");
    display.setCursor(10, 30); display.printf("WiFi: %s", wifiConnected ? "OK" : "FAIL");
    if (wifiConnected) {
      display.setCursor(10, 42);
      display.printf("IP:%s", WiFi.localIP().toString().c_str());
    }
    display.display();
    delay(2000);
  }
}

void loop() {
  unsigned long now = millis();

  // 1. Feed GPS parser
  while (gpsSerial.available())
    gps.encode(gpsSerial.read());

  // 2. Read IMU
  readMPU();

  // 3. Hall speed calc
  if (now - lastHallCalc >= HALL_CALC_INTERVAL) {
    lastHallCalc = now;
    calcHallSpeed(now);
  }

  //4. HTTP jobs
  if (WiFi.status() == WL_CONNECTED && pendingJob == JOB_NONE) {
    if (now - lastTelemetrySend >= TELEMETRY_INTERVAL) {
      pendingJob = JOB_TELEMETRY;
    } else if (now - lastDisplayPoll >= DISPLAY_POLL_INTERVAL) {
      pendingJob = JOB_DISPLAY_POLL;
    }
  }

  //5.Execute one HTTP job per loop pass
  if (pendingJob != JOB_NONE) {
    executeHttpJob();
  }

  //6.Accident display timeout
  if (accidentActive && now >= accidentDisplayUntil) {
    accidentActive = false;
    if (tiltAngle <= WARNING_TILT) bikeStatus = "safe";
  }

  //7.OLED update
  if (oledOK && now - lastOLEDUpdate >= OLED_INTERVAL) {
    lastOLEDUpdate = now;
    renderDisplay();
  }

  //8.WiFi reconnect
  if (now - lastWifiCheck >= WIFI_CHECK_INTERVAL) {
    lastWifiCheck = now;
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.reconnect();
    }
  }
}


void calcHallSpeed(unsigned long now) {
  noInterrupts();
  unsigned long intv       = pulseInterval;
  unsigned long sinceLastP = now - lastPulseTime;
  interrupts();

  if (sinceLastP > HALL_TIMEOUT_MS) {
    rpm          = 0.0f;
    hallSpeedKmh = 0.0f;
  } else if (intv > 0) {
    rpm          = 60000.0f / (float)intv;
    hallSpeedKmh = (2.0f * PI * radiusMeters * rpm * 60.0f) / 1000.0f;
  }
}

void readMPU() {
  if (!mpuOK) {
    //Attempt MPU re-init once in a while if it failed
    static unsigned long lastMpuRetry = 0;
    if (millis() - lastMpuRetry > 5000) {
      lastMpuRetry = millis();
      mpuOK = initMPU();
    }
    return;
  }

  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  uint8_t err = Wire.endTransmission(false);
  if (err != 0) {
    mpuOK = false;
    Serial.printf("MPU I2C err: %d  will retry\n", err);
    //Attempt bus recovery
    i2cRecovery();
    Wire.begin(21, 22);
    Wire.setClock(100000);
    return;
  }

  uint8_t got = Wire.requestFrom(MPU_ADDR, 6);
  if (got != 6) { mpuOK = false; return; }

  int16_t ax_raw = (Wire.read() << 8) | Wire.read();
  int16_t ay_raw = (Wire.read() << 8) | Wire.read();
  int16_t az_raw = (Wire.read() << 8) | Wire.read();

  ax_g     = ax_raw / 4096.0f;
  ay_g     = ay_raw / 4096.0f;
  az_g     = az_raw / 4096.0f;
  totalAcc = sqrtf(ax_g*ax_g + ay_g*ay_g + az_g*az_g);

  float clamped = constrain(ay_g, -1.0f, 1.0f);
  tiltAngle = acosf(clamped) * (180.0f / PI);
  mpuOK = true;

  //Accident state machine
  unsigned long now  = millis();
  bool isFallen  = (tiltAngle >= TILT_THRESHOLD);
  bool isTilted  = (tiltAngle >= WARNING_TILT);

  if (isFallen) {
    if (tiltStartTime == 0) tiltStartTime = now;
  } else {
    tiltStartTime = 0;
    //Reset per-accident message counter only once fully upright
    if (!isTilted) accidentMsgCount = 0;
  }

  bool persistedFall = isFallen && (now - tiltStartTime >= TILT_PERSIST_TIME);
  bool cooldownOver  = (now - lastAccidentTime > ACCIDENT_COOLDOWN);

  if (persistedFall && cooldownOver) {
    lastAccidentTime     = now;
    accidentActive       = true;
    accidentDisplayUntil = now + ACCIDENT_SHOW_MS;
    tiltStartTime        = 0;
    bikeStatus           = "accident";
    Serial.println("*** ACCIDENT DETECTED ***");

    // Queue accident alert (higher priority than telemetry)
    if (accidentMsgCount < MAX_ACCIDENT_MSG) {
      pendingJob = JOB_ACCIDENT;   // will be sent next loop pass
      accidentMsgCount++;
    }
  } else if (!accidentActive) {
    bikeStatus = isTilted ? "warning" : "safe";
  }
}

//  Builds telemetry JSON into a fixed buffer
void buildTelemetryJson(char* buf, size_t len) {
  float gpsSpd = gps.location.isValid() ? gps.speed.kmph() : 0.0f;
  double lat   = gps.location.isValid() ? gps.location.lat() : 0.0;
  double lng   = gps.location.isValid() ? gps.location.lng() : 0.0;

  snprintf(buf, len,
    "{\"lat\":%.6f,\"lng\":%.6f,\"speed\":%.1f,"
    "\"satellites\":%u,\"hall_speed\":%.1f,\"rpm\":%.0f,"
    "\"tilt_angle\":%.1f,\"total_accel\":%.2f,\"status\":\"%s\"}",
    lat, lng, gpsSpd,
    gps.satellites.value(), hallSpeedKmh, rpm,
    tiltAngle, totalAcc, bikeStatus.c_str()
  );
}

void applyDisplayConfig(const char* jsonStr) {
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, jsonStr) != DeserializationError::Ok) return;

  JsonVariant root;
  if (doc.containsKey("display"))
    root = doc["display"].as<JsonVariant>();
  else
    root = doc.as<JsonVariant>();

  if (root.containsKey("template"))       currentTemplate   = root["template"].as<String>();
  if (root.containsKey("brightness"))     displayBrightness = root["brightness"].as<int>();
  if (root.containsKey("custom_message")) customMessage     = root["custom_message"].as<String>();
}


void executeHttpJob() {
  HTTPClient http;
  char buf[320];

  switch (pendingJob) {

    case JOB_TELEMETRY:
      lastTelemetrySend = millis();
      buildTelemetryJson(buf, sizeof(buf));
      http.begin(updateUrl);
      http.addHeader("Content-Type", "application/json");
      http.setTimeout(2500);
      if (http.POST(buf) > 0) {
        String resp = http.getString();
        applyDisplayConfig(resp.c_str());
      }
      http.end();
      break;

    case JOB_ACCIDENT: {
      double lat = gps.location.isValid() ? gps.location.lat() : 0.0;
      double lng = gps.location.isValid() ? gps.location.lng() : 0.0;
      snprintf(buf, sizeof(buf), "{\"lat\":%.6f,\"lng\":%.6f}", lat, lng);
      http.begin(accidentUrl);
      http.addHeader("Content-Type", "application/json");
      http.setTimeout(2500);
      http.POST(buf);
      http.end();
      Serial.println("Accident alert sent");
      break;
    }

    case JOB_DISPLAY_POLL:
      lastDisplayPoll = millis();
      http.begin(displayUrl);
      http.setTimeout(2000);
      if (http.GET() == 200) {
        String resp = http.getString();
        applyDisplayConfig(resp.c_str());
      }
      http.end();
      break;

    default: break;
  }

  pendingJob = JOB_NONE;
}

void renderDisplay() {
  if (currentTemplate == "off") {
    display.clearDisplay();
    display.display();
    return;
  }

  display.setContrast((uint8_t)displayBrightness);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);

  // default 
  if (currentTemplate == "default") {
    display.setCursor(0, 0);
    display.printf("RPM:%.0f  %.1fkm/h", rpm, hallSpeedKmh);
    display.setCursor(0, 10);
    if (gps.location.isValid())
      display.printf("GPS:%.1fkm/h S:%u", gps.speed.kmph(), gps.satellites.value());
    else
      display.print("GPS: No fix");

    display.setCursor(0, 20);
    if (!mpuOK) display.print("MPU: ERROR");
    else        display.printf("Tilt:%+5.1f deg", tiltAngle);

    display.setCursor(0, 30);
    if (!mpuOK) display.print("Acc: --");
    else        display.printf("Acc: %.2f g", totalAcc);

    display.drawLine(0, 40, 128, 40, SH110X_WHITE);
    display.setCursor(0, 44);
    display.print(WiFi.status() == WL_CONNECTED ? "WiFi:OK" : "WiFi:DOWN");
    display.setCursor(68, 44);
    display.printf("[%s]", bikeStatus.c_str());
  }

  // speed_only
  else if (currentTemplate == "speed_only") {
    display.setTextSize(3);
    display.setCursor(10, 5);
    display.printf("%.0f", hallSpeedKmh);
    display.setTextSize(1);
    display.setCursor(90, 15); display.print("km/h");
    display.setCursor(10, 40); display.print("Hall Speed");
    display.setCursor(10, 52);
    display.printf("GPS: %.1f km/h", gps.location.isValid() ? gps.speed.kmph() : 0.0f);
  }

  // gps_only 
  else if (currentTemplate == "gps_only") {
    if (gps.location.isValid()) {
      display.setCursor(0, 0);  display.printf("Lat: %.6f", gps.location.lat());
      display.setCursor(0, 12); display.printf("Lng: %.6f", gps.location.lng());
      display.setCursor(0, 28); display.printf("Spd: %.1f km/h", gps.speed.kmph());
      display.setCursor(0, 40); display.printf("Sats: %u", gps.satellites.value());
    } else {
      display.setCursor(0, 0);  display.print("GPS: Searching...");
      display.setCursor(0, 14); display.printf("Sats: %u", gps.satellites.value());
    }
  }

  // minimal 
  else if (currentTemplate == "minimal") {
    display.setTextSize(4);
    display.setCursor(15, 8);
    display.printf("%.0f", hallSpeedKmh);
    display.setTextSize(1);
    display.setCursor(48, 50); display.print("km/h");
  }

  // custom 
  else if (currentTemplate == "custom") {
    display.setTextSize(2);
    int msgLen = customMessage.length();
    int x = max(0, (128 - msgLen * 12) / 2);
    display.setCursor(x, 22);
    display.print(customMessage);
  }

  //Accident overlay
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