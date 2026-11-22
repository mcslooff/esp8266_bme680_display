#pragma once
#include <ESPAsyncTCP.h>
#include "../config/ConfigManager.h"
#include "../system/Logger.h"
#include "PublicationResult.h"

// One asynchronous plain-HTTP POST at a time. Payload acceptance is not success:
// callers must retain the record until the response headers confirm HTTP 2xx.
class HttpPublisher {
 public:
  HttpPublisher(AsyncClient& client, const HttpPostConfiguration& cfg, Logger& log);
  HttpPublisher(const HttpPublisher&) = delete;
  HttpPublisher& operator=(const HttpPublisher&) = delete;
  PublicationResult publish(const String& payload, uint32_t now);
  void update(uint32_t now);
  PublicationResult result() const { return result_; }

 private:
  static constexpr uint32_t TIMEOUT_MS = 5000;
  static constexpr uint32_t RETRY_MS = 5000;
  AsyncClient& client_;
  const HttpPostConfiguration& cfg_;
  Logger& log_;
  PublicationResult result_ = PublicationResult::Failed;
  String host_, request_;
  uint16_t port_ = 80;
  uint32_t startedAt_ = 0, failedAt_ = 0;
  size_t sent_ = 0, headerBytes_ = 0, statusLength_ = 0;
  char statusLine_[96] = {};
  uint32_t headerTail_ = 0;
  int statusCode_ = 0;
  bool connected_ = false, firstLine_ = true, failureCooldown_ = false;
  bool cleanupPending_ = false;
  const char* failure_ = nullptr;

  bool makeRequest(const String& payload);
  void receive(const char* data, size_t length);
  void finish(bool success, const char* reason);
};
