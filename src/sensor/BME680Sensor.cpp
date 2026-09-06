#include "BME680Sensor.h"

bool BME680Sensor::begin() {
    if (sensor_.begin(0x76)) {
		initialized_ = true;
        return true;
    }

    if (sensor_.begin(0x77)) {
		initialized_ = true;
        return true;
    }

    initialized_ = false;
    return false;
}

bool BME680Sensor::warmupRead() {
  return initialized_ && sensor_.performReading();
}

bool BME680Sensor::read(float batteryVoltage, Measurement& out) {
  if (!initialized_ || !sensor_.performReading()) return false;
  out.timestamp = time(nullptr);
  out.temperature = sensor_.temperature + correctionC_;
  out.humidity = sensor_.humidity;
  out.pressurePa = sensor_.pressure;
  out.gasResistanceOhm = sensor_.gas_resistance;
  out.batteryVoltage = batteryVoltage;
  return out.valid();
}
