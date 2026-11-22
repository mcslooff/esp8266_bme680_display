#pragma once
#include "../model/Measurement.h"
#include "../config/ConfigManager.h"
#include "MeasurementJsonSerializer.h"
#include "MqttPublisher.h"
#include "HttpPublisher.h"

class Publisher {
 public:
  Publisher(const MeasurementConfiguration& cfg, const MeasurementJsonSerializer& serializer,
            MqttPublisher& mqttPublisher, HttpPublisher& httpPublisher)
      : cfg_(cfg),
        serializer_(serializer),
        mqttPublisher_(mqttPublisher),
        httpPublisher_(httpPublisher) {}
  PublicationResult publish(const Measurement& m, uint32_t now);
  PublicationResult publishBufferedJson(const String& body, uint32_t now);
  void update(uint32_t now);
  PublicationResult result() const { return result_; }

 private:
  const MeasurementConfiguration& cfg_;
  const MeasurementJsonSerializer& serializer_;
  MqttPublisher& mqttPublisher_;
  HttpPublisher& httpPublisher_;
  bool mqttSucceeded_ = false;
  PublicationResult result_ = PublicationResult::Failed;
};
