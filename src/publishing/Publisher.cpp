#include "Publisher.h"
#include <ESP8266WiFi.h>
#include "../system/Version.h"

String Publisher::jsonEscape(const String& in) {
  String o;
  o.reserve(in.length() + 8);
  for (size_t i = 0; i < in.length(); ++i) {
    char c = in[i];
    if (c == '"' || c == '\\') o += '\\';
    if (c == '\n')
      o += "\\n";
    else
      o += c;
  }
  return o;
}

String Publisher::json(const Measurement& m) {
  String s = String("{") + "  \"timestamp\": " + String((unsigned long)m.timestamp) + "," +
             "  \"station\" : {" + "    \"id\": \"" + jsonEscape(getDeviceId()) + "\"," +
             "    \"name\": \"" + jsonEscape(cfg_.station.hostname) + "\"," +
             "    \"version\": \"" + String(Version::SOFTWARE_VERSION) + "\"," +
             "    \"power\": {" + "      \"voltage\": " + String(m.batteryVoltage, 3) + "    }" +
             "  }," + "  \"measurement\": ";
  bool prev = false;
  if (cfg_.measurement.temperature || cfg_.measurement.pressure || cfg_.measurement.humidity ||
      cfg_.measurement.gasResistance) {
    s += "{";
    if (cfg_.measurement.temperature) {
      s += String("    \"temperature\": {") + "      \"value\": " + String(m.temperature, 3) + "," +
           "      \"unit\": \"C\"" + "    }";
      prev = true;
    }
    if (cfg_.measurement.pressure) {
      s += String(prev ? "," : "");
      s += String("    \"airPressure\": {") + "      \"value\": " + String(m.pressureHpa(), 3) +
           "," + "      \"unit\": \"hPa\"" + "    }";
      prev = true;
    }
    if (cfg_.measurement.humidity) {
      s += String(prev ? "," : "");
      s += String("    \"humidity\": {") + "      \"value\": " + String(m.humidity, 3) + "," +
           "      \"unit\": \"%\"" + "    }";
      prev = true;
    }
    if (cfg_.measurement.gasResistance) {
      s += String(prev ? "," : "");
      s += String("    \"gasResistance\": {") +
           "      \"value\": " + String(m.gasResistanceOhm, 1) + "," + "      \"unit\": \"kOhm\"" +
           "    }";
    }
    s += String("}");
  } else {
    s += String("null");
  }
  s += String("}");
  return s;
}

String Publisher::getDeviceId() const {
  String id = WiFi.macAddress();
  id.replace(":", "");
  id.toUpperCase();
  return id;
}

PublicationResult Publisher::publishBufferedJson(const String& body, uint32_t now) {
  if (result_ == PublicationResult::Pending) return PublicationResult::Failed;
  if (!cfg_.measurement.publishHttp && !cfg_.measurement.publishMqtt)
    return result_ = PublicationResult::Succeeded;
  mqttSucceeded_ = cfg_.measurement.publishMqtt && mqttPublisher_.publish(body);
  const PublicationResult http =
      cfg_.measurement.publishHttp ? httpPublisher_.publish(body, now) : PublicationResult::Failed;
  if (http == PublicationResult::Pending) return result_ = http;
  return result_ = mqttSucceeded_ || http == PublicationResult::Succeeded
                       ? PublicationResult::Succeeded
                       : PublicationResult::Failed;
}

PublicationResult Publisher::publish(const Measurement& m, uint32_t now) {
  return publishBufferedJson(json(m), now);
}

void Publisher::update(uint32_t now) {
  httpPublisher_.update(now);
  if (result_ == PublicationResult::Pending &&
      httpPublisher_.result() != PublicationResult::Pending) {
    result_ = mqttSucceeded_ || httpPublisher_.result() == PublicationResult::Succeeded
                  ? PublicationResult::Succeeded
                  : PublicationResult::Failed;
  }
}
