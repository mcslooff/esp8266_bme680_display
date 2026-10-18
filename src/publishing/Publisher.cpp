#include "Publisher.h"
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <ArduinoMqttClient.h>
#include <FS.h>
#include <base64.h>
#include "../system/Version.h"

String Publisher::jsonEscape(const String& in) {
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

String Publisher::json(const Measurement& m) {
  String s = 
  String("{") +
  "  \"timestamp\": " + String((unsigned long)m.timestamp) + "," +
  "  \"station\" : {" +
  "    \"id\": \"" + jsonEscape(getDeviceId()) + "\"," +
  "    \"name\": \"" + jsonEscape(cfg_.station.hostname) + "\"," +
  "    \"version\": \""+ String(Version::SOFTWARE_VERSION) + "\"," +
  "    \"power\": {" +
  "      \"voltage\": " + String(m.batteryVoltage, 3) +
  "    }" +
  "  }," + 
  "  \"measurement\": ";
  bool prev = false;
  if(cfg_.measurement.temperature || cfg_.measurement.pressure || cfg_.measurement.humidity || cfg_.measurement.gasResistance) {
    s += "{";
      if(cfg_.measurement.temperature) {
        s+= String("    \"temperature\": {") +
            "      \"value\": " + String(m.temperature, 3) + "," +
            "      \"unit\": \"C\"" +
            "    }";
        prev = true;
      }
      if(cfg_.measurement.pressure) {
        s+= String(prev ? "," : "");
        s+= String("    \"airPressure\": {") +
            "      \"value\": " + String(m.pressureHpa(), 3) + "," +
            "      \"unit\": \"hPa\"" +
            "    }";
        prev = true;
      }
      if(cfg_.measurement.humidity) {
        s+= String(prev ? "," : "");
        s+= String("    \"humidity\": {") +
            "      \"value\": " + String(m.humidity, 3) + "," +
            "      \"unit\": \"%\"" +
            "    }";
        prev = true;
      }
      if(cfg_.measurement.gasResistance) {
        s+= String(prev ? "," : "");
        s+= String("    \"gasResistance\": {") +
            "      \"value\": " + String(m.gasResistanceOhm, 1) + "," +
            "      \"unit\": \"kOhm\"" +
            "    }";
      }
    s += String("}");
  } else {
    s += String("null");
  }
  s += String("}");
  return s;
}

String Publisher::getDeviceId() const
{
    String id = WiFi.macAddress();
    id.replace(":", "");
    id.toUpperCase();
    return id;
}

bool Publisher::mqttPost(const String& body) {

  if(!cfg_.measurement.publishMqtt) return true;

  if (cfg_.mqtt.username[0]) {
    mqttClient_.setUsernamePassword(
      cfg_.mqtt.username,
      cfg_.mqtt.password
    );
  }

  log_.debug("Checking MQTT connection\n");
  if (!mqttClient_.connect(cfg_.mqtt.broker, cfg_.mqtt.port)) {



    log_.error(
      "Failed to connect to MQTT broker %s:%d; error code=%d, %s\n",
      cfg_.mqtt.broker,
      cfg_.mqtt.port,
      mqttClient_.connectError(),
      translateMqttErrorCode(mqttClient_.connectError())
    );
    return false;
  }

  log_.debug("begin message\n");

  mqttClient_.beginMessage(
      cfg_.mqtt.producerTopic,
      static_cast<unsigned long>(body.length())
  );

  mqttClient_.print(body);
  mqttClient_.endMessage();
  log_.debug("end message\n");

  return true;
}

String Publisher::translateMqttErrorCode(const int code) {
  switch (code) {
    case MQTT_CONNECTION_REFUSED:
      return "Connection refused";

    case MQTT_CONNECTION_TIMEOUT:
      return "Connection timed out";

    case MQTT_SUCCESS:
      return "Success";

    case MQTT_UNACCEPTABLE_PROTOCOL_VERSION:
      return "Unacceptable MQTT protocol version";

    case MQTT_IDENTIFIER_REJECTED:
      return "Client identifier rejected";

    case MQTT_SERVER_UNAVAILABLE:
      return "MQTT server unavailable";

    case MQTT_BAD_USER_NAME_OR_PASSWORD:
      return "Bad username or password";

    case MQTT_NOT_AUTHORIZED:
      return "Not authorized";

    default:
      return String("Unknown MQTT error (") + code + ")";
  }
}

bool Publisher::httpPost(const String& body) {

  uint16_t timeout = 5000;
  if(!cfg_.measurement.publishHttp) return true;
  if (!cfg_.httpPost.url[0] || WiFi.status() != WL_CONNECTED) return false;
  WiFiClient client;
  HTTPClient http;
  if (!http.begin(client, cfg_.httpPost.url)) return false;
  http.setTimeout(timeout);
  http.addHeader("Content-Type", "application/json");

  // Add HTTP Basic Authentication when credentials are configured.
  if (cfg_.httpPost.username[0]) {
    http.setAuthorization(
      cfg_.httpPost.username,
      cfg_.httpPost.password
    );
  }
  int code = http.POST(body);

  if(code == HTTPC_ERROR_CONNECTION_FAILED) {
    log_.warn("connection failed\n");
    http.end();
    return false;
  }

  if (code == HTTPC_ERROR_READ_TIMEOUT) {
    log_.warn("HTTP response to %s timed out after %d ms\n", cfg_.httpPost.url, timeout);
    http.end();
    return false;
  }

#ifdef HTTPC_ERROR_CONNECTION_TIMEOUT
  if (code == HTTPC_ERROR_CONNECTION_TIMEOUT) {
    log_.warn("HTTP connection %s timed out after %d ms\n", cfg_.httpPost.url, timeout);
    http.end();
    return false;
  }
#endif

  if (code < 200 || code >= 300) {
    String responseBody = http.getString();
    log_.warn(
      "HTTP publish to %s failed with code %d and response body '%s'\n",
      cfg_.httpPost.url, code, responseBody
    );
    log_.warn("%s", body);
  }

  http.end();

  return code >= 200 && code < 300;
}

bool Publisher::publishBufferedJson(const String& body) {
  bool mqttOk = mqttPost(body);
  bool httpOk = httpPost(body);

  return httpOk || mqttOk;
}

bool Publisher::publish(const Measurement& m) {
  if(!cfg_.measurement.publishHttp && !cfg_.measurement.publishMqtt) return true;

  const String body = json(m);
  const bool httpOk = httpPost(body);
  const bool mqttOk = mqttPost(body);

  if (!(httpOk || mqttOk)) log_.warn("Publish failed\n");
  
  return httpOk || mqttOk;

}

void Publisher::retryBuffered() { /* Reserved for persistent replay; current v2 buffers JSON in the application. */
}
