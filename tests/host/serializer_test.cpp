#include "PublishingFixture.h"
#include <iostream>

void serializationTests() {
  Fixture contract;
  std::strcpy(contract.cfg.station.hostname, "Room \"A\"\\\n\r\t\001");
  // Parsed independently by Python's standard JSON parser in the normal runner.
  std::cout << "SERIALIZER_CONTRACT " << contract.json().c_str() << '\n';
  contract.cfg.measurement.temperature = contract.cfg.measurement.pressure =
      contract.cfg.measurement.humidity = contract.cfg.measurement.gasResistance = false;
  contract.measurement.temperature = contract.measurement.pressurePa =
      contract.measurement.humidity = contract.measurement.gasResistanceOhm = NAN;
  std::cout << "SERIALIZER_DISABLED " << contract.json().c_str() << '\n';
  contract.cfg.measurement.humidity = true;
  contract.measurement.humidity = 50;
  std::cout << "SERIALIZER_SELECTED " << contract.json().c_str() << '\n';

  Fixture rounded;
  rounded.measurement.temperature = 0.0625f;  // Preserve platform String rounding, not printf's.
  const std::string expectedNumber = String(rounded.measurement.temperature, 3).c_str();
  assert(std::string(rounded.json().c_str()).find("\"value\": " + expectedNumber) !=
         std::string::npos);
  rounded.measurement.batteryVoltage = -0.0f;
  const std::string expectedVoltage = String(rounded.measurement.batteryVoltage, 3).c_str();
  assert(std::string(rounded.json().c_str()).find("\"voltage\": " + expectedVoltage) !=
         std::string::npos);

  Fixture failure;
  failure.cfg.measurement.publishHttp = failure.cfg.measurement.publishMqtt = true;
  failure.connect();
  String output("stale payload");
  failure.measurement.humidity = NAN;
  assert(!failure.serializer.serialize(failure.measurement, output) && output.length() == 0);
  assert(failure.publisher.publish(failure.measurement, 0) == PublicationResult::Failed);
  assert(!failure.buffered.publish(failure.measurement, 0));
  assert(failure.client.publishCalls == 0 && failure.httpSocket.connectCalls == 0);
  assert(!failure.fs.exists("buffer.json"));
  failure.measurement.humidity = 50;
  failure.measurement.batteryVoltage = INFINITY;
  assert(!failure.serializer.serialize(failure.measurement, output));
  failure.measurement.batteryVoltage = 3.5;
  failure.deviceId = String(std::string(2000, 'x'));
  assert(!failure.serializer.serialize(failure.measurement, output) && output.length() == 0);
  assert(failure.publisher.publish(failure.measurement, 1) == PublicationResult::Failed);
  failure.deviceId = "123456789ABC";
  String::failNextReserve = true;
  output = "stale payload";
  assert(!failure.serializer.serialize(failure.measurement, output) && output.length() == 0);
  String::failNextConcat = true;
  assert(!failure.serializer.serialize(failure.measurement, output) && output.length() == 0);
  String::failNextReserve = true;
  assert(!failure.buffered.publish(failure.measurement, 2));
  assert(failure.client.publishCalls == 0 && failure.httpSocket.connectCalls == 0);
  assert(!failure.fs.exists("buffer.json"));

  Fixture inFlight;
  inFlight.cfg.measurement.publishHttp = true;
  assert(inFlight.publisher.publish(inFlight.measurement, 0) == PublicationResult::Pending);
  inFlight.measurement.temperature = NAN;
  assert(inFlight.publisher.publish(inFlight.measurement, 1) == PublicationResult::Failed);
  assert(inFlight.publisher.result() == PublicationResult::Pending);
  assert(inFlight.httpSocket.connectCalls == 1);
  inFlight.completeHttp(2);
  assert(inFlight.publisher.result() == PublicationResult::Succeeded);

  Fixture queued;
  queued.cfg.measurement.publishMqtt = true;
  queued.connect();
  const size_t length = queued.json().length();
  String::reserveSizes.clear();
  assert(queued.buffered.publish(queued.measurement, 0));
  assert(std::count(String::reserveSizes.begin(), String::reserveSizes.end(), length) == 1);
  queued.fs.seed(std::string(queued.json().c_str()) + "\r\n");
  String::reserveSizes.clear();
  for (uint32_t now = 0; now < 200; now += 10) queued.buffered.update(now);
  assert(!queued.fs.exists("buffer.json"));
  assert(String::reserveSizes.empty());  // Existing buffered JSON is never reserialized.
}
