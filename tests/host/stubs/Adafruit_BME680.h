#pragma once
#include <cstdint>

class Adafruit_BME680 {
 public:
  float temperature = 22.1f;
  float humidity = 50.0f;
  float pressure = 101325.0f;
  uint32_t gas_resistance = 261728;

  bool begin(uint8_t) { return true; }
  bool performReading() { return true; }
};
