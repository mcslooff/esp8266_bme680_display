#include "MqttPublisher.h"
#include <ESP8266WiFi.h>
#include <string.h>

MqttPublisher::MqttPublisher(AsyncMqttClient& client, const MqttConfiguration& cfg, Logger& log)
    : client_(client), cfg_(cfg), log_(log) {
  client_.setKeepAlive(60);  // Preserve the previous client's keep-alive interval.
  // ESP8266 callbacks run cooperatively. Defer logging and retry scheduling to
  // update(), and never start another connection from a network callback.
  client_.onDisconnect([this](AsyncMqttClientDisconnectReason reason) {
    disconnectReason_ = reason;
    disconnectPending_ = true;
  });
}

void MqttPublisher::scheduleRetry(uint32_t now, const char* reason) {
  state_ = State::Disconnected;
  retryStartedAt_ = now;
  retryDelay_ = nextRetryDelay_;
  nextRetryDelay_ = nextRetryDelay_ < MAX_RETRY_MS / 2 ? nextRetryDelay_ * 2 : MAX_RETRY_MS;
  retryScheduled_ = true;
  log_.warn(
      "MQTT state=disconnected broker=%s:%d reason=%s; retry in %lu seconds "
      "[attempt=%lu backoff_ms=%lu uptime_ms=%lu]\n",
      cfg_.broker, cfg_.port, reason, static_cast<unsigned long>(retryDelay_ / 1000),
      static_cast<unsigned long>(attemptCount_), static_cast<unsigned long>(retryDelay_),
      static_cast<unsigned long>(now));
}

void MqttPublisher::logStatus(uint32_t now) {
  const int wifiStatus = WiFi.status();
  if (statusLogged_ && wifiStatus == lastWifiStatus_ &&
      uint32_t(now - lastStatusAt_) < STATUS_INTERVAL_MS)
    return;
  statusLogged_ = true;
  lastWifiStatus_ = wifiStatus;
  lastStatusAt_ = now;
  const bool connected = client_.connected();
  const char* state =
      connected ? "connected" : (state_ == State::Connecting ? "connecting" : "disconnected");
  const uint32_t elapsed = uint32_t(now - retryStartedAt_);
  const uint32_t remaining = retryScheduled_ && elapsed < retryDelay_ ? retryDelay_ - elapsed : 0;
  log_.info(
      "MQTT status state=%s wifi=%s wifi_status=%d broker_configured=%s "
      "attempts=%lu backoff_ms=%lu retry_in_ms=%lu uptime_ms=%lu\n",
      state, wifiStatus == WL_CONNECTED ? "connected" : "disconnected", wifiStatus,
      cfg_.broker[0] ? "yes" : "no", static_cast<unsigned long>(attemptCount_),
      static_cast<unsigned long>(retryScheduled_ ? retryDelay_ : 0),
      static_cast<unsigned long>(remaining), static_cast<unsigned long>(now));
}

void MqttPublisher::update(uint32_t now) {
  logStatus(now);
  // The client is the connection truth; state_ only controls transitions/timers.
  if (client_.connected()) {
    disconnectPending_ = false;
    if (state_ != State::Connected) {
      state_ = State::Connected;
      retryScheduled_ = false;
      nextRetryDelay_ = INITIAL_RETRY_MS;
      log_.info(
          "MQTT state=connected broker=%s:%d attempt=%lu backoff_reset_ms=%lu uptime_ms=%lu\n",
          cfg_.broker, cfg_.port, static_cast<unsigned long>(attemptCount_),
          static_cast<unsigned long>(INITIAL_RETRY_MS), static_cast<unsigned long>(now));
      if (cfg_.listenerTopic[0] && !client_.subscribe(cfg_.listenerTopic, 0)) {
        log_.warn("Failed to subscribe to MQTT topic %s\n", cfg_.listenerTopic);
      }
    }
    return;
  }

  if (disconnectPending_) {
    disconnectPending_ = false;
    // Ignore duplicate notifications for an already scheduled failure.
    if (state_ != State::Disconnected || !retryScheduled_) {
      scheduleRetry(now, translateDisconnectReason(disconnectReason_));
    }
  } else if (state_ == State::Connected) {
    scheduleRetry(now, "connection lost");
  }

  if (state_ == State::Connecting) {
    if (uint32_t(now - attemptStartedAt_) >= CONNECT_TIMEOUT_MS) {
      client_.disconnect(true);
      disconnectPending_ = false;
      scheduleRetry(now, "connection timed out");
    }
    return;
  }

  if (!cfg_.broker[0] || WiFi.status() != WL_CONNECTED) return;
  if (retryScheduled_ && uint32_t(now - retryStartedAt_) < retryDelay_) return;

  client_.setServer(cfg_.broker, cfg_.port);
  client_.setCredentials(cfg_.username[0] ? cfg_.username : nullptr,
                         cfg_.username[0] ? cfg_.password : nullptr);
  state_ = State::Connecting;
  attemptStartedAt_ = now;
  ++attemptCount_;
  const uint32_t previousBackoff = retryScheduled_ ? retryDelay_ : 0;
  retryScheduled_ = false;
  log_.info("MQTT state=connecting broker=%s:%d attempt=%lu backoff_ms=%lu uptime_ms=%lu\n",
            cfg_.broker, cfg_.port, static_cast<unsigned long>(attemptCount_),
            static_cast<unsigned long>(previousBackoff), static_cast<unsigned long>(now));
  // Both DNS lookup and TCP/MQTT handshakes are asynchronous on ESP8266.
  client_.connect();
}

bool MqttPublisher::publish(const String& payload) {
  // Publishing never attempts a connection or waits for recovery.
  if (!client_.connected()) return false;

  // The async library copies the whole packet. Keep headroom for WiFi/HTTP
  // rather than allowing buffered replay to consume the ESP8266's small heap.
  const size_t packetBytes = payload.length() + strlen(cfg_.producerTopic) + 128;
  if (ESP.getFreeHeap() < packetBytes + 8192) {
    log_.warn("Not enough memory to queue MQTT message on topic %s\n", cfg_.producerTopic);
    return false;
  }
  if (!client_.publish(cfg_.producerTopic, 0, false, payload.c_str(), payload.length())) {
    log_.warn("Failed to queue MQTT message on topic %s\n", cfg_.producerTopic);
    return false;
  }
  log_.debug("MQTT message queued (%lu bytes)\n", static_cast<unsigned long>(payload.length()));
  return true;
}

const char* MqttPublisher::translateDisconnectReason(AsyncMqttClientDisconnectReason reason) const {
  switch (reason) {
    case AsyncMqttClientDisconnectReason::TCP_DISCONNECTED:
      return "network/DNS/TCP connection lost or failed";
    case AsyncMqttClientDisconnectReason::MQTT_UNACCEPTABLE_PROTOCOL_VERSION:
      return "unacceptable MQTT protocol version";
    case AsyncMqttClientDisconnectReason::MQTT_IDENTIFIER_REJECTED:
      return "client identifier rejected";
    case AsyncMqttClientDisconnectReason::MQTT_SERVER_UNAVAILABLE:
      return "MQTT server unavailable";
    case AsyncMqttClientDisconnectReason::MQTT_MALFORMED_CREDENTIALS:
      return "bad username or password";
    case AsyncMqttClientDisconnectReason::MQTT_NOT_AUTHORIZED:
      return "not authorized";
    case AsyncMqttClientDisconnectReason::ESP8266_NOT_ENOUGH_SPACE:
      return "not enough ESP8266 memory";
    case AsyncMqttClientDisconnectReason::TLS_BAD_FINGERPRINT:
      return "TLS fingerprint mismatch";
    default:
      return "unknown MQTT disconnect reason";
  }
}
