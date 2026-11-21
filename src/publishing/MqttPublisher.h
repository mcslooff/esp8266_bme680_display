#pragma once
#include <AsyncMqttClient.h>
#include "../config/ConfigManager.h"
#include "../system/Logger.h"

class MqttPublisher {
public:
  MqttPublisher(AsyncMqttClient& client, const MqttConfiguration& cfg, Logger& log);
  MqttPublisher(const MqttPublisher&) = delete;
  MqttPublisher& operator=(const MqttPublisher&) = delete;
  void update(uint32_t now);
  // Success means accepted by the async client, not acknowledged by the broker (QoS 0).
  bool publish(const String& payload);
private:
  enum class State { Disconnected, Connecting, Connected };
  static constexpr uint32_t INITIAL_RETRY_MS = 5000;
  static constexpr uint32_t MAX_RETRY_MS = 300000;
  static constexpr uint32_t CONNECT_TIMEOUT_MS = 30000;
  static constexpr uint32_t STATUS_INTERVAL_MS = 30000;

  AsyncMqttClient& client_;
  const MqttConfiguration& cfg_;
  Logger& log_;
  State state_ = State::Disconnected;
  uint32_t attemptStartedAt_ = 0;
  uint32_t retryStartedAt_ = 0;
  uint32_t retryDelay_ = INITIAL_RETRY_MS;
  uint32_t nextRetryDelay_ = INITIAL_RETRY_MS;
  uint32_t attemptCount_ = 0;
  uint32_t lastStatusAt_ = 0;
  int lastWifiStatus_ = -1;
  bool statusLogged_ = false;
  bool retryScheduled_ = false;
  bool disconnectPending_ = false;
  AsyncMqttClientDisconnectReason disconnectReason_ =
    AsyncMqttClientDisconnectReason::TCP_DISCONNECTED;

  void scheduleRetry(uint32_t now, const char* reason);
  void logStatus(uint32_t now);
  const char* translateDisconnectReason(AsyncMqttClientDisconnectReason reason) const;
};
