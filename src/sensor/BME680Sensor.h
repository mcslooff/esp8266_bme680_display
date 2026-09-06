#pragma once
#include <Arduino.h>
#include <Adafruit_BME680.h>
#include "../model/Measurement.h"

class BME680Sensor {
public:
  explicit BME680Sensor(Adafruit_BME680& sensor, float temperatureCorrectionC = -4.0f)
    : sensor_(sensor), correctionC_(temperatureCorrectionC) {}
  bool begin();
  bool read(float batteryVoltage, Measurement& out);
  bool warmupRead();
  bool present() const { return initialized_; }
  void setTemperatureCorrection(float c) { correctionC_ = c; }
  float temperatureCorrection() const { return correctionC_; }
private:
  Adafruit_BME680& sensor_;
  float correctionC_;
  bool initialized_ = false;
};
