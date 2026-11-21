#pragma once
#include <Arduino.h>

#define MQTT_CONNECTION_REFUSED -2
#define MQTT_CONNECTION_TIMEOUT -1
#define MQTT_SUCCESS 0
#define MQTT_UNACCEPTABLE_PROTOCOL_VERSION 1
#define MQTT_IDENTIFIER_REJECTED 2
#define MQTT_SERVER_UNAVAILABLE 3
#define MQTT_BAD_USER_NAME_OR_PASSWORD 4
#define MQTT_NOT_AUTHORIZED 5

class MqttClient {
public:
  bool isConnected = false;
  int connectResult = 1, error = MQTT_NOT_AUTHORIZED;
  int beginResult = 1, endResult = 1;
  int connectCalls = 0, credentialCalls = 0, beginCalls = 0, endCalls = 0;
  size_t writeLimit = static_cast<size_t>(-1);
  unsigned long declaredSize = 0;
  std::string username, password, broker, topic, payload;
  int port = 0;

  uint8_t connected() { return isConnected; }
  void setUsernamePassword(const char* user, const char* pass) {
    ++credentialCalls; username = user; password = pass;
  }
  int connect(const char* host, uint16_t number) {
    ++connectCalls; broker = host; port = number;
    isConnected = connectResult;
    return connectResult;
  }
  int connectError() const { return error; }
  int beginMessage(const char* name, unsigned long size) {
    ++beginCalls; topic = name; declaredSize = size; payload.clear();
    return beginResult;
  }
  size_t print(const String& body) {
    const size_t written = std::min(body.length(), writeLimit);
    payload.assign(body.c_str(), written);
    return written;
  }
  int endMessage() { ++endCalls; return endResult; }
};
