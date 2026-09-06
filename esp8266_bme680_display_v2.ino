/*
 * ESP8266 BME680 + SSD1306 Display v2
 * Reworked from the recovered 2019 ESP8266 application.
 * Legacy endpoints and configuration semantics are retained where practical.
 */
#include <Arduino.h>
#include <Wire.h>
#include <time.h>
#include <EEPROM.h>
#include <FS.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <Adafruit_BME680.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP8266FtpServer.h>

#include "src/config/ConfigManager.h"
#include "src/model/Measurement.h"
#include "src/storage/MeasurementStore.h"
#include "src/sensor/BME680Sensor.h"
#include "src/battery/BatteryMonitor.h"
#include "src/display/DisplayManager.h"
#include "src/network/WifiManager.h"
#include "src/publishing/Publisher.h"
#include "src/system/Logger.h"

static const char SOFTWARE_VERSION[] = "2.0.0";
static constexpr uint8_t BUTTON_A = 0;
static constexpr uint8_t BUTTON_B = 16;
static constexpr uint8_t BUTTON_C = 2;
static constexpr float ADC_MULTIPLIER = 0.0046948357f;
static constexpr uint32_t SENSOR_WARMUP_SECONDS = 300;
static constexpr uint32_t BUTTON_DEBOUNCE_MS = 250;
static constexpr char BUFFER_FILE[] = "/buffer.jsonl";

Adafruit_BME680 bme;
Adafruit_SSD1306 display(128, 32, &Wire);
ESP8266WebServer server(80);
FtpServer ftpSrv;

ConfigManager configStore;
AppConfig config;
Logger logger;
BatteryMonitor battery(ADC_MULTIPLIER);
MeasurementStore measurements(120);
BME680Sensor sensor(bme, -4.0f);  // retain legacy correction
WifiManager wifi(logger);
DisplayManager displayManager(display, measurements, battery, config);
Publisher publisher(config, measurements, logger);

uint32_t lastSampleMillis = 0;
uint32_t lastWarmupMillis = 0;
time_t lastMeasurement = 0;
uint32_t bootCounter = 0;

bool buttonStateA = true, buttonStateB = true, buttonStateC = true;
uint32_t lastButtonA = 0, lastButtonB = 0, lastButtonC = 0;

static bool auth() {
  if (!config.stationRequireAuthentication) return true;
  if (server.authenticate(config.stationUsername, config.stationPassword)) return true;
  server.requestAuthentication();
  return false;
}

static void safeCopy(char* dst, size_t n, const String& src) {
  if (n) src.toCharArray(dst, n);
}
static bool parseIPv4(const String& s, uint8_t out[4]) {
  int parts[4] = { 0 };
  int p = 0, start = 0;
  for (int i = 0; i <= s.length(); ++i) {
    if (i == s.length() || s[i] == '.') {
      if (p >= 4 || i == start) return false;
      String part = s.substring(start, i);
      for (size_t k = 0; k < part.length(); ++k) {
        char c = part[k];
        if (c < '0' || c > '9') return false;
      }
      parts[p++] = part.toInt();
      start = i + 1;
    }
  }
  if (p != 4) return false;
  for (int i = 0; i < 4; i++)
    if (parts[i] < 0 || parts[i] > 255) return false;
  for (int i = 0; i < 4; i++) out[i] = (uint8_t)parts[i];
  return true;
}

static String jsonEscape(const String& in) {
  String o;
  o.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); ++i) {
    char c = in[i];
    if (c == '"' || c == '\\') o += '\\';
    if (c == '\n') o += "\\n";
    else o += c;
  }
  return o;
}
static void sendMeasurementJson(const Measurement& m, int code = 0, const char* text = "Success") {
  String s = "{\"stationName\":\"" + jsonEscape(config.hostName) + "\",\"timestamp\":" + String((unsigned long)m.timestamp) + ",\"temperature\":" + String(m.temperature, 3) + ",\"humidity\":" + String(m.humidity, 3) + ",\"airpressure\":" + String(m.pressureHpa(), 3) + ",\"voc\":" + String(m.gasResistanceKOhm(), 3) + ",\"gasResistance\":" + String(m.gasResistanceOhm, 1) + ",\"battery\":" + String(m.batteryVoltage, 3) + ",\"dewpoint\":" + String(m.dewPoint(), 3) + ",\"resultCode\":" + String(code) + ",\"resultText\":\"" + jsonEscape(text) + "\"}";
  server.send(200, "application/json", s);
}

static int max(int val1, int val2) {
  return val2 > val1 ? val2 : val1;
}

static void handleRoot() {
  if (!auth()) return;
  if (server.method() == HTTP_POST) {
    if (server.hasArg("accessPointMode")) config.accessPointMode = server.arg("accessPointMode") == "on";
    if (server.hasArg("accessPointSSID")) safeCopy(config.accessPointSSID, sizeof(config.accessPointSSID), server.arg("accessPointSSID"));
    if (server.hasArg("accessPointPassword")) safeCopy(config.accessPointPassword, sizeof(config.accessPointPassword), server.arg("accessPointPassword"));
    if (server.hasArg("accessPointIPAddress")) parseIPv4(server.arg("accessPointIPAddress"), config.accessPointIP);
    if (server.hasArg("stationMode")) config.stationMode = server.arg("stationMode") == "on";
    if (server.hasArg("accessPointList")) safeCopy(config.stationSSID, sizeof(config.stationSSID), server.arg("accessPointList"));
    if (server.hasArg("stationPassword")) safeCopy(config.stationAccessPointPassword, sizeof(config.stationAccessPointPassword), server.arg("stationPassword"));
    if (server.hasArg("requireAuthentication")) config.stationRequireAuthentication = server.arg("requireAuthentication") == "on";
    if (server.hasArg("authenticationUsername")) safeCopy(config.stationUsername, sizeof(config.stationUsername), server.arg("authenticationUsername"));
    if (server.hasArg("authenticationPassword")) safeCopy(config.stationPassword, sizeof(config.stationPassword), server.arg("authenticationPassword"));
    if (server.hasArg("sampleInterval")) config.sensorSampleInterval = max(1, server.arg("sampleInterval").toInt());
    if (server.hasArg("publishURL")) safeCopy(config.publishingURL, sizeof(config.publishingURL), server.arg("publishURL"));
    if (server.hasArg("publishingUsername")) safeCopy(config.publishingUsername, sizeof(config.publishingUsername), server.arg("publishingUsername"));
    if (server.hasArg("publishingPassword")) safeCopy(config.publishingPassword, sizeof(config.publishingPassword), server.arg("publishingPassword"));
    if (server.hasArg("publishingPolicy")) safeCopy(config.publishingPolicy, sizeof(config.publishingPolicy), server.arg("publishingPolicy"));
    if (server.hasArg("stationHostname")) safeCopy(config.hostName, sizeof(config.hostName), server.arg("stationHostname"));
    if (server.hasArg("useNTP")) config.useNTP = server.arg("useNTP") == "on";
    if (server.hasArg("NTPOffset")) config.NTPOffset = server.arg("NTPOffset").toInt();
    if (server.hasArg("NTPPoolURL")) safeCopy(config.NTPPoolURL, sizeof(config.NTPPoolURL), server.arg("NTPPoolURL"));
    if (server.hasArg("serverPort")) {
      int p = server.arg("serverPort").toInt();
      if (p >= 1 && p <= 65535) config.serverPort = p;
    }
    if (server.hasArg("domoticz")) config.domoticz = server.arg("domoticz") == "on";
    if (server.hasArg("temphumbaridx")) safeCopy(config.temphumbaridx, sizeof(config.temphumbaridx), server.arg("temphumbaridx"));
    if (server.hasArg("vocidx")) safeCopy(config.vocidx, sizeof(config.vocidx), server.arg("vocidx"));
    configStore.save(config);
    server.send(200, "text/html", "<html><body><h2>Settings saved</h2><p>Rebooting...</p></body></html>");
    delay(250);
    ESP.restart();
    return;
  }
  static const char PAGE[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset=utf-8><meta name=viewport content="width=device-width"><title>ESP8266 BME680</title><style>body{font:16px sans-serif;max-width:760px;margin:auto;padding:1em}label{display:block;margin:.6em 0}input,select{width:100%;box-sizing:border-box;padding:.4em}button{padding:.6em 1em}.card{border:1px solid #bbb;border-radius:8px;padding:1em;margin:1em 0}</style></head><body>
<h1>ESP8266 BME680</h1><div class=card><h2>Status</h2><pre id=status>loading...</pre></div>
<div class=card><h2>Settings</h2><form method=post>
<label>AP enabled <input type=checkbox name=accessPointMode checked></label><label>AP SSID <input name=accessPointSSID></label><label>AP password <input name=accessPointPassword type=password></label><label>AP IP <input name=accessPointIPAddress value="192.168.4.1"></label>
<label>Station enabled <input type=checkbox name=stationMode></label><label>Station SSID <input name=accessPointList></label><label>Station password <input name=stationPassword type=password></label>
<label>Authentication <input type=checkbox name=requireAuthentication checked></label><label>Username <input name=authenticationUsername value=admin></label><label>Password <input name=authenticationPassword type=password value=admin></label>
<label>Sample interval (s) <input name=sampleInterval type=number min=1 value=10></label><label>Push URL <input name=publishURL></label><label>Push username <input name=publishingUsername></label><label>Push password <input name=publishingPassword type=password></label>
<label>Policy <select name=publishingPolicy><option>Poll</option><option>Push</option></select></label><label>Hostname <input name=stationHostname value=NodeMCU></label>
<label>NTP enabled <input type=checkbox name=useNTP checked></label><label>NTP offset seconds <input name=NTPOffset type=number value=3600></label><label>NTP pool <input name=NTPPoolURL value=nl.pool.ntp.org></label><label>HTTP port <input name=serverPort type=number value=80></label>
<label>Domoticz <input type=checkbox name=domoticz></label><label>Temp/Hum/Bar idx <input name=temphumbaridx value=0></label><label>Gas idx <input name=vocidx value=0></label><button>Save & reboot</button></form></div>
<script>setInterval(()=>fetch('/status/json').then(r=>r.json()).then(x=>status.textContent=JSON.stringify(x,null,2)).catch(e=>status.textContent=e),3000)</script></body></html>)HTML";
  server.send_P(200, "text/html", PAGE);
}

static void handleSettings() {
  if (!auth()) return;
  String ap = WiFi.softAPIP().toString(), sta = WiFi.localIP().toString();
  String s = "{\"accessPointMode\":" + String(config.accessPointMode ? "true" : "false") + ",\"accessPointSSID\":\"" + jsonEscape(config.accessPointSSID) + "\",\"accessPointPassword\":\"\",\"accessPointIPAddress\":\"" + IPAddress(config.accessPointIP).toString() + "\",\"stationMode\":" + String(config.stationMode ? "true" : "false") + ",\"stationSSID\":\"" + jsonEscape(config.stationSSID) + "\",\"stationPassword\":\"\",\"requireAuthentication\":" + String(config.stationRequireAuthentication ? "true" : "false") + ",\"sampleInterval\":" + String(config.sensorSampleInterval) + ",\"publishURL\":\"" + jsonEscape(config.publishingURL) + "\",\"publishingUsername\":\"" + jsonEscape(config.publishingUsername) + "\",\"publishingPolicy\":\"" + jsonEscape(config.publishingPolicy) + "\",\"stationHostname\":\"" + jsonEscape(config.hostName) + "\",\"useNTP\":" + String(config.useNTP ? "true" : "false") + ",\"NTPOffset\":" + String(config.NTPOffset) + ",\"NTPPoolURL\":\"" + jsonEscape(config.NTPPoolURL) + "\",\"serverPort\":" + String(config.serverPort) + ",\"domoticz\":" + String(config.domoticz ? "true" : "false") + ",\"temphumbaridx\":\"" + jsonEscape(config.temphumbaridx) + "\",\"vocidx\":\"" + jsonEscape(config.vocidx) + "\",\"apIP\":\"" + ap + "\",\"stationIP\":\"" + sta + "\"}";
  server.send(200, "application/json", s);
}

static void handleSensorRead() {
  if (!auth()) return;
  Measurement m;
  float v = battery.readVoltage();
  if (sensor.read(v, m)) {
    measurements.add(m);
    lastMeasurement = m.timestamp;
    sendMeasurementJson(m);
  } else {
    Measurement empty;
    server.send(503, "application/json", "{\"resultCode\":10,\"resultText\":\"Failed to perform reading\"}");
  }
}
static void handleStatus() {
  if (!auth()) return;
  Measurement m;
  String s = "<html><body><pre>";
  s += "Version: " + String(SOFTWARE_VERSION) + "\n";
  s += "Hostname: " + String(config.hostName) + "\n";
  s += "IP: " + wifi.localIP() + "\n";
  s += "AP IP: " + wifi.apIP() + "\n";
  s += "Battery: " + String(battery.readVoltage(), 3) + " V\n";
  s += "Samples: " + String(measurements.size()) + "/" + String(measurements.capacity()) + "\n";
  s += "Last measurement: " + String((unsigned long)lastMeasurement) + "\n";
  if (measurements.latest(m)) s += "Temperature: " + String(m.temperature, 2) + " C\nHumidity: " + String(m.humidity, 2) + " %\nPressure: " + String(m.pressureHpa(), 2) + " hPa\nGas: " + String(m.gasResistanceKOhm(), 2) + " kOhm\nDew point: " + String(m.dewPoint(), 2) + " C\n";
  s += "</pre></body></html>";
  server.send(200, "text/html", s);
}
static void handleStatusJson() {
  Measurement m;
  String s = "{\"version\":\"" + String(SOFTWARE_VERSION) + "\",\"stationName\":\"" + jsonEscape(config.hostName) + "\",\"accessPointIP\":\"" + wifi.apIP() + "\",\"stationIP\":\"" + wifi.localIP() + "\",\"powerVoltage\":" + String(battery.readVoltage(), 3) + ",\"systemTime\":" + String((unsigned long)time(nullptr)) + ",\"upTime\":" + String(millis()) + ",\"lastSampleTime\":" + String((unsigned long)lastMeasurement) + ",\"sampleCount\":" + String(measurements.size());
  if (measurements.latest(m)) s += ",\"temperature\":" + String(m.temperature, 3) + ",\"humidity\":" + String(m.humidity, 3) + ",\"airpressure\":" + String(m.pressureHpa(), 3) + ",\"gasResistance\":" + String(m.gasResistanceOhm, 1) + ",\"batteryPercent\":" + String(m.batteryPercent(), 2);
  s += '}';
  server.send(200, "application/json", s);
}
static void handleAPScan() {
  if (!auth()) return;
  wifi.scan();
  server.send(200, "application/json", "{\"accessPointList\":" + wifi.networksJson(config.stationSSID) + "}");
}
static void handleCSS() {
  server.send(200, "text/css", "body{font-family:sans-serif}");
}
static void handleJS() {
  server.send(200, "application/javascript", "console.log('ESP8266 BME680 v2');");
}
static void handleNotFound() {
  server.send(404, "text/plain", "Not found: " + server.uri());
}

static bool bufferMeasurement(const Measurement& m) {
  if (!SPIFFS.exists(BUFFER_FILE)) {
    File f = SPIFFS.open(BUFFER_FILE, "w");
    if (f) f.close();
  }
  File f = SPIFFS.open(BUFFER_FILE, "a");
  if (!f) return false;
  if (f.size() > 2000000) {
    f.close();
    return false;
  }
  String body = "{\"stationName\":\"" + jsonEscape(config.hostName) + "\",\"timestamp\":" + String((unsigned long)m.timestamp) + ",\"temperature\":" + String(m.temperature, 3) + ",\"humidity\":" + String(m.humidity, 3) + ",\"airpressure\":" + String(m.pressureHpa(), 3) + ",\"voc\":" + String(m.gasResistanceKOhm(), 3) + ",\"battery\":" + String(m.batteryVoltage, 3) + "}";
  f.println(body);
  f.close();
  return true;
}
static void tryPublish() {
  if (strcmp(config.publishingPolicy, "Push") != 0) return;
  // Replay buffered HTTP measurements first. Keep the remainder when the
  // remote endpoint is still unavailable. Domoticz measurements are not
  // replayed because their legacy format contains no durable queue record.
  if (!config.domoticz && SPIFFS.exists(BUFFER_FILE)) {
    File in = SPIFFS.open(BUFFER_FILE, "r");
    File out = SPIFFS.open("/buffer.tmp", "w");
    bool allOk = true;
    if (in && out) {
      while (in.available()) {
        String line = in.readStringUntil('\n');
        line.trim();
        if (!line.length()) continue;
        if (allOk && publisher.publishBufferedJson(line)) { continue; }
        allOk = false;
        out.println(line);
      }
      in.close();
      out.close();
      SPIFFS.remove(BUFFER_FILE);
      if (!allOk) SPIFFS.rename("/buffer.tmp", BUFFER_FILE);
      else SPIFFS.remove("/buffer.tmp");
    } else {
      if (in) in.close();
      if (out) out.close();
      SPIFFS.remove("/buffer.tmp");
    }
  }
  Measurement m;
  if (!measurements.latest(m)) return;
  if (!publisher.publish(m)) bufferMeasurement(m);
}
static void sampleIfDue() {
  const uint32_t interval = (uint32_t)max(1, config.sensorSampleInterval) * 1000UL;
  uint32_t now = millis();
  if (lastWarmupMillis == 0 || (uint32_t)(now - lastWarmupMillis) >= 30000UL) {
    sensor.warmupRead();
    lastWarmupMillis = now;
  }
  if (lastSampleMillis == 0 || (uint32_t)(now - lastSampleMillis) >= interval) {
    Measurement m;
    if (sensor.read(battery.readVoltage(), m)) {
      measurements.add(m);
      lastMeasurement = m.timestamp;
      lastSampleMillis = now;
      logger.info("Measurement: %.2f C, %.2f %% RH, %.2f hPa, %.2f kOhm\n", m.temperature, m.humidity, m.pressureHpa(), m.gasResistanceKOhm());
      tryPublish();
    }
  }
}
static void buttons() {
  const uint32_t now = millis();
  bool a = digitalRead(BUTTON_A), b = digitalRead(BUTTON_B), c = digitalRead(BUTTON_C);
  if (!a && buttonStateA && (uint32_t)(now - lastButtonA) >= BUTTON_DEBOUNCE_MS) {
    displayManager.toggle();
    lastButtonA = now;
  }
  if (!b && buttonStateB && (uint32_t)(now - lastButtonB) >= BUTTON_DEBOUNCE_MS) {
    displayManager.prev();
    lastButtonB = now;
  }
  if (!c && buttonStateC && (uint32_t)(now - lastButtonC) >= BUTTON_DEBOUNCE_MS) {
    displayManager.next();
    lastButtonC = now;
  }
  buttonStateA = a;
  buttonStateB = b;
  buttonStateC = c;
}

bool factoryResetRequested()
{
    pinMode(BUTTON_B, INPUT_PULLUP);

    if (digitalRead(BUTTON_B) != LOW)
        return false;

    const uint32_t start = millis();

    while (digitalRead(BUTTON_B) == LOW) {
        if (millis() - start >= 5000) {
            return true;
        }

        delay(10);
        yield();
    }

    return false;
}

void setup() {
  Serial.begin(115200);
  delay(50);
  logger.info("ESP8266 BME680 Display v%s\n", SOFTWARE_VERSION);
  pinMode(BUTTON_A, INPUT_PULLUP);
  pinMode(BUTTON_B, INPUT_PULLUP);
  pinMode(BUTTON_C, INPUT_PULLUP);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.display();
  displayManager.begin();
  displayManager.wake();
  configStore.begin();
  configStore.begin();
  if (factoryResetRequested()) {
    Serial.println("FACTORY RESET");

    configStore.factoryDefaults(config);
    configStore.save(config);

    delay(500);
    ESP.restart();
  }

  if (!configStore.load(config)) {
    configStore.factoryDefaults(config);
    configStore.save(config);
  }

  configStore.load(config);

  wifi.begin(config);
  wifi.scan();
  if (config.useNTP && config.stationMode) { configTime(config.NTPOffset, 0, config.NTPPoolURL); }
  if (!SPIFFS.begin()) {
    logger.error("SPIFFS init failed\n");
  } else {
    ftpSrv.begin("esp8266", "esp8266");
  }
  if (!sensor.begin()) { logger.error("BME680 not found.\n"); } else { logger.error("BME680 found and initialized.\n"); }
  server.on("/", HTTP_ANY, handleRoot);
  server.on("/sensor/read", HTTP_GET, handleSensorRead);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/settings", HTTP_GET, handleSettings);
  server.on("/aplist", HTTP_GET, handleAPScan);
  server.on("/nodemcu.css", HTTP_GET, handleCSS);
  server.on("/nodemcu.js", HTTP_GET, handleJS);
  server.on("/status/json", HTTP_GET, handleStatusJson);
  server.onNotFound(handleNotFound);
  server.begin();
  lastSampleMillis = millis() - ((uint32_t)config.sensorSampleInterval * 1000UL);
  logger.info("HTTP server started\n");
}

void loop() {
  server.handleClient();
  ftpSrv.handleFTP();
  wifi.update();
  buttons();
  displayManager.update();
  sampleIfDue();
  yield();
}
