#pragma once
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include "../config/ConfigManager.h"
#include "../system/Logger.h"

class WifiManager {
public:
  explicit WifiManager(Logger& log): log_(log) {}
  void begin(const AppConfig& cfg);
  void update();
  int scan();
  int networkCount() const { return networkCount_; }
  String networksJson(const char* selected) const;
  String localIP() const { return WiFi.localIP().toString(); }
  String apIP() const { return WiFi.softAPIP().toString(); }
  bool connected() const { return WiFi.status()==WL_CONNECTED; }
private:
  Logger& log_; int networkCount_=0; unsigned long retryAt_=0; const AppConfig* cfg_=nullptr;
};
