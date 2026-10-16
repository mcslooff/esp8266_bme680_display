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
#include <ArduinoMqttClient.h>

#include "src/config/ConfigManager.h"
#include "src/model/Measurement.h"
#include "src/storage/MeasurementStore.h"
#include "src/sensor/BME680Sensor.h"
#include "src/battery/BatteryMonitor.h"
#include "src/display/DisplayManager.h"
#include "src/network/WifiManager.h"
#include "src/publishing/Publisher.h"
#include "src/system/Logger.h"
#include "src/system/Version.h"

static constexpr uint8_t BUTTON_A = 0;
static constexpr uint8_t BUTTON_B = 16;
static constexpr uint8_t BUTTON_C = 2;
static constexpr float ADC_MULTIPLIER = 0.0046948357f;
static constexpr uint32_t SENSOR_WARMUP_SECONDS = 300;
static constexpr uint32_t BUTTON_DEBOUNCE_MS = 250;
static constexpr char BUFFER_FILE[] = "buffer.json";

Adafruit_BME680 bme;
Adafruit_SSD1306 display(128, 32, &Wire);
ESP8266WebServer server(80);
WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);

ConfigManager configStore;
AppConfig config;
Logger logger;
BatteryMonitor battery(ADC_MULTIPLIER);
MeasurementStore measurements(120);
BME680Sensor sensor(bme, -4.0f);  // retain legacy correction
WifiManager wifi(logger);
DisplayManager displayManager(display, measurements, battery, config);
Publisher publisher(config, measurements, logger, mqttClient);

uint32_t lastSampleMillis = 0;
uint32_t lastWarmupMillis = 0;
time_t lastMeasurement = 0;
uint32_t bootCounter = 0;

bool buttonStateA = true, buttonStateB = true, buttonStateC = true;
uint32_t lastButtonA = 0, lastButtonB = 0, lastButtonC = 0;

uint16_t gasWarmupSeconds = 30;

static bool auth() {
  if (!config.server.requireAuthentication) return true;
  if (server.authenticate(config.server.username, config.server.password)) return true;
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

static int max(int val1, int val2) {
  return val2 > val1 ? val2 : val1;
}

static void handleRoot() {
  if (!auth()) return;
  if (server.method() == HTTP_POST) {
    if(server.hasArg("mode")) safeCopy(config.mode, sizeof(config.mode), server.arg("mode"));

    if (server.hasArg("accessPointSSID")) safeCopy(config.accessPoint.SSID, sizeof(config.accessPoint.SSID), server.arg("accessPointSSID"));
    if (server.hasArg("accessPointPassword") && !server.arg("accessPointPassword").isEmpty()) safeCopy(config.accessPoint.password, sizeof(config.accessPoint.password), server.arg("accessPointPassword"));
    if (server.hasArg("accessPointIPAddress")) parseIPv4(server.arg("accessPointIPAddress"), config.accessPoint.IP);
    
    if (server.hasArg("stationSSID")) safeCopy(config.station.SSID, sizeof(config.station.SSID), server.arg("stationSSID"));
    if (server.hasArg("stationPassword") && !server.arg("stationPassword").isEmpty()) safeCopy(config.station.password, sizeof(config.station.password), server.arg("stationPassword"));
    if (server.hasArg("stationHostname")) safeCopy(config.station.hostname, sizeof(config.station.hostname), server.arg("stationHostname"));
    
    if (server.hasArg("requireAuthentication")) config.server.requireAuthentication = server.arg("requireAuthentication") == "on";
    if (server.hasArg("authenticationUsername")) safeCopy(config.server.username, sizeof(config.server.username), server.arg("authenticationUsername"));
    if (server.hasArg("authenticationPassword") && !server.arg("authenticationPassword").isEmpty()) safeCopy(config.server.password, sizeof(config.server.password), server.arg("authenticationPassword"));
    
    if (server.hasArg("publishHttpPost")) config.measurement.publishHttp = server.arg("publishHttpPost") == "on";
    if (server.hasArg("publishMqtt")) config.measurement.publishMqtt = server.arg("publishMqtt") == "on";
    if (server.hasArg("sampleInterval")) config.measurement.sampleIntervalSeconds = max(1, server.arg("sampleInterval").toInt());
    if (server.hasArg("publishHumidity")) config.measurement.humidity = server.arg("publishHumidity") == "on";
    if (server.hasArg("publishTemperature")) config.measurement.temperature = server.arg("publishTemperature") == "on";
    if (server.hasArg("publishPressure")) config.measurement.pressure = server.arg("publishPressure") == "on";
    if (server.hasArg("publishGasResistance")) config.measurement.gasResistance = server.arg("publishGasResistance") == "on";
    
    if (server.hasArg("publishURL")) safeCopy(config.httpPost.url, sizeof(config.httpPost.url), server.arg("publishURL"));
    if (server.hasArg("publishingUsername")) safeCopy(config.httpPost.username, sizeof(config.httpPost.username), server.arg("publishingUsername"));
    if (server.hasArg("publishingPassword") && !server.arg("publishingPassword").isEmpty()) safeCopy(config.httpPost.password, sizeof(config.httpPost.password), server.arg("publishingPassword"));

    if (server.hasArg("mqttBrokerAddress")) safeCopy(config.mqtt.broker, sizeof(config.mqtt.broker), server.arg("mqttBrokerAddress"));
    if (server.hasArg("mqttUsername")) safeCopy(config.mqtt.username, sizeof(config.mqtt.username), server.arg("mqttUsername"));
    if (server.hasArg("mqttPassword")) safeCopy(config.mqtt.password, sizeof(config.mqtt.password), server.arg("mqttPassword"));
    if (server.hasArg("mqttPublishTopic")) safeCopy(config.mqtt.producerTopic, sizeof(config.mqtt.producerTopic), server.arg("mqttPublishTopic"));
    if (server.hasArg("mqttListenerTopic")) safeCopy(config.mqtt.listenerTopic, sizeof(config.mqtt.listenerTopic), server.arg("mqttListenerTopic"));

    if (server.hasArg("mqttPort")) {
      int p = server.arg("mqttPort").toInt();
      if (p >= 1 && p <= 65535) config.mqtt.port = p;
    }

    if (server.hasArg("useNTP")) config.ntp.useNTP = server.arg("useNTP") == "on";
    if (server.hasArg("NTPOffset")) config.ntp.NTPOffset = server.arg("NTPOffset").toInt();
    if (server.hasArg("NTPPoolURL")) safeCopy(config.ntp.NTPPoolURL, sizeof(config.ntp.NTPPoolURL), server.arg("NTPPoolURL"));

    if (server.hasArg("serverPort")) {
      int p = server.arg("serverPort").toInt();
      if (p >= 1 && p <= 65535) config.server.port = p;
    }

    if(!configStore.save(config))
      Serial.println("Failed to save configuration");

    server.send(200, "text/html", "<html><body><h2>Settings saved</h2><p>Rebooting...</p></body></html>");
    delay(250);
    ESP.restart();
    return;
  }

  static const char PAGE[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset=utf-8><meta name=viewport content="width=device-width"><title>ESP8266 BME680</title><style>body{font:16px sans-serif;max-width:760px;margin:auto;padding:1em}label{display:block;margin:.6em 0}input,select{width:100%;box-sizing:border-box;padding:.4em}button{padding:.6em 1em}.card{border:1px solid #bbb;border-radius:8px;padding:1em;margin:1em 0}</style></head><body onload="getSettings('/settings');">
<h1>ESP8266 BME680</h1><div class=card><h2>Status</h2><pre id=status>loading...</pre></div>
<div class=card><h2>Settings</h2><form method=post>

<div class=card>
<label>Mode <select name=mode id=mode></select></label>
</div>
<div class=card>
<label>AP SSID <input name=accessPointSSID id=accessPointSSID></label>
<label>AP password <input name=accessPointPassword id=accessPointPassword type=password></label>
<label>AP IP <input name=accessPointIPAddress id=accessPointIPAddress></label>
</div>
<div class=card>
<label>Station SSID <select name=stationSSID id=stationSSID></select></label>
<label>Station password <input name=stationPassword id=stationPassword type=password></label>
<label>Station IP <input name=stationIP id=stationIP readonly></label>
<label>Hostname <input name=stationHostname id=stationHostname value=NodeMCU></label>
</div>
<div class=card>
<label>HTTP port <input name=serverPort id=serverPort type=number value=80></label>
<label>Authentication <input type=checkbox name=requireAuthentication id=requireAuthentication checked></label>
<label>Username <input name=authenticationUsername id=authenticationUsername value=admin></label>
<label>Password <input name=authenticationPassword id=authenticationPassword type=password value=admin></label>
</div>
<div class=card>
<label>Publish over http POST <input type=checkbox name=publishHttpPost id=publishHttpPost checked></label>
<label>Publish over MQTT <input type=checkbox name=publishMqtt id=publishMqtt checked></label>
<label>Sample interval (10s - 3600s) <input name=sampleInterval id=sampleInterval type=number min=10 value=3600></label>
<label>Publish temperature <input type=checkbox name=sampleTemperature id=sampleTemperature checked></label>
<label>Public humidity <input type=checkbox name=sampleHumidity id=sampleHumidity checked></label>
<label>Publish pressure <input type=checkbox name=samplePressure id=samplePressure checked></label>
<label>Publish VOC gass resitence <input type=checkbox name=sampleVoc id=sampleVoc checked></label>
</div>
<div class=card>
<label>Push URL <input name=publishURL id=publishURL></label>
<label>Push username <input name=publishingUsername id=publishingUsername></label>
<label>Push password <input name=publishingPassword id=publishingPassword type=password></label>
</div>
<div class=card>
<label>MQTT broker <input name=mqttBrokerAddress id=mqttBrokerAddress ></label>
<label>MQTT port <input name=mqttPort id=mqttPort value=1883></label>
<label>Username <input name=mqttUsername id=mqttUsername></label>
<label>Password <input name=mqttPassword id=mqttPassword type=password></label>
<label>Publishing topic <input name=mqttPublishTopic id=mqttPublishTopic></label>
<label>Listener topic <input name=mqttListenerTopic id=mqttListenerTopic></label>
</div>
<div class=card>
<label>NTP enabled <input type=checkbox name=useNTP id=useNTP checked></label>
<label>NTP offset seconds <input name=NTPOffset id=NTPOffset type=number value=3600></label>
<label>NTP pool <input name=NTPPoolURL id=NTPPoolURL value=nl.pool.ntp.org></label>
</div>
<button>Save & reboot</button></form></div>
<script>
const statusElement = document.getElementById('status'); 
setInterval(()=>fetch('/status/json').then(r=>r.json()).then(x=>statusElement.textContent=JSON.stringify(x,null,2)).catch(e=>statusElement.textContent=e),3000);
function getSettings(url) {
 console.log("Gettings settings");
 var xhttp = new XMLHttpRequest();

 xhttp.onreadystatechange = function(){
   if(this.readyState == 4 && this.status == 200){
     setFormData.apply(xhttp);
   } else {
     console.log("Ready state: " + this.readyState);
     console.log("Status: " + this.status);
   }
 };
 xhttp.open("GET", url, true);
 xhttp.send();
}

function setFormData() {
 
 var obj = JSON.parse(this.responseText);
 
 for(var key in obj) {
   var element = document.getElementById(key);
   if(element == null) {
     console.log("Element: " + key + " not found by id.");
     var elements = document.getElementsByName(key);
     for(i=0; i<elements.length; i++) {
       elements[i].checked = obj[key][elements[i].value];
     }
   } else {
     if(element.type == 'select-one') {
       while(element.length > 0) {
         element.remove(0);
       }
       for(i=0; i<obj[key].length; i++) {
         var option = document.createElement('option');
         option.value = obj[key][i].value;
         option.text = obj[key][i].text;
         element.add(option);
         if(obj[key][i].selected==true) {
           element.selectedIndex = i;
         }
       }
     } else if(element.type == 'checkbox') {
       element.checked = obj[key];
     } else {
       element.value = obj[key];
     }
   }
 }
}
</script></body></html>)HTML";

  server.send_P(200, "text/html", PAGE);
}

static String modeJson() {
  String s = "[{\"value\": \"WiFiAccessPoint\",\"text\": \"WiFiAccessPoint\",\"selected\":";
  s += (strcmp(config.mode, "WiFiAccessPoint") == 0 ? "true" : "false");
  s += "},{\"value\": \"WiFiClient\",\"text\": \"WiFiClient\",\"selected\": "; 
  s += (strcmp(config.mode, "WiFiClient") == 0 ? "true" : "false");
  s += "}]";
  return s;
}

static void handleSettings() {
  if (!auth()) return;
  String ap = WiFi.softAPIP().toString(), sta = WiFi.localIP().toString();

  String s = "{\"mode\":" + modeJson() + "," +
             "\"accessPointSSID\":\"" + jsonEscape(config.accessPoint.SSID) + "\"," +
             "\"accessPointPassword\":\"" + jsonEscape(config.accessPoint.password) + "\"," + 
             "\"accessPointIPAddress\":\"" + IPAddress(config.accessPoint.IP).toString() + "\"," + 

             "\"stationSSID\":" + wifi.networksJson(config.station.SSID) + "," + 
             "\"stationPassword\":\"" + jsonEscape(config.station.password) + "\"," + 
             "\"stationIP\":\"" + sta + "\"," +
             "\"stationHostname\":\"" + jsonEscape(config.station.hostname) + "\"," + 

             "\"serverPort\":" + String(config.server.port) + "," +
             "\"requireAuthentication\":" + String(config.server.requireAuthentication ? "true" : "false") + "," + 
             "\"authenticationUsername\":\"" + jsonEscape(config.server.username) + "\"," +
             "\"authenticationPassword\":\"" + jsonEscape(config.server.password) + "\"," +

             "\"publishHttpPost\":" + String(config.measurement.publishHttp ? "true" : "false") + "," +
             "\"publishMqtt\":" + String(config.measurement.publishMqtt ? "true" : "false") + "," +
             "\"sampleInterval\":" + String(config.measurement.sampleIntervalSeconds) + "," + 
             "\"sampleTemperature\":" + String(config.measurement.temperature ? "true" : "false") + "," +
             "\"sampleHumidity\":" + String(config.measurement.humidity ? "true" : "false") + "," +
             "\"samplePressure\":" + String(config.measurement.pressure ? "true" : "false") + "," +
             "\"sampleVoc\":" + String(config.measurement.gasResistance ? "true" : "false") + "," +

             "\"publishURL\":\"" + jsonEscape(config.httpPost.url) + "\"," + 
             "\"publishingUsername\":\"" + jsonEscape(config.httpPost.username) + "\"," + 
             "\"publishingPassword\":\"" + jsonEscape(config.httpPost.password) + "\"," + 

             "\"mqttBrokerAddress\":\"" + jsonEscape(config.mqtt.broker) + "\"," +
             "\"mqttPort\":" + config.mqtt.port + "," +
             "\"mqttUsername\":\"" + jsonEscape(config.mqtt.username) + "\"," +
             "\"mqttPassword\":\"" + jsonEscape(config.mqtt.password) + "\"," +
             "\"mqttPublishTopic\":\"" + jsonEscape(config.mqtt.producerTopic) + "\"," +
             "\"mqttListenerTopic\":\"" + jsonEscape(config.mqtt.listenerTopic) + "\"," +

             "\"useNTP\":" + String(config.ntp.useNTP ? "true" : "false") + "," + 
             "\"NTPOffset\":" + String(config.ntp.NTPOffset) + "," + 
             "\"NTPPoolURL\":\"" + jsonEscape(config.ntp.NTPPoolURL) + "\"," +
             
             "\"apIP\":\"" + ap + "\"" +
             "}";
  server.send(200, "application/json", s);
}

static void handleSensorRead() {
  if (!auth()) return;
  Measurement m;
  float v = battery.readVoltage();
  if (sensor.read(v, m)) {
    measurements.add(m);
    lastMeasurement = m.timestamp;
    server.send(200, "application/json", publisher.json(m));
  } else {
    Measurement empty;
    server.send(503, "application/json", "{\"resultCode\":10,\"resultText\":\"Failed to perform reading\"}");
  }
}

static void handleStatus() {
  if (!auth()) return;
  Measurement m;
  String s = "<html><body><pre>";
  s += "Version: " + String(Version::SOFTWARE_VERSION) + "\n";
  s += "Hostname: " + String(config.station.hostname) + "\n";
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
  String s = "{\"version\":\"" + String(Version::SOFTWARE_VERSION) + "\",\"stationName\":\"" + jsonEscape(config.station.hostname) + "\",\"accessPointIP\":\"" + wifi.apIP() + "\",\"stationIP\":\"" + wifi.localIP() + "\",\"powerVoltage\":" + String(battery.readVoltage(), 3) + ",\"systemTime\":" + String((unsigned long)time(nullptr)) + ",\"upTime\":" + String(millis()) + ",\"lastSampleTime\":" + String((unsigned long)lastMeasurement) + ",\"sampleCount\":" + String(measurements.size());
  if (measurements.latest(m)) s += ",\"temperature\":" + String(m.temperature, 3) + ",\"humidity\":" + String(m.humidity, 3) + ",\"airpressure\":" + String(m.pressureHpa(), 3) + ",\"gasResistance\":" + String(m.gasResistanceOhm, 1) + ",\"batteryPercent\":" + String(m.batteryPercent(), 2);
  s += '}';
  server.send(200, "application/json", s);
}

static void handleAPScan() {
  if (!auth()) return;
  wifi.scan();
  server.send(200, "application/json", "{\"accessPointList\":" + wifi.networksJson(config.station.SSID) + "}");
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

  String body = publisher.json(m);
  f.println(body);
  f.close();
  return true;
}

static void tryPublish() {

  if(!config.measurement.publishHttp && !config.measurement.publishMqtt) {
    return;
  }

  // Replay buffered HTTP measurements first. Keep the remainder when the
  // remote endpoint is still unavailable. Domoticz measurements are not
  // replayed because their legacy format contains no durable queue record.
  if (SPIFFS.exists(BUFFER_FILE)) {
    // Open buffer files
    File in = SPIFFS.open(BUFFER_FILE, "r");
    // Create backup file.
    File out = SPIFFS.open("/buffer.tmp", "w");
    bool allOk = true;
    int count = 0;
    int lineCount = 0;
    if (in && out) {
      while (in.available()) {
        // Read line from buffer file.
        String line = in.readStringUntil('\n');
        line.trim();
        if (!line.length()) continue;
        lineCount++;
        // If ublishing fails write buffered line to temp buffer.
        if (publisher.publishBufferedJson(line)) {
          count++; 
          continue; 
        }
        allOk = false;
        out.println(line);
      }
      logger.info("%d Lines in buffer\n", lineCount);
      logger.info("Off-loaded %d buffered lines\n", count);
      in.close();
      out.close();
      SPIFFS.remove(BUFFER_FILE);
      if (!allOk) {
        // Failed lines are in temp buffer rename to buffer file.
        SPIFFS.rename("/buffer.tmp", BUFFER_FILE);
      } else {
        // All lines were ofloaded, remove temp buffer.
        SPIFFS.remove("/buffer.tmp");
      }
    } else {
      if (in) in.close();
      if (out) {
        out.close();
        SPIFFS.remove("/buffer.tmp");
      }
    }
  }

  Measurement m;
  if (!measurements.latest(m)) return;
  if (!publisher.publish(m)) bufferMeasurement(m);
}

static bool sample() {

  const uint32_t now = millis();
  Measurement m;
  if (sensor.read(battery.readVoltage(), m)) {
    measurements.add(m);
    lastMeasurement = m.timestamp;
    lastSampleMillis = now;
    logger.info("Measurement: %.2f C, %.2f %% RH, %.2f hPa, %.2f kOhm\n", m.temperature, m.humidity, m.pressureHpa(), m.gasResistanceKOhm());
    return true;
  }
  return false;
}

static void sampleIfDue() {
  const uint32_t interval = (uint32_t)max(1, config.measurement.sampleIntervalSeconds) * 1000UL;
  uint32_t now = millis();
  if (lastWarmupMillis == 0 || (uint32_t)(now - lastWarmupMillis) >= 30000UL) {
    sensor.warmupRead();
    lastWarmupMillis = now;
  }
  if (lastSampleMillis == 0 || (uint32_t)(now - lastSampleMillis) >= interval) {
    if (sample()) {
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

/*
  Reset to factory default when the middle button remains 
  pressed for over 5 seconds during boot sequence.
*/
bool factoryResetRequested() {
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

void setupSerial() {
  // Initialize serial port
  Serial.begin(115200);
  delay(50);
  logger.info("ESP8266 BME680 Display v%s\n", Version::SOFTWARE_VERSION);
}

void setupButtons() {
  // Setup buttons
  pinMode(BUTTON_A, INPUT_PULLUP);
  pinMode(BUTTON_B, INPUT_PULLUP);
  pinMode(BUTTON_C, INPUT_PULLUP);
}

void setupDisplay() {
  // Setup display
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.display();
  displayManager.begin();
  displayManager.wake();
}

void loadConfiguration() {
  // Load configuratio
  configStore.begin();
  if (factoryResetRequested()) {
    Serial.println("FACTORY RESET");

    configStore.factoryDefaults(config);
    configStore.save(config);

    delay(500);
  }
  /* Make sure that when configuration is not correctly 
     loaded we reset to factory defaults.
  */
  if (!configStore.load(config)) {
    configStore.factoryDefaults(config);
    configStore.save(config);
  }

  configStore.load(config);

}

void setupWiFi() {
  // Setup WiFi
  wifi.begin(config);
  wifi.scan();
}

void setupNTP() {
  if (config.ntp.useNTP && strcmp(config.mode, "WiFiClient") == 0) {
    configTime(config.ntp.NTPOffset, 0, config.ntp.NTPPoolURL);
  }
}

void setupBme680() {
  // Setup BME680
  if (!sensor.begin()) {
    logger.error("BME680 not found.\n");
  } else {
    logger.error("BME680 found and initialized.\n");
  }
}

void setupSPIFFS() {
  // Setup SPIFFS
  if (!SPIFFS.begin()) {
    logger.error("SPIFFS init failed\n");
  }
}

void setupAPI() {
  /*
    Setup API end-points
  */
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
  logger.info("HTTP server started\n");
}

void onMqttMessage(int messageSize) {
  // we received a message, print out the topic and contents
  Serial.println("Received a message with topic '");
  Serial.print(mqttClient.messageTopic());
  Serial.print("', length ");
  Serial.print(messageSize);
  Serial.println(" bytes:");

  // use the Stream interface to print the contents
  while (mqttClient.available()) {
    Serial.print((char)mqttClient.read());
  }
  Serial.println();
  Serial.println();
}

void setupMqtt() {
  
  if(config.mqtt.broker[0]) {
    if (!mqttClient.connect(config.mqtt.broker, config.mqtt.port)) {
      Serial.println(mqttClient.connectError());
    }

    if(config.mqtt.listenerTopic[0]) {
      mqttClient.onMessage(onMqttMessage);
      mqttClient.subscribe(config.mqtt.listenerTopic);
    }
  }
}

void setup() {
  setupSerial();
  setupButtons();
  setupDisplay();
  loadConfiguration();
  setupWiFi();
  setupNTP();
  setupBme680();
  setupSPIFFS();
  setupAPI();
  setupMqtt();
  // Set last sample timestamp to now so first sample is delayed with sample-interval seconds.
  lastSampleMillis = millis() - ((uint32_t)config.measurement.sampleIntervalSeconds * 1000UL);
}

void loop() {
  server.handleClient();
  wifi.update();
  buttons();
  sampleIfDue();
  displayManager.update();

  if(config.mqtt.broker[0]) {
    // call poll() regularly to allow the library to send MQTT keep alive which
    // avoids being disconnected by the broker
    mqttClient.poll();
  }
  yield();
}
