/*
 * Project 3: Sensor Data Logger
 * ESP32 + INA219 + DS18B20 + SSD1306 OLED + microSD + WiFi UDP
 *
 * Button on GPIO 17:
 *   Short press  — start logging (new file) / stop logging
 *   Long press (2s) — freeze OLED for photography
 *
 * Files named: NNN_YYYYMMDD_HHMMSS.CSV
 * Session counter persists across power cycles via SD card scan on boot.
 * UDP streams continuously regardless of logging state.
 */

#include <Wire.h>
#include <SPI.h>
#include <SdFat.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <uri/UriBraces.h>
#include <time.h>
#include <Adafruit_INA219.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ── WiFi credentials ─────────────────────────────────────────
const char* SSID      = "YOUR_SSID";
const char* PASSWORD  = "YOUR_PASSWORD";
const char* LAPTOP_IP = "YOUR_LAPTOP_IP";  // run: ipconfig getifaddr en0
const int   UDP_PORT  = 5005;

// ── Pin definitions ──────────────────────────────────────────
#define ONE_WIRE_PIN  4
#define SD_CS_PIN     15
#define BUTTON_PIN    17

// ── Debug (set true to print sensor data to Serial Monitor) ──
#define DEBUG false

// ── OLED ─────────────────────────────────────────────────────
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT  64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ── Sensors ──────────────────────────────────────────────────
Adafruit_INA219 ina219;
OneWire oneWire(ONE_WIRE_PIN);
DallasTemperature tempSensor(&oneWire);

// ── SD ───────────────────────────────────────────────────────
SdFat sd;

// ── UDP + HTTP ───────────────────────────────────────────────
WiFiUDP udp;
WebServer server(80);

// ── Config ───────────────────────────────────────────────────
const unsigned long LOG_INTERVAL_MS = 1000;
const unsigned long DEBOUNCE_MS     = 50;

// ── State ────────────────────────────────────────────────────
char     FILENAME[32];
int      fileCount        = 0;
bool     isLogging        = false;
bool     photoMode        = false;
bool     sdOk             = false;
bool     inaOk            = false;
bool     wifiOk           = false;
unsigned long lastLogTime     = 0;
unsigned long lastDebounce    = 0;
unsigned long buttonPressTime = 0;
bool     lastButtonState      = HIGH;
bool     buttonState          = HIGH;

// ── Scan SD for highest existing session number ───────────────
int readFileCount() {
  SdFile root, file;
  char fname[64];
  int highest = 0;
  root.open("/");
  while (file.openNext(&root, O_READ)) {
    file.getName(fname, sizeof(fname));
    String name = String(fname);
    if (name.endsWith(".CSV") || name.endsWith(".csv")) {
      int count = name.substring(0, 3).toInt();
      if (count > highest) highest = count;
    }
    file.close();
  }
  root.close();
  return highest;
}

// ── Build filename: NNN_YYYYMMDD_HHMMSS.CSV ──────────────────
void buildFilename() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    snprintf(FILENAME, sizeof(FILENAME), "%03d_BOOT_%08lu.CSV", fileCount, millis());
    return;
  }
  char base[20];
  strftime(base, sizeof(base), "%Y%m%d_%H%M%S", &timeinfo);
  snprintf(FILENAME, sizeof(FILENAME), "%03d_%s.CSV", fileCount, base);
}

// ─────────────────────────────────────────────────────────────
void startLogging() {
  fileCount++;
  buildFilename();
  SdFile f;
  if (f.open(FILENAME, O_WRITE | O_CREAT | O_APPEND)) {
    f.println(F("time_ms,temp_C,bus_V,current_mA,power_mW"));
    f.close();
    isLogging = true;
    Serial.print(F("Logging started: "));
    Serial.println(FILENAME);
  } else {
    Serial.println(F("ERROR: Could not create log file"));
    sdOk = false;
  }
}

void stopLogging() {
  isLogging = false;
  Serial.println(F("Logging stopped."));
}

// ── HTTP: list CSV files on SD ────────────────────────────────
void handleFileList() {
  String json = "[";
  SdFile root, file;
  char fname[64];
  root.open("/");
  bool first = true;
  while (file.openNext(&root, O_READ)) {
    file.getName(fname, sizeof(fname));
    String name = String(fname);
    if (name.endsWith(".CSV") || name.endsWith(".csv")) {
      if (!first) json += ",";
      json += "\"" + name + "\"";
      first = false;
    }
    file.close();
  }
  root.close();
  json += "]";
  server.send(200, "application/json", json);
}

// ── HTTP: stream a file from SD to client ─────────────────────
void handleFileDownload() {
  String filename = server.pathArg(0);
  if (filename.indexOf('/') >= 0 || filename.indexOf('\\') >= 0) {
    server.send(400, "text/plain", "Bad filename");
    return;
  }
  SdFile f;
  if (!f.open(filename.c_str(), O_READ)) {
    server.send(404, "text/plain", "File not found");
    return;
  }
  server.sendHeader("Content-Disposition", "attachment; filename=" + filename);
  server.setContentLength(f.fileSize());
  server.send(200, "text/csv", "");
  uint8_t buf[512];
  int n;
  while ((n = f.read(buf, sizeof(buf))) > 0) {
    server.client().write(buf, n);
  }
  f.close();
}

// ── OLED helpers ──────────────────────────────────────────────
void displayVoltage(float voltage_V) {
  display.setTextSize(2);
  display.setCursor(0, 10);
  if (abs(voltage_V) >= 1) {
    display.print(voltage_V, 2);
    display.print("V");
  } else {
    display.print(voltage_V * 1000.0, 1);
    display.print("mV");
  }
}

void displayTemp(float tempC, bool valid) {
  display.setTextSize(2);
  display.setCursor(72, 10);
  if (valid) {
    display.print(tempC, 1);
    display.print("C");
  } else {
    display.print("--C");
  }
}

void displayCurrent(float current_mA) {
  display.setTextSize(2);
  display.setCursor(0, 34);
  if (abs(current_mA) >= 1000) {
    display.print(current_mA / 1000.0, 2);
    display.print("A");
  } else {
    display.print(current_mA, 1);
    display.print("mA");
  }
}

void displayPower(float power_mW) {
  display.setTextSize(1);
  display.setCursor(80, 38);
  display.print(F("Power:"));
  display.setCursor(80, 47);
  if (abs(power_mW) >= 1000) {
    display.print(power_mW / 1000.0, 1);
    display.print("W");
  } else {
    display.print(power_mW, 1);
    display.print("mW");
  }
}

// ─────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println(F("=== Sensor Data Logger ==="));

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // SD first — other inits don't affect SPI
  Serial.print(F("SD card..."));
  if (!sd.begin(SD_CS_PIN, SD_SCK_MHZ(4))) {
    Serial.println(F(" FAILED"));
  } else {
    Serial.println(F(" OK"));
    sdOk = true;
    fileCount = readFileCount();
    Serial.print(F("Last file count: "));
    Serial.println(fileCount);
  }

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED init failed"));
  } else {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println(F("Data Logger"));
    display.println(F("Starting up..."));
    display.display();
  }

  if (ina219.begin()) {
    ina219.setCalibration_16V_400mA();
    inaOk = true;
    Serial.println(F("INA219: OK"));
  } else {
    Serial.println(F("INA219: NOT FOUND"));
  }

  tempSensor.begin();
  tempSensor.setResolution(12);  // 0.0625°C resolution, 750ms conversion
  Serial.print(F("DS18B20 devices found: "));
  Serial.println(tempSensor.getDeviceCount());

  Serial.print(F("WiFi..."));
  WiFi.begin(SSID, PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print('.');
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    wifiOk = true;
    udp.begin(UDP_PORT);
    Serial.print(F(" connected. IP: "));
    Serial.println(WiFi.localIP());

    configTime(-7 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    Serial.print(F("NTP sync..."));
    struct tm timeinfo;
    int ntpAttempts = 0;
    while (!getLocalTime(&timeinfo) && ntpAttempts < 10) {
      delay(500);
      Serial.print('.');
      ntpAttempts++;
    }
    if (getLocalTime(&timeinfo)) {
      Serial.print(F(" OK — "));
      Serial.println(&timeinfo, "%Y-%m-%d %H:%M:%S");
    } else {
      Serial.println(F(" FAILED — filename will use millis()"));
    }
  } else {
    Serial.println(F(" FAILED"));
  }

  server.on("/files", handleFileList);
  server.on(UriBraces("/sd/{}"), handleFileDownload);
  server.begin();
  Serial.println(F("HTTP server started"));
  Serial.println(F("Ready. Press button to start logging."));
}

// ─────────────────────────────────────────────────────────────
void loop() {
  server.handleClient();

  // ── Button ───────────────────────────────────────────────
  bool reading = digitalRead(BUTTON_PIN);
  if (reading != lastButtonState) lastDebounce = millis();
  if ((millis() - lastDebounce) > DEBOUNCE_MS) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) {
        buttonPressTime = millis();
      } else {
        unsigned long holdTime = millis() - buttonPressTime;
        if (holdTime > 2000) {
          photoMode = !photoMode;
          Serial.println(photoMode ? F("Photo mode ON") : F("Photo mode OFF"));
        } else {
          if (!isLogging) {
            if (sdOk) startLogging();
          } else {
            stopLogging();
          }
        }
      }
    }
  }
  lastButtonState = reading;

  unsigned long now = millis();
  if (now - lastLogTime < LOG_INTERVAL_MS) return;
  lastLogTime = now;

  // ── Read sensors ─────────────────────────────────────────
  tempSensor.requestTemperatures();
  float tempC = tempSensor.getTempCByIndex(0);
  bool tempValid = (tempC != DEVICE_DISCONNECTED_C);

  float busV = 0, currentMA = 0, powerMW = 0;
  if (inaOk) {
    busV      = ina219.getBusVoltage_V();
    currentMA = ina219.getCurrent_mA();
    powerMW   = busV * currentMA;  // avoids 2mW quantization of getPower_mW()
  }

  // ── OLED ─────────────────────────────────────────────────
  if (!photoMode) {
    display.clearDisplay();

    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print(F("Voltage:"));
    display.setCursor(72, 0);
    display.print(F("Temp:"));

    displayVoltage(busV);
    displayTemp(tempC, tempValid);

    display.setTextSize(1);
    display.setCursor(0, 26);
    display.print(F("Current:"));
    displayCurrent(currentMA);

    displayPower(powerMW);

    display.setTextSize(1);
    display.setCursor(0, 56);
    display.print(wifiOk ? F("W:OK ") : F("W:-- "));
    if (sdOk) {
      display.print(F("SD:"));
      char countStr[4];
      snprintf(countStr, sizeof(countStr), "%03d", fileCount);
      display.print(countStr);
    } else {
      display.print(F("SD:---"));
    }
    display.print(isLogging ? F(" [REC]") : F(" [---]"));
    display.display();
  }

  // ── Serial (debug only) ───────────────────────────────────
  if (DEBUG) {
    Serial.print(now);
    Serial.print(F(" ms | "));
    Serial.print(tempValid ? tempC : -999.0, 2);
    Serial.print(F(" C | "));
    Serial.print(busV, 3);      Serial.print(F(" V | "));
    Serial.print(currentMA, 2); Serial.print(F(" mA | "));
    Serial.print(powerMW, 2);   Serial.println(F(" mW"));
  }

  // ── CSV row ───────────────────────────────────────────────
  String row = String(now) + "," +
               String(tempValid ? tempC : -999.0, 4) + "," +
               String(busV, 4) + "," +
               String(currentMA, 4) + "," +
               String(powerMW, 4);

  if (wifiOk) {
    udp.beginPacket(LAPTOP_IP, UDP_PORT);
    udp.print(row);
    udp.endPacket();
  }

  if (isLogging && sdOk) {
    SdFile f;
    if (f.open(FILENAME, O_WRITE | O_CREAT | O_APPEND)) {
      f.println(row.c_str());
      f.close();
    } else {
      Serial.println(F("SD write error"));
      sdOk = false;
    }
  }
}
