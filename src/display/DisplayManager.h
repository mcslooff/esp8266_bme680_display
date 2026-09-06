#pragma once
#include <Wire.h>
#include <ESP8266WiFi.h>
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "../storage/MeasurementStore.h"
#include "../battery/BatteryMonitor.h"
#include "../config/ConfigManager.h"

class DisplayManager {
public:
  static constexpr int WIDTH=128, HEIGHT=32;
  DisplayManager(Adafruit_SSD1306& display, MeasurementStore& store, BatteryMonitor& battery, const AppConfig& cfg)
    : display_(display), store_(store), battery_(battery), cfg_(cfg) {}
  void begin();
  void update();
  void wake(); void toggle(); void next(); void prev();
  void addConnectionScreen(); void addDateTimeScreen(); void addSpiffsScreen();
  void setLogoTime(uint32_t ms) { logoTime_=ms; }
  void setAutoOffTime(uint32_t ms) { sleepTime_=ms; }
private:
  enum Screen : uint8_t { LAST, BATTERY, TEMP, HUM, GAS, PRESSURE, BATGRAPH, CONNECTION, DATETIME, SPIFFS, SCREEN_COUNT };
  Adafruit_SSD1306& display_; MeasurementStore& store_; BatteryMonitor& battery_; const AppConfig& cfg_;
  uint8_t screen_=LAST; bool on_=false; uint32_t turnedOnAt_=0; uint32_t logoTime_=2000; uint32_t sleepTime_=120000; bool showLogo_=false;
  void render(); void logo(); void last(); void batteryScreen(); void graph(uint8_t type); void connection(); void datetime(); void spiffs();
  void text(const char* s, int x, int y); void grid(int h,int v); void bar(int x,int y,int w,int h,float pct);
  static bool elapsed(uint32_t now,uint32_t since,uint32_t interval){return (uint32_t)(now-since)>=interval;}
};
