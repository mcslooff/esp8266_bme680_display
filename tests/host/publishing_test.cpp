#include <cassert>
#include <cstring>
#include <iostream>
#include <ESP8266HTTPClient.h>
#include "../../src/publishing/Publisher.h"

SerialStub Serial;
WiFiStub WiFi;
EspStub ESP;
int HTTPClient::calls = 0;
int HTTPClient::result = 200;
std::string HTTPClient::payload;

struct Fixture {
  AppConfig cfg;
  Logger log;
  AsyncMqttClient client;
  MeasurementStore store{0};
  MqttPublisher mqtt{client, cfg.mqtt, log};
  Publisher publisher{cfg, store, log, mqtt};
  Measurement measurement;

  Fixture() {
    Serial.output.clear();
    WiFi.connectionStatus = WL_CONNECTED;
    ESP.freeHeap = 40000;
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

  void connect(uint32_t now = 0) {
    mqtt.update(now);
    assert(client.attemptPending && !client.connected());
    client.completeConnect();
    mqtt.update(now + 1);
  }
};

size_t occurrences(const std::string& text, const std::string& needle) {
  size_t count = 0, offset = 0;
  while ((offset = text.find(needle, offset)) != std::string::npos) {
    ++count;
    offset += needle.size();
  }
  return count;
}

void transportModes() {
  for (int mode = 0; mode < 4; ++mode) {
    Fixture f;
    f.cfg.measurement.publishHttp = mode & 1;
    f.cfg.measurement.publishMqtt = mode & 2;
    if (mode & 2) f.connect();
    const std::string expected = f.publisher.json(f.measurement).c_str();
    assert(expected.size() > 256);
    assert(f.publisher.publish(f.measurement));
    assert(HTTPClient::calls == ((mode & 1) ? 1 : 0));
    assert(f.client.publishCalls == ((mode & 2) ? 1 : 0));
    if (mode & 1) assert(HTTPClient::payload == expected);
    if (mode & 2) {
      assert(f.client.payload == expected);
      assert(f.client.declaredSize == expected.size());
      assert(f.client.topic == "measurements");
      assert(f.client.qos == 0 && !f.client.retain);
    }
  }
}

void credentialsAndConnectionTruth() {
  Fixture f;
  assert(f.client.keepAlive == 60);
  std::strcpy(f.cfg.mqtt.username, "user");
  std::strcpy(f.cfg.mqtt.password, "password");
  std::strcpy(f.cfg.mqtt.listenerTopic, "commands");
  // publish() never starts a connection, even before update() has run.
  assert(!f.mqtt.publish("{}"));
  assert(f.client.connectCalls == 0);
  f.mqtt.update(0);
  assert(f.client.username == "user" && f.client.password == "password");
  assert(f.client.broker == "broker.test" && f.client.port == 1883);
  for (uint32_t now = 1; now < 100; ++now) {
    f.mqtt.update(now);
    assert(!f.mqtt.publish("{}"));
  }
  assert(f.client.connectCalls == 1);
  f.client.completeConnect();
  // The client, not the cached state, is the truth for publish().
  assert(f.mqtt.publish("{}"));
  f.mqtt.update(100);
  f.mqtt.update(101);
  assert(f.client.subscribeCalls == 1 && f.client.listenerTopic == "commands");
  assert(f.client.connectCalls == 1);

  f.client.fail();
  assert(!f.mqtt.publish("{}"));
  f.mqtt.update(200);
  f.cfg.mqtt.port = 1884;
  std::strcpy(f.cfg.mqtt.producerTopic, "updated");
  f.cfg.mqtt.username[0] = '\0';
  f.mqtt.update(5200);
  assert(f.client.connectCalls == 2 && f.client.port == 1884);
  assert(f.client.username.empty() && f.client.password.empty());
  f.client.completeConnect();
  f.mqtt.update(5201);
  assert(f.client.subscribeCalls == 2);
  assert(f.mqtt.publish("{}") && f.client.topic == "updated");
}

void boundedBackoffAndRecovery() {
  Fixture f;
  uint32_t now = 100;
  f.mqtt.update(now);
  int attempts = 1;
  for (uint32_t delay : {5000u, 10000u, 20000u, 40000u, 80000u, 160000u, 300000u, 300000u}) {
    f.client.fail(AsyncMqttClientDisconnectReason::MQTT_NOT_AUTHORIZED);
    f.mqtt.update(++now);
    const std::string logs = Serial.output;
    assert(logs.find("not authorized") != std::string::npos);
    assert(logs.find("retry in " + std::to_string(delay / 1000) + " seconds") != std::string::npos);
    f.mqtt.update(now + delay - 1);
    assert(f.client.connectCalls == attempts);
    assert(occurrences(Serial.output, "MQTT state=disconnected") ==
           occurrences(logs, "MQTT state=disconnected"));
    assert(occurrences(Serial.output, "MQTT state=connecting") ==
           occurrences(logs, "MQTT state=connecting"));
    assert(!f.mqtt.publish("{}"));
    now += delay;
    f.mqtt.update(now);
    assert(f.client.connectCalls == ++attempts);
  }
  f.client.completeConnect();
  f.mqtt.update(++now);
  f.client.fail();
  f.mqtt.update(++now);
  f.mqtt.update(now + 4999);
  assert(f.client.connectCalls == attempts);
  f.mqtt.update(now + 5000);
  assert(f.client.connectCalls == attempts + 1);  // Success reset the back-off.
}

void timeoutRolloverAndWifi() {
  Fixture f;
  WiFi.connectionStatus = 0;
  f.mqtt.update(0);
  assert(f.client.connectCalls == 0);
  WiFi.connectionStatus = WL_CONNECTED;
  const uint32_t start = UINT32_MAX - 1000;
  f.mqtt.update(start);
  f.mqtt.update(uint32_t(start + 29999));
  assert(f.client.connectCalls == 1 && f.client.disconnectCalls == 0);
  f.mqtt.update(uint32_t(start + 30000));
  assert(f.client.disconnectCalls == 1);
  assert(Serial.output.find("connection timed out") != std::string::npos);
  f.mqtt.update(uint32_t(start + 34999));
  assert(f.client.connectCalls == 1);
  f.mqtt.update(uint32_t(start + 35000));
  assert(f.client.connectCalls == 2);

  // Retry timing itself also crosses rollover.
  Fixture g;
  g.mqtt.update(start);
  g.client.fail();
  g.mqtt.update(start);
  g.mqtt.update(uint32_t(start + 4999));
  assert(g.client.connectCalls == 1);
  g.mqtt.update(uint32_t(start + 5000));
  assert(g.client.connectCalls == 2);

  Fixture empty;
  empty.cfg.mqtt.broker[0] = '\0';
  empty.mqtt.update(0);
  assert(empty.client.connectCalls == 0);
}

void lostConnectionAndDuplicateEvents() {
  Fixture f;
  f.connect();
  // Recover even if a disconnect notification is absent.
  f.client.isConnected = false;
  f.mqtt.update(100);
  const std::string log = Serial.output;
  f.client.fail();
  f.mqtt.update(200);  // Duplicate notification must not restart/increase the timer.
  assert(Serial.output == log);
  WiFi.connectionStatus = 0;
  f.mqtt.update(5100);
  assert(f.client.connectCalls == 1);
  WiFi.connectionStatus = WL_CONNECTED;
  f.mqtt.update(5200);
  assert(f.client.connectCalls == 2);
}

void payloadLifetimeAndFailures() {
  Fixture f;
  f.connect();
  {
    const String payload(std::string(1024, 'x'));
    assert(f.mqtt.publish(payload));
  }
  assert(f.client.payload == std::string(1024, 'x'));
  assert(f.client.declaredSize == 1024);
  f.client.publishResult = 0;
  assert(!f.mqtt.publish("{}"));
  assert(Serial.output.find("Failed to queue MQTT message") != std::string::npos);
  ESP.freeHeap = 8200;
  const int calls = f.client.publishCalls;
  assert(!f.mqtt.publish(String(std::string(1024, 'x'))));
  assert(f.client.publishCalls == calls);
  assert(Serial.output.find("Not enough memory") != std::string::npos);
}

void connectionTraceLogging() {
  Fixture f;
  f.log.setLogLevel(LOG_INFO);
  WiFi.connectionStatus = 0;
  f.mqtt.update(0);
  assert(Serial.output.find("state=disconnected wifi=disconnected") != std::string::npos);
  assert(Serial.output.find("attempts=0 backoff_ms=0 retry_in_ms=0") != std::string::npos);
  const std::string first = Serial.output;
  f.mqtt.update(29999);
  assert(Serial.output == first);
  f.mqtt.update(30000);
  assert(occurrences(Serial.output, "MQTT status") == 2);

  WiFi.connectionStatus = WL_CONNECTED;
  std::strcpy(f.cfg.mqtt.password, "do-not-log-this");
  f.mqtt.update(30001);
  assert(Serial.output.find("wifi=connected") != std::string::npos);
  assert(Serial.output.find("attempt=1 backoff_ms=0 uptime_ms=30001") != std::string::npos);
  f.client.fail();
  f.mqtt.update(30002);
  assert(Serial.output.find("attempt=1 backoff_ms=5000") != std::string::npos);
  f.mqtt.update(35001);
  assert(f.client.connectCalls == 1);
  f.mqtt.update(35002);
  assert(Serial.output.find("attempt=2 backoff_ms=5000 uptime_ms=35002") != std::string::npos);
  f.client.completeConnect();
  f.mqtt.update(35003);
  assert(Serial.output.find("state=connected broker=broker.test:1883 attempt=2 backoff_reset_ms=5000") != std::string::npos);
  assert(Serial.output.find("do-not-log-this") == std::string::npos);

  Fixture rollover;
  WiFi.connectionStatus = 0;
  const uint32_t start = UINT32_MAX - 100;
  rollover.mqtt.update(start);
  rollover.mqtt.update(uint32_t(start + 29999));
  assert(occurrences(Serial.output, "MQTT status") == 1);
  rollover.mqtt.update(uint32_t(start + 30000));
  assert(occurrences(Serial.output, "MQTT status") == 2);
}

void bufferingAndSuccessRules() {
  Fixture f;
  f.cfg.measurement.publishMqtt = true;
  const String payload(std::string(1024, 'x'));
  // MQTT-only failures must propagate to the application's existing buffer path.
  assert(!f.publisher.publish(f.measurement));
  assert(!f.publisher.publishBufferedJson(payload));
  assert(HTTPClient::calls == 0 && f.client.connectCalls == 0);
  f.connect();
  assert(f.publisher.publishBufferedJson(payload));
  assert(f.client.payload == payload.c_str());

  f.cfg.measurement.publishHttp = true;
  f.client.publishResult = 0;
  assert(f.publisher.publish(f.measurement));  // HTTP success remains sufficient.
  HTTPClient::result = HTTPC_ERROR_CONNECTION_FAILED;
  assert(!f.publisher.publish(f.measurement));
  assert(!f.publisher.publishBufferedJson(payload));
  f.client.publishResult = 1;
  assert(f.publisher.publish(f.measurement));  // MQTT success remains sufficient.
  f.cfg.measurement.publishMqtt = false;
  assert(!f.publisher.publish(f.measurement));  // Disabled MQTT is not a success.
  assert(!f.publisher.publishBufferedJson(payload));
}

int main() {
  transportModes();
  credentialsAndConnectionTruth();
  boundedBackoffAndRecovery();
  timeoutRolloverAndWifi();
  lostConnectionAndDuplicateEvents();
  payloadLifetimeAndFailures();
  connectionTraceLogging();
  bufferingAndSuccessRules();
  std::cout << "Publishing/reconnect regression tests passed\n";
}
