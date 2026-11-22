#pragma once
#include <FS.h>
#include "Publisher.h"

// Preserve the existing newline-delimited buffer file. A RAM-only replay cursor
// advances only after success; a reset may replay a delivered prefix (duplicates),
// but never requires deleting/replacing the original file before it is drained.
class BufferedPublisher {
 public:
  BufferedPublisher(Publisher& publisher, const MeasurementJsonSerializer& serializer, FS& fs,
                    const MeasurementConfiguration& cfg, Logger& log)
      : publisher_(publisher), serializer_(serializer), fs_(fs), cfg_(cfg), log_(log) {}
  bool publish(const Measurement& measurement, uint32_t now);
  void update(uint32_t now);

 private:
  static constexpr size_t MAX_BUFFER_BYTES = 2000000;
  static constexpr size_t MAX_RECORD_BYTES = 4096;
  static constexpr size_t READ_BYTES_PER_STEP = 128;
  static constexpr uint32_t STEP_MS = 10;
  static constexpr uint32_t RETRY_MS = 5000;
  Publisher& publisher_;
  const MeasurementJsonSerializer& serializer_;
  FS& fs_;
  const MeasurementConfiguration& cfg_;
  Logger& log_;
  String pending_;
  size_t cursor_ = 0, readPosition_ = 0;
  uint32_t retryAt_ = 0, steppedAt_ = 0, replayed_ = 0, progressAt_ = 0;
  bool retryWaiting_ = false, stepped_ = false, active_ = false, replay_ = false;

  bool append(const String& body);
  void retryLater(uint32_t now);
  void replaySucceeded(uint32_t now);
};
