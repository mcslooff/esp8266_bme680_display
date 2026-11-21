#pragma once
#include <Arduino.h>

constexpr int WL_CONNECTED = 3;
class WiFiClient {};
struct WiFiStub {
  int status() const { return WL_CONNECTED; }
  String macAddress() const { return "12:34:56:78:9a:bc"; }
};
extern WiFiStub WiFi;
