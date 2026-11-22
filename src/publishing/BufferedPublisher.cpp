#include "BufferedPublisher.h"

namespace {
constexpr char BUFFER_FILE[] = "buffer.json";
}

bool BufferedPublisher::append(const String& body) {
  File file = fs_.open(BUFFER_FILE, "a");
  if (!file || file.size() + body.length() + 2 > MAX_BUFFER_BYTES) {
    log_.error("Cannot buffer measurement: filesystem unavailable or buffer full\n");
    return false;
  }
  const size_t previousSize = file.size();
  if (file.println(body) != body.length() + 2) {
    if (!file.truncate(previousSize)) log_.error("Failed to roll back incomplete buffer write\n");
    log_.error("Cannot buffer measurement: incomplete filesystem write\n");
    return false;
  }
  return true;
}

void BufferedPublisher::retryLater(uint32_t now) {
  retryWaiting_ = true;
  retryAt_ = now;
  log_.warn("Buffered publication paused; retry in %lu ms (acknowledged offset=%lu)\n",
            static_cast<unsigned long>(RETRY_MS), static_cast<unsigned long>(cursor_));
}

bool BufferedPublisher::publish(const Measurement& measurement, uint32_t now) {
  if (!cfg_.publishHttp && !cfg_.publishMqtt) return true;
  String body;
  if (!serializer_.serialize(measurement, body)) {
    log_.error("Measurement serialization failed; not published or buffered\n");
    return false;
  }
  if (active_ || pending_.length() || fs_.exists(BUFFER_FILE)) return append(body);

  const PublicationResult result = publisher_.publishBufferedJson(body, now);
  if (result == PublicationResult::Succeeded) return true;
  if (result == PublicationResult::Pending) {
    pending_ = body;
    active_ = true;
    replay_ = false;
    return true;
  }
  const bool saved = append(body);
  retryLater(now);
  return saved;
}

void BufferedPublisher::replaySucceeded(uint32_t now) {
  cursor_ = readPosition_;
  ++replayed_;
  if (replayed_ == 1 || uint32_t(now - progressAt_) >= 30000) {
    progressAt_ = now;
    log_.info("Buffered replay: %lu records published (offset=%lu)\n",
              static_cast<unsigned long>(replayed_), static_cast<unsigned long>(cursor_));
  }
}

void BufferedPublisher::update(uint32_t now) {
  publisher_.update(now);
  if (active_) {
    const PublicationResult result = publisher_.result();
    if (result == PublicationResult::Pending) return;
    if (result == PublicationResult::Succeeded) {
      if (replay_) replaySucceeded(now);
    } else {
      if (!replay_ && !append(pending_))
        log_.error("Failed HTTP measurement could not be persisted\n");
      readPosition_ = cursor_;
      retryLater(now);
    }
    active_ = false;
    pending_ = "";
    return;  // At most one completion or replay step per iteration.
  }
  if (!cfg_.publishHttp && !cfg_.publishMqtt) return;
  if (retryWaiting_ && uint32_t(now - retryAt_) < RETRY_MS) return;
  retryWaiting_ = false;
  if (stepped_ && uint32_t(now - steppedAt_) < STEP_MS) return;
  stepped_ = true;
  steppedAt_ = now;
  if (!fs_.exists(BUFFER_FILE)) return;

  File file = fs_.open(BUFFER_FILE, "r");
  if (!file || !file.seek(readPosition_, SeekSet)) {
    retryLater(now);
    return;
  }
  bool recordReady = false;
  for (size_t i = 0; i < READ_BYTES_PER_STEP; ++i) {
    const int c = file.read();  // File.read(), not Stream's blocking readStringUntil().
    if (c < 0) {
      recordReady = pending_.length() != 0;
      break;
    }
    ++readPosition_;
    if (c == '\n') {
      recordReady = true;
      break;
    }
    if (pending_.length() >= MAX_RECORD_BYTES) {
      // Do not discard an oversized/corrupt record or exhaust the small heap.
      pending_ = "";
      readPosition_ = cursor_;
      log_.warn("Buffered record exceeds replay memory limit; original retained\n");
      retryLater(now);
      return;
    }
    pending_ += static_cast<char>(c);
  }
  const bool atEnd = readPosition_ == file.size();
  file.close();
  if (!recordReady) {
    if (atEnd && !pending_.length()) {
      if (!fs_.remove(BUFFER_FILE)) {
        retryLater(now);
        return;
      }
      log_.info("Buffered replay drained: %lu records published\n",
                static_cast<unsigned long>(replayed_));
      cursor_ = readPosition_ = replayed_ = 0;
    }
    return;
  }

  pending_.trim();
  if (!pending_.length()) {
    cursor_ = readPosition_;
    return;
  }
  const PublicationResult result = publisher_.publishBufferedJson(pending_, now);
  if (result == PublicationResult::Pending) {
    active_ = true;
    replay_ = true;
    return;
  }
  if (result == PublicationResult::Succeeded) {
    replaySucceeded(now);
  } else {
    readPosition_ = cursor_;
    retryLater(now);
  }
  pending_ = "";
}
