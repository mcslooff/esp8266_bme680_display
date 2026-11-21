#include <cassert>
#include <cstring>
#include <iostream>
#include <ESP8266HTTPClient.h>
#include "../../src/publishing/Publisher.h"

SerialStub Serial;
WiFiStub WiFi;
int HTTPClient::calls = 0;
int HTTPClient::result = 200;
std::string HTTPClient::payload;

struct Fixture {
  AppConfig cfg;
  Logger log;
  MqttClient client;
  MeasurementStore store{0};
  MqttPublisher mqtt{client, cfg.mqtt, log};
  Publisher publisher{cfg, store, log, mqtt};
  Measurement measurement;

  Fixture() {
    Serial.output.clear();
    HTTPClient::calls = 0;
    HTTPClient::result = 200;
    HTTPClient::payload.clear();
    std::strcpy(cfg.mqtt.broker, "broker.test");
    std::strcpy(cfg.mqtt.producerTopic, "measurements");
    std::strcpy(cfg.httpPost.url, "http://collector.test");
    measurement.timestamp = 123456;
    measurement.temperature = 21.5;
    measurement.humidity = 50;
    measurement.pressurePa = 101325;
    measurement.gasResistanceOhm = 12345;
    measurement.batteryVoltage = 3.5;
  }
};

void transportModes() {
  for (int mode = 0; mode < 4; ++mode) {
    Fixture f;
    f.cfg.measurement.publishHttp = mode & 1;
    f.cfg.measurement.publishMqtt = mode & 2;
    const std::string expected = f.publisher.json(f.measurement).c_str();
    assert(expected.size() > 256);
    assert(f.publisher.publish(f.measurement));
    assert(HTTPClient::calls == ((mode & 1) ? 1 : 0));
    assert(f.client.beginCalls == ((mode & 2) ? 1 : 0));
    if (mode & 1) assert(HTTPClient::payload == expected);
    if (mode & 2) {
      assert(f.client.payload == expected);
      assert(f.client.declaredSize == expected.size());
      assert(f.client.topic == "measurements");
    }
  }
}

void connectionStateAndCredentials() {
  Fixture f;
  std::strcpy(f.cfg.mqtt.username, "user");
  std::strcpy(f.cfg.mqtt.password, "password");
  assert(f.mqtt.publish("{}"));
  assert(f.client.username == "user" && f.client.password == "password");
  assert(f.client.broker == "broker.test" && f.client.port == 1883);
  assert(f.mqtt.publish("{}"));
  assert(f.client.connectCalls == 1 && f.client.credentialCalls == 1);
  f.client.isConnected = false;
  f.cfg.mqtt.port = 1884;
  std::strcpy(f.cfg.mqtt.producerTopic, "updated");
  assert(f.mqtt.publish("{}"));
  assert(f.client.connectCalls == 2 && f.client.port == 1884);
  assert(f.client.topic == "updated");

  Fixture anonymous;
  assert(anonymous.mqtt.publish("{}"));
  assert(anonymous.client.credentialCalls == 0);
}

void failuresAreReported() {
  Fixture f;
  f.client.connectResult = 0;
  assert(!f.mqtt.publish("{}"));
  assert(f.client.beginCalls == 0);
  assert(Serial.output.find("broker.test:1883") != std::string::npos);
  assert(Serial.output.find("Not authorized") != std::string::npos);

  f.client.connectResult = 1;
  f.client.beginResult = 0;
  assert(!f.mqtt.publish("{}"));
  assert(f.client.endCalls == 0);
  assert(Serial.output.find("Failed to begin MQTT message") != std::string::npos);

  f.client.beginResult = 1;
  f.client.writeLimit = 1;
  assert(!f.mqtt.publish("{}"));
  assert(Serial.output.find("wrote 1/2 bytes") != std::string::npos);

  f.client.writeLimit = static_cast<size_t>(-1);
  f.client.endResult = 0;
  assert(!f.mqtt.publish("{}"));
  assert(Serial.output.find("result=0") != std::string::npos);
}

void bufferedPayloadAndSuccessRules() {
  Fixture f;
  f.cfg.measurement.publishMqtt = true;
  const String payload(std::string(1024, 'x'));
  assert(f.publisher.publishBufferedJson(payload));
  assert(f.client.payload == payload.c_str());
  assert(f.client.declaredSize == 1024);

  f.cfg.measurement.publishHttp = true;
  f.client.endResult = 0;
  assert(f.publisher.publish(f.measurement));  // HTTP success remains sufficient.
  HTTPClient::result = HTTPC_ERROR_CONNECTION_FAILED;
  assert(!f.publisher.publish(f.measurement));
  assert(!f.publisher.publishBufferedJson(payload));
  f.client.endResult = 1;
  assert(f.publisher.publish(f.measurement));  // MQTT success remains sufficient.
}

int main() {
  transportModes();
  connectionStateAndCredentials();
  failuresAreReported();
  bufferedPayloadAndSuccessRules();
  std::cout << "Publishing regression tests passed\n";
}
