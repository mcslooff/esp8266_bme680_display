#include "MqttPublisher.h"

bool MqttPublisher::publish(const String& payload) {
  log_.debug("Checking MQTT connection\n");
  if (!client_.connected()) {
    if (cfg_.username[0]) {
      client_.setUsernamePassword(cfg_.username, cfg_.password);
    }
    if (!client_.connect(cfg_.broker, cfg_.port)) {
      const int code = client_.connectError();
      log_.error(
        "Failed to connect to MQTT broker %s:%d; error code=%d, %s\n",
        cfg_.broker, cfg_.port, code, translateMqttErrorCode(code).c_str()
      );
      return false;
    }
  }

  log_.debug("Beginning MQTT message\n");
  if (!client_.beginMessage(
        cfg_.producerTopic, static_cast<unsigned long>(payload.length()))) {
    log_.error("Failed to begin MQTT message on topic %s\n", cfg_.producerTopic);
    return false;
  }

  const size_t written = client_.print(payload);
  const int result = client_.endMessage();
  if (written != payload.length() || !result) {
    log_.error(
      "Failed to publish MQTT message on topic %s; wrote %lu/%lu bytes, result=%d\n",
      cfg_.producerTopic, static_cast<unsigned long>(written),
      static_cast<unsigned long>(payload.length()), result
    );
    return false;
  }

  log_.debug("MQTT message published\n");
  return true;
}

String MqttPublisher::translateMqttErrorCode(int code) const {
  switch (code) {
    case MQTT_CONNECTION_REFUSED:
      return "Connection refused";
    case MQTT_CONNECTION_TIMEOUT:
      return "Connection timed out";
    case MQTT_SUCCESS:
      return "Success";
    case MQTT_UNACCEPTABLE_PROTOCOL_VERSION:
      return "Unacceptable MQTT protocol version";
    case MQTT_IDENTIFIER_REJECTED:
      return "Client identifier rejected";
    case MQTT_SERVER_UNAVAILABLE:
      return "MQTT server unavailable";
    case MQTT_BAD_USER_NAME_OR_PASSWORD:
      return "Bad username or password";
    case MQTT_NOT_AUTHORIZED:
      return "Not authorized";
    default:
      return String("Unknown MQTT error (") + code + ")";
  }
}
