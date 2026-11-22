#pragma once
#include "../model/Measurement.h"

// References keep metadata current without passing configuration or networking
// into the serializer. Referenced objects must outlive the serializer.
struct MeasurementJsonMetadata {
  const String& deviceId;
  const char* stationName;
  const bool& temperature;
  const bool& pressure;
  const bool& humidity;
  const bool& gasResistance;
};

class MeasurementJsonSerializer {
 public:
  static constexpr size_t MAX_PAYLOAD_BYTES = 1024;
  explicit MeasurementJsonSerializer(const MeasurementJsonMetadata& metadata)
      : metadata_(metadata) {}
  // Clears output on every failure; callers must not publish or buffer it.
  bool serialize(const Measurement& measurement, String& output) const;

 private:
  const MeasurementJsonMetadata metadata_;
};
