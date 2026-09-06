#pragma once
#include "../model/Measurement.h"
#include "../config/ConfigManager.h"
#include "../storage/MeasurementStore.h"
#include "../system/Logger.h"
class Publisher {
public:
  Publisher(const AppConfig& cfg, MeasurementStore& store, Logger& log):cfg_(cfg),store_(store),log_(log){}
  bool publish(const Measurement& m);
  bool publishBufferedJson(const String& body);
  void retryBuffered();
private:
  const AppConfig& cfg_; MeasurementStore& store_; Logger& log_;
  bool httpPost(const String& body); bool domoticz(const Measurement& m); String json(const Measurement& m) const;
};
