#pragma once
#include <ArduinoMqttClient.h>
#include "../config/ConfigManager.h"
#include "../system/Logger.h"

class MqttPublisher {
public:
  MqttPublisher(MqttClient& client, const MqttConfiguration& cfg, Logger& log)
    : client_(client), cfg_(cfg), log_(log) {}
  bool publish(const String& payload);
private:
  MqttClient& client_;
  const MqttConfiguration& cfg_;
  Logger& log_;
  String translateMqttErrorCode(int code) const;
};
