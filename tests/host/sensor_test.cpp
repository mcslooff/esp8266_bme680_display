#include <cassert>
#include <cmath>
#include "../../src/sensor/BME680Sensor.h"

void sensorTests() {
  Adafruit_BME680 bme;
  BME680Sensor sensor(bme);  // Same default correction as the firmware sketch.
  assert(sensor.begin());
  assert(std::fabs(sensor.temperatureCorrection() - (-3.1f)) < 0.0001f);

  Measurement measurement;
  for (int i = 0; i < 2; ++i) {
    assert(sensor.read(3.3f, measurement));
    assert(std::fabs(measurement.temperature - 19.0f) < 0.0001f);
    assert(bme.temperature == 22.1f);  // Repeated reads must not compound the correction.
    assert(measurement.humidity == bme.humidity);
    assert(measurement.pressurePa == bme.pressure);
    assert(measurement.gasResistanceOhm == bme.gas_resistance);
    assert(measurement.batteryVoltage == 3.3f);
  }
}
