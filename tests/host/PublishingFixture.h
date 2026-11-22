#pragma once
#include <cassert>
#include <cstring>
#include <ESP8266WiFi.h>
#include "../../src/publishing/BufferedPublisher.h"

struct Fixture {
  AppConfig cfg;
  Logger log;
  AsyncMqttClient client;
  AsyncClient httpSocket;
  String deviceId{"123456789ABC"};
  MeasurementJsonSerializer serializer{{deviceId, cfg.station.hostname, cfg.measurement.temperature,
                                        cfg.measurement.pressure, cfg.measurement.humidity,
                                        cfg.measurement.gasResistance}};
  MqttPublisher mqtt{client, cfg.mqtt, log};
  HttpPublisher http{httpSocket, cfg.httpPost, log};
  Publisher publisher{cfg.measurement, serializer, mqtt, http};
  FS fs;
  BufferedPublisher buffered{publisher, serializer, fs, cfg.measurement, log};
  Measurement measurement;

  Fixture() {
    Serial.output.clear();
    WiFi.connectionStatus = WL_CONNECTED;
    ESP.freeHeap = 40000;
    String::failNextReserve = String::failNextConcat = false;
    String::reserveSizes.clear();
    std::strcpy(cfg.mqtt.broker, "broker.test");
    std::strcpy(cfg.mqtt.producerTopic, "measurements");
    std::strcpy(cfg.httpPost.url, "http://collector.test");
    measurement.timestamp = 123456;
    measurement.temperature = 21.5;
    measurement.humidity = 50;
    measurement.pressurePa = 101325;
    measurement.gasResistanceOhm = 12345;
    measurement.batteryVoltage = 3.5;
  }
  String json() const {
    String body;
    assert(serializer.serialize(measurement, body));
    return body;
  }
  void connect(uint32_t now = 0) {
    mqtt.update(now);
    assert(client.attemptPending && !client.connected());
    client.completeConnect();
    mqtt.update(now + 1);
  }
  void sendHttp(uint32_t now = 1) {
    httpSocket.completeConnect();
    // Includes partial writes without assuming that an arbitrary payload fits one segment.
    for (uint32_t i = 0; i < 30; ++i) publisher.update(now + i);
  }
  void completeHttp(uint32_t now = 1, int status = 200) {
    sendHttp(now);
    httpSocket.receive("HTTP/1.1 " + std::to_string(status) +
                       " Result\r\nContent-Length: 0\r\n\r\n");
    publisher.update(now + 30);
  }
  std::string httpBody() const {
    const size_t boundary = httpSocket.written.find("\r\n\r\n");
    return boundary == std::string::npos ? "" : httpSocket.written.substr(boundary + 4);
  }
};
