#pragma once
#include "../model/Measurement.h"
#include "../config/ConfigManager.h"
#include "../storage/MeasurementStore.h"
#include "../system/Logger.h"
#include <ArduinoMqttClient.h>

class Publisher {
public:
  Publisher(const AppConfig& cfg, MeasurementStore& store, Logger& log, MqttClient& mqttClient)
    : cfg_(cfg), store_(store), log_(log), mqttClient_(mqttClient) {}
  bool publish(const Measurement& m);
  String jsonEscape(const String& in);
  bool publishBufferedJson(const String& body);
  void retryBuffered();
  String json(const Measurement& m);
private:
  const AppConfig& cfg_;
  MeasurementStore& store_;
  Logger& log_;
  MqttClient& mqttClient_;
  bool mqttConnected = false;
  bool mqttPost(const String& body);
  bool httpPost(const String& body);
  String getDeviceId() const;
};
