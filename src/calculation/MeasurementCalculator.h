#pragma once
#include "../model/Measurement.h"
class MeasurementCalculator {
public:
  static float dewPoint(const Measurement& m) { return m.dewPoint(); }
  static float batteryPercent(const Measurement& m) { return m.batteryPercent(); }
};
