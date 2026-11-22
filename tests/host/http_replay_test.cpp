#include "PublishingFixture.h"
#include <limits>

namespace {
void httpRequestAndResponse() {
  Fixture f;
  std::strcpy(f.cfg.httpPost.url, "http://collector.test:8080/measurements?q=1#ignored");
  std::strcpy(f.cfg.httpPost.username, "user");
  std::strcpy(f.cfg.httpPost.password, "password");
  const String body(std::string(1300, 'x'));
  assert(f.http.publish(body, 0) == PublicationResult::Pending);
  assert(f.httpSocket.written.empty());  // No wait for TCP/DNS completion.
  f.httpSocket.completeConnect();
  f.httpSocket.writeLimit = 71;
  for (uint32_t now = 1; now < 40; ++now) f.http.update(now);
  assert(f.httpSocket.host == "collector.test" && f.httpSocket.port == 8080);
  assert(f.httpSocket.written.find("POST /measurements?q=1 HTTP/1.1\r\n") == 0);
  assert(f.httpSocket.written.find("Host: collector.test:8080\r\n") != std::string::npos);
  assert(f.httpSocket.written.find("Content-Length: 1300\r\n") != std::string::npos);
  assert(f.httpSocket.written.find("Authorization: Basic dXNlcjpwYXNzd29yZA==\r\n") !=
         std::string::npos);
  assert(f.httpBody() == body.c_str());
  assert(f.http.publish("must-not-replace-active-body", 40) == PublicationResult::Failed);
  assert(f.httpBody() == body.c_str());
  f.httpSocket.receive("HTTP/1.");
  f.httpSocket.receive("1 201 Created\r\nContent-Length: 0\r\n");
  assert(f.http.result() == PublicationResult::Pending);
  f.httpSocket.receive("\r\nignored response body");
  assert(f.http.result() == PublicationResult::Succeeded);
  f.http.update(41);
  assert(Serial.output.find("password") == std::string::npos);
  assert(Serial.output.find("dXNlcjpwYXNzd29yZA") == std::string::npos);

  Fixture urlAuth;
  std::strcpy(urlAuth.cfg.httpPost.url, "http://user:password@collector.test");
  assert(urlAuth.http.publish("{}", 0) == PublicationResult::Pending);
  urlAuth.sendHttp();
  assert(urlAuth.httpSocket.host == "collector.test");
  assert(urlAuth.httpSocket.written.find("Basic dXNlcjpwYXNzd29yZA==") != std::string::npos);
  urlAuth.httpSocket.receive("HTTP/1.1 401 Unauthorized\r\n\r\n");
  urlAuth.http.update(50);
  std::strcpy(urlAuth.cfg.httpPost.username, "override");
  std::strcpy(urlAuth.cfg.httpPost.password, "secret");
  assert(urlAuth.http.publish("{}", 5050) == PublicationResult::Pending);
  urlAuth.sendHttp(5051);
  assert(urlAuth.httpSocket.written.find("Basic dXNlcjpwYXNzd29yZA==") == std::string::npos);
  assert(Serial.output.find("password") == std::string::npos);
}

void httpFailureTimeoutAndCooldown() {
  Fixture f;
  const uint32_t start = UINT32_MAX - 1000;
  assert(f.http.publish("{}", start) == PublicationResult::Pending);
  f.httpSocket.completeConnect();
  f.httpSocket.capacity = 0;  // TCP backpressure must not busy-wait either.
  f.http.update(uint32_t(start + 4999));
  assert(f.http.result() == PublicationResult::Pending);
  f.http.update(uint32_t(start + 5000));
  assert(f.http.result() == PublicationResult::Failed);
  assert(Serial.output.find("request timed out") != std::string::npos);
  assert(f.httpSocket.closeCalls == 1);
  // Late callbacks cannot turn a timed-out publication into a success.
  f.httpSocket.completeConnect();
  f.httpSocket.receive("HTTP/1.1 200 OK\r\n\r\n");
  assert(f.http.result() == PublicationResult::Failed);
  assert(f.http.publish("{}", uint32_t(start + 9999)) == PublicationResult::Failed);
  assert(f.httpSocket.connectCalls == 1);
  f.httpSocket.connectResult = false;
  assert(f.http.publish("{}", uint32_t(start + 10000)) == PublicationResult::Failed);
  assert(f.httpSocket.connectCalls == 2);
  f.httpSocket.connectResult = true;
  assert(f.http.publish("{}", uint32_t(start + 15000)) == PublicationResult::Pending);
  f.httpSocket.fail();
  f.http.update(uint32_t(start + 15001));
  assert(f.http.result() == PublicationResult::Failed);
  assert(Serial.output.find("DNS/TCP") != std::string::npos);

  Fixture sendFailed;
  sendFailed.http.publish("{}", 0);
  sendFailed.httpSocket.completeConnect();
  sendFailed.httpSocket.sendResult = false;
  sendFailed.http.update(1);
  assert(sendFailed.http.result() == PublicationResult::Failed);
  assert(Serial.output.find("TCP send failed") != std::string::npos);
}

void httpResponseFailuresAndInterim() {
  const std::string responses[] = {
      "HTTP/1.1 503 Unavailable\r\n\r\n", "not HTTP\r\n\r\n", "HTTP/1.1 2000 Invalid\r\n\r\n",
      "HTTP/1.1 200 " + std::string(100, 'x') + "\r\n\r\n",
      "HTTP/1.1 200 OK\r\nX-Large: " + std::string(4100, 'x') + "\r\n\r\n"};
  for (const std::string& response : responses) {
    Fixture f;
    assert(f.http.publish("{}", 0) == PublicationResult::Pending);
    f.sendHttp();
    f.httpSocket.receive(response);
    f.http.update(50);
    assert(f.http.result() == PublicationResult::Failed);
  }
  Fixture interim;
  assert(interim.http.publish("{}", 0) == PublicationResult::Pending);
  interim.sendHttp();
  interim.httpSocket.receive("HTTP/1.1 100 Continue\r\n\r\n");
  assert(interim.http.result() == PublicationResult::Pending);
  interim.httpSocket.receive("HTTP/1.0 204 No Content\r\n\r\n");
  assert(interim.http.result() == PublicationResult::Succeeded);
  interim.http.update(50);

  Fixture closed;
  closed.http.publish("{}", 0);
  closed.httpSocket.close(true);
  closed.http.update(1);
  assert(closed.http.result() == PublicationResult::Failed);

  Fixture premature;
  premature.http.publish(String(std::string(2000, 'x')), 0);
  premature.httpSocket.receive("HTTP/1.1 200 OK\r\n\r\n");
  assert(premature.http.result() == PublicationResult::Failed);
}

void httpConfigurationAndResourceFailures() {
  for (const char* url :
       {"", "https://collector.test/", "http://", "http://:80/", "http://collector.test:/",
        "http://collector.test:0/", "http://collector.test:65536/", "http://collector.test:x/",
        "http://col lector/", "http://[::1]/", "http://collector.test/a b",
        "http://collector.test/\r\nInjected: yes"}) {
    Fixture f;
    std::strcpy(f.cfg.httpPost.url, url);
    assert(f.http.publish("{}", 0) == PublicationResult::Failed);
    assert(f.httpSocket.connectCalls == 0);
  }
  Fixture query;
  std::strcpy(query.cfg.httpPost.url, "http://collector.test?q=1");
  assert(query.http.publish("{}", 0) == PublicationResult::Pending);
  query.sendHttp();
  assert(query.httpSocket.written.find("POST /?q=1 HTTP/1.1\r\n") == 0);

  Fixture offline;
  WiFi.connectionStatus = 0;
  assert(offline.http.publish("{}", 0) == PublicationResult::Failed);
  assert(offline.httpSocket.connectCalls == 0);
  Fixture memory;
  ESP.freeHeap = 8192;
  assert(memory.http.publish("{}", 0) == PublicationResult::Failed);
  assert(memory.httpSocket.connectCalls == 0);
}

void pendingLiveFailureIsBuffered() {
  Fixture f;
  f.cfg.measurement.publishHttp = true;
  const std::string expected = f.publisher.json(f.measurement).c_str();
  assert(f.buffered.publish(f.measurement, 0));
  assert(!f.fs.exists("buffer.json"));  // Do not write successful live requests to flash.
  f.buffered.update(4999);
  assert(!f.fs.exists("buffer.json"));
  f.buffered.update(5000);
  assert(f.fs.buffered() == expected + "\r\n");
  const size_t reads = f.fs.readCount;
  for (uint32_t now = 5001; now < 10000; ++now) f.buffered.update(now);
  assert(f.fs.readCount == reads && f.httpSocket.connectCalls == 1);
  f.buffered.update(10000);  // One bounded read step, not a whole-file replay.
  assert(f.fs.readCount - reads <= 128);

  Fixture success;
  success.cfg.measurement.publishHttp = true;
  assert(success.buffered.publish(success.measurement, 0));
  success.completeHttp();
  success.buffered.update(40);
  assert(!success.fs.exists("buffer.json"));
}

void stalledHttpRetainsBacklogAndAcceptsNewSamples() {
  Fixture f;
  f.cfg.measurement.publishHttp = true;
  const std::string original = "{\"timestamp\":1}\r\n{\"timestamp\":2}\r\n";
  f.fs.seed(original);
  f.buffered.update(0);
  assert(f.httpSocket.connectCalls == 1);
  const size_t reads = f.fs.readCount;
  for (uint32_t now = 1; now < 5000; ++now) {
    f.buffered.update(now);
    // Represents other main-loop work: no repeated connection attempts or reads.
    assert(f.httpSocket.connectCalls == 1 && f.fs.readCount == reads);
  }
  assert(f.buffered.publish(f.measurement, 100));
  const std::string retained = f.fs.buffered();
  assert(retained.find(original) == 0);
  f.buffered.update(5000);
  assert(f.fs.buffered() == retained);
  f.buffered.update(9999);
  assert(f.httpSocket.connectCalls == 1);
  f.buffered.update(10000);
  assert(f.httpSocket.connectCalls == 2);
  f.completeHttp(10001);
  f.buffered.update(10040);
  // Delivered prefix remains until the whole file is acknowledged.
  assert(f.fs.buffered() == retained);
  f.buffered.update(10050);
  assert(f.httpSocket.connectCalls == 3);
  f.completeHttp(10051, 503);
  f.buffered.update(10090);
  assert(f.fs.buffered() == retained);  // Non-2xx must not advance/drop the second record.
  f.buffered.update(15090);
  f.completeHttp(15091);
  f.buffered.update(15130);
  // Read and deliver the newly appended measurement in bounded chunks.
  for (uint32_t now = 15140; now < 16000; now += 10) {
    f.buffered.update(now);
    if (f.publisher.result() == PublicationResult::Pending) {
      f.completeHttp(now + 1);
      f.buffered.update(now + 40);
    }
    if (!f.fs.exists("buffer.json")) break;
  }
  assert(!f.fs.exists("buffer.json"));
}

void replayBudgetMqttAndResetSafety() {
  Fixture f;
  f.cfg.measurement.publishMqtt = true;
  f.connect();
  const std::string body(1024, 'x');
  f.fs.seed(body + "\r\nsecond\r\n");
  for (uint32_t now = 0; now < 200; now += 10) {
    const size_t before = f.fs.readCount;
    const int published = f.client.publishCalls;
    f.buffered.update(now);
    assert(f.fs.readCount - before <= 128);
    assert(f.client.publishCalls - published <= 1);
  }
  assert(!f.fs.exists("buffer.json"));
  assert(f.client.publishCalls == 2);

  Fixture restart;
  restart.cfg.measurement.publishMqtt = true;
  restart.connect();
  restart.fs.seed("one\r\ntwo\r\n");
  restart.buffered.update(0);
  assert(restart.fs.buffered() == "one\r\ntwo\r\n");
  BufferedPublisher afterReset(restart.publisher, restart.fs, restart.cfg.measurement, restart.log);
  afterReset.update(10);
  assert(restart.client.payload == "one");  // Replay duplicates, never erase unacknowledged suffix.
  afterReset.update(20);
  assert(restart.client.payload == "two");
  afterReset.update(30);
  assert(!restart.fs.exists("buffer.json"));
}

void replayFilesystemEdgesAndCombinedMode() {
  Fixture f;
  f.cfg.measurement.publishMqtt = true;
  f.fs.failOpen = true;
  assert(!f.buffered.publish(f.measurement, 0));
  f.fs.failOpen = false;
  f.fs.seed(std::string(2000000, 'x'));
  assert(!f.buffered.publish(f.measurement, 1));
  f.fs.seed("retained\r\n");
  f.fs.writeLimit = 3;
  assert(!f.buffered.publish(f.measurement, 2));
  assert(f.fs.buffered() == "retained\r\n");  // Roll back a partial append.
  f.fs.writeLimit = std::numeric_limits<size_t>::max();

  Fixture blank;
  blank.cfg.measurement.publishMqtt = true;
  blank.connect();
  blank.fs.seed("\r\nlast-without-newline");
  for (uint32_t now = 0; now < 100; now += 10) blank.buffered.update(now);
  assert(!blank.fs.exists("buffer.json") && blank.client.payload == "last-without-newline");
  blank.cfg.measurement.publishMqtt = false;
  blank.fs.seed("keep\r\n");
  blank.buffered.update(100);
  assert(blank.fs.buffered() == "keep\r\n");
  assert(blank.buffered.publish(blank.measurement, 101));

  Fixture oversized;
  oversized.cfg.measurement.publishMqtt = true;
  oversized.fs.seed(std::string(4200, 'x') + "\r\n");
  for (uint32_t now = 0; now < 500; now += 10) oversized.buffered.update(now);
  assert(oversized.fs.buffered().size() == 4202);
  assert(Serial.output.find("original retained") != std::string::npos);

  Fixture combined;
  combined.cfg.measurement.publishMqtt = combined.cfg.measurement.publishHttp = true;
  combined.connect();
  combined.fs.seed("{}\r\n");
  combined.buffered.update(0);
  assert(combined.client.publishCalls == 1);
  combined.buffered.update(5000);  // HTTP times out, but MQTT succeeded.
  combined.buffered.update(5010);
  assert(!combined.fs.exists("buffer.json"));

  Fixture metadata;
  metadata.cfg.measurement.publishMqtt = true;
  metadata.connect();
  metadata.fs.seed("{}\r\n");
  metadata.fs.failOpen = true;
  metadata.buffered.update(0);
  assert(metadata.fs.buffered() == "{}\r\n");
  metadata.fs.failOpen = false;
  metadata.buffered.update(4999);
  assert(metadata.client.publishCalls == 0);
  metadata.buffered.update(5000);
  assert(metadata.client.publishCalls == 1);
  metadata.fs.failRemove = true;
  metadata.buffered.update(5010);
  assert(metadata.fs.exists("buffer.json"));
  metadata.fs.failRemove = false;
  metadata.buffered.update(10010);
  assert(!metadata.fs.exists("buffer.json"));

  Fixture rollover;
  rollover.cfg.measurement.publishMqtt = true;
  rollover.fs.seed("{}\r\n");
  const uint32_t start = UINT32_MAX - 1000;
  rollover.buffered.update(start);
  const size_t reads = rollover.fs.readCount;
  rollover.client.completeConnect();
  rollover.buffered.update(uint32_t(start + 4999));
  assert(rollover.fs.readCount == reads);
  rollover.buffered.update(uint32_t(start + 5000));
  rollover.buffered.update(uint32_t(start + 5010));
  assert(!rollover.fs.exists("buffer.json"));
}
}  // namespace

void httpAndReplayTests() {
  httpRequestAndResponse();
  httpFailureTimeoutAndCooldown();
  httpResponseFailuresAndInterim();
  httpConfigurationAndResourceFailures();
  pendingLiveFailureIsBuffered();
  stalledHttpRetainsBacklogAndAcceptsNewSamples();
  replayBudgetMqttAndResetSafety();
  replayFilesystemEdgesAndCombinedMode();
}
