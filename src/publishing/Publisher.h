#pragma once
#include "../model/Measurement.h"
#include "../config/ConfigManager.h"
#include "../storage/MeasurementStore.h"
#include "../system/Logger.h"
#include "MqttPublisher.h"
#include "HttpPublisher.h"

class Publisher {
 public:
  Publisher(const AppConfig& cfg, MeasurementStore& store, Logger& log,
            MqttPublisher& mqttPublisher, HttpPublisher& httpPublisher)
      : cfg_(cfg),
        store_(store),
        log_(log),
        mqttPublisher_(mqttPublisher),
        httpPublisher_(httpPublisher) {}
  PublicationResult publish(const Measurement& m, uint32_t now);
  String jsonEscape(const String& in);
  PublicationResult publishBufferedJson(const String& body, uint32_t now);
  void update(uint32_t now);
  PublicationResult result() const { return result_; }
  String json(const Measurement& m);

 private:
  const AppConfig& cfg_;
  MeasurementStore& store_;
  Logger& log_;
  MqttPublisher& mqttPublisher_;
  HttpPublisher& httpPublisher_;
  bool mqttSucceeded_ = false;
  PublicationResult result_ = PublicationResult::Failed;
  String getDeviceId() const;
};
