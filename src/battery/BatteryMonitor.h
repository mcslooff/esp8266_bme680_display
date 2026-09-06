#pragma once
#include <Arduino.h>
class BatteryMonitor {
public:
  explicit BatteryMonitor(float multiplier) : multiplier_(multiplier) {}
  float readVoltage() const { return analogRead(A0) * multiplier_; }
  static float percentage(float voltage) {
    return constrain((voltage - 2.78f) * 100.0f, 0.0f, 100.0f);
  }
private:
  float multiplier_;
};
