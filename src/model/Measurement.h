#pragma once
#include <Arduino.h>
#include <time.h>

struct Measurement {
  time_t timestamp = 0;
  float temperature = NAN;
  float humidity = NAN;
  float pressurePa = NAN;
  float gasResistanceOhm = NAN;
  float batteryVoltage = NAN;

  bool valid() const {
    return isfinite(temperature) && isfinite(humidity) && isfinite(pressurePa) &&
           isfinite(gasResistanceOhm) && isfinite(batteryVoltage);
  }

  float pressureHpa() const { return pressurePa / 100.0f; }
  float gasResistanceKOhm() const { return gasResistanceOhm / 1000.0f; }
  float batteryPercent(float emptyV = 2.78f, float fullV = 3.78f) const {
    if (!isfinite(batteryVoltage) || fullV <= emptyV) return NAN;
    float p = (batteryVoltage - emptyV) * 100.0f / (fullV - emptyV);
    return constrain(p, 0.0f, 100.0f);
  }

  float dewPoint() const {
    if (!isfinite(temperature) || !isfinite(humidity) || humidity <= 0.0f) return NAN;
    constexpr float a = 17.27f;
    constexpr float b = 237.7f;
    const float gamma = (a * temperature) / (b + temperature) + logf(humidity / 100.0f);
    return (b * gamma) / (a - gamma);
  }
};
