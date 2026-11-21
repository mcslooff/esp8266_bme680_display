#pragma once
#include <Arduino.h>
#include <functional>
#include <cassert>

enum class AsyncMqttClientDisconnectReason : uint8_t {
  TCP_DISCONNECTED, MQTT_UNACCEPTABLE_PROTOCOL_VERSION, MQTT_IDENTIFIER_REJECTED,
  MQTT_SERVER_UNAVAILABLE, MQTT_MALFORMED_CREDENTIALS, MQTT_NOT_AUTHORIZED,
  ESP8266_NOT_ENOUGH_SPACE, TLS_BAD_FINGERPRINT
};

// Deterministic asynchronous fake: connect() starts an attempt; the test drives
// its later completion/failure explicitly instead of completing inline.
class AsyncMqttClient {
public:
  bool isConnected = false, attemptPending = false;
  uint16_t publishResult = 1, subscribeResult = 1;
  int connectCalls = 0, credentialCalls = 0, publishCalls = 0, disconnectCalls = 0;
  int subscribeCalls = 0;
  size_t declaredSize = 0;
  std::string username, password, broker, topic, payload, listenerTopic;
  uint16_t port = 0;
  uint16_t keepAlive = 0;
  uint8_t qos = 255;
  bool retain = true;
  std::function<void(AsyncMqttClientDisconnectReason)> disconnected;

  bool connected() const { return isConnected; }
  AsyncMqttClient& setKeepAlive(uint16_t seconds) { keepAlive = seconds; return *this; }
  AsyncMqttClient& onDisconnect(std::function<void(AsyncMqttClientDisconnectReason)> fn) {
    disconnected = fn; return *this;
  }
  AsyncMqttClient& setCredentials(const char* user, const char* pass) {
    ++credentialCalls; username = user ? user : ""; password = pass ? pass : "";
    return *this;
  }
  AsyncMqttClient& setServer(const char* host, uint16_t number) {
    broker = host; port = number; return *this;
  }
  void connect() { ++connectCalls; attemptPending = true; }
  void completeConnect() { isConnected = true; attemptPending = false; }
  void fail(AsyncMqttClientDisconnectReason reason = AsyncMqttClientDisconnectReason::TCP_DISCONNECTED) {
    isConnected = false; attemptPending = false;
    if (disconnected) disconnected(reason);
  }
  void disconnect(bool force) {
    assert(force);
    ++disconnectCalls; fail();
  }
  uint16_t subscribe(const char* name, uint8_t level) {
    assert(level == 0);
    ++subscribeCalls; listenerTopic = name; return subscribeResult;
  }
  uint16_t publish(const char* name, uint8_t level, bool retained,
                   const char* body, size_t size) {
    ++publishCalls; topic = name; declaredSize = size; qos = level; retain = retained;
    payload.assign(body, size);
    return publishResult;
  }
};
