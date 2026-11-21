#pragma once
#include <Arduino.h>

constexpr int WL_CONNECTED = 3;
class WiFiClient {};
struct WiFiStub {
  int connectionStatus = WL_CONNECTED;
  int status() const { return connectionStatus; }
  // Host-test fixture only; firmware uses its real ESP8266 hardware MAC.
  String macAddress() const { return "12:34:56:78:9a:bc"; }
};
extern WiFiStub WiFi;
struct EspStub {
  size_t freeHeap = 40000;
  size_t getFreeHeap() const { return freeHeap; }
};
extern EspStub ESP;
