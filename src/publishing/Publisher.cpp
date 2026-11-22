#include "Publisher.h"

PublicationResult Publisher::publishBufferedJson(const String& body, uint32_t now) {
  if (result_ == PublicationResult::Pending) return PublicationResult::Failed;
  if (!cfg_.publishHttp && !cfg_.publishMqtt) return result_ = PublicationResult::Succeeded;
  mqttSucceeded_ = cfg_.publishMqtt && mqttPublisher_.publish(body);
  const PublicationResult http =
      cfg_.publishHttp ? httpPublisher_.publish(body, now) : PublicationResult::Failed;
  if (http == PublicationResult::Pending) return result_ = http;
  return result_ = mqttSucceeded_ || http == PublicationResult::Succeeded
                       ? PublicationResult::Succeeded
                       : PublicationResult::Failed;
}

PublicationResult Publisher::publish(const Measurement& m, uint32_t now) {
  if (result_ == PublicationResult::Pending) return PublicationResult::Failed;
  if (!cfg_.publishHttp && !cfg_.publishMqtt) return result_ = PublicationResult::Succeeded;
  String body;
  if (!serializer_.serialize(m, body)) return result_ = PublicationResult::Failed;
  return publishBufferedJson(body, now);
}

void Publisher::update(uint32_t now) {
  httpPublisher_.update(now);
  if (result_ == PublicationResult::Pending &&
      httpPublisher_.result() != PublicationResult::Pending) {
    result_ = mqttSucceeded_ || httpPublisher_.result() == PublicationResult::Succeeded
                  ? PublicationResult::Succeeded
                  : PublicationResult::Failed;
  }
}
