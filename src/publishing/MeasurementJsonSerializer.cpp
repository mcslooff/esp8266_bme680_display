#include "MeasurementJsonSerializer.h"
#include <stdarg.h>
#include <stdio.h>
#include "../system/Version.h"

namespace {
// One bounded scratch buffer, no growing concatenation chain. A partial scratch
// buffer is never exposed to the caller.
class JsonWriter {
 public:
  JsonWriter(char* data, size_t capacity) : data_(data), capacity_(capacity) {}
  void append(const char* format, ...) {
    if (!ok_) return;
    va_list args;
    va_start(args, format);
    const int written = vsnprintf(data_ + size_, capacity_ - size_, format, args);
    va_end(args);
    if (written < 0 || static_cast<size_t>(written) >= capacity_ - size_) {
      ok_ = false;
      return;
    }
    size_ += static_cast<size_t>(written);
  }
  void quoted(const char* text) {
    if (!text) {
      ok_ = false;
      return;
    }
    append("\"");
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p && ok_; ++p) {
      switch (*p) {
        case '"':
          append("\\\"");
          break;
        case '\\':
          append("\\\\");
          break;
        case '\n':
          append("\\n");
          break;
        case '\r':
          append("\\r");
          break;
        case '\t':
          append("\\t");
          break;
        default:
          if (*p < 0x20)
            append("\\u%04x", static_cast<unsigned int>(*p));
          else
            append("%c", *p);
      }
    }
    append("\"");
  }
  void number(float value, unsigned char decimals) {
    if (!ok_) return;
    // Keep Arduino's existing rounding (including half-way/negative-zero cases).
    // Finite float values with <=3 decimals need fewer than 50 characters.
    const String text(value, decimals);
    if (!text.length()) {
      ok_ = false;
      return;
    }
    append("%s", text.c_str());
  }
  bool ok() const { return ok_; }
  size_t size() const { return size_; }

 private:
  char* data_;
  size_t capacity_, size_ = 0;
  bool ok_ = true;
};
}  // namespace

bool MeasurementJsonSerializer::serialize(const Measurement& m, String& output) const {
  output = String();
  // NaN/Infinity are not JSON numbers. Disabled values are not serialized and
  // therefore need not be finite.
  if (!isfinite(m.batteryVoltage) || (metadata_.temperature && !isfinite(m.temperature)) ||
      (metadata_.pressure && !isfinite(m.pressurePa)) ||
      (metadata_.humidity && !isfinite(m.humidity)) ||
      (metadata_.gasResistance && !isfinite(m.gasResistanceOhm)))
    return false;

  char scratch[MAX_PAYLOAD_BYTES + 1];
  JsonWriter json(scratch, sizeof(scratch));
  json.append("{  \"timestamp\": %lu,  \"station\" : {    \"id\": ",
              static_cast<unsigned long>(m.timestamp));
  json.quoted(metadata_.deviceId.c_str());
  json.append(",    \"name\": ");
  json.quoted(metadata_.stationName);
  json.append(",    \"version\": ");
  json.quoted(Version::SOFTWARE_VERSION);
  json.append(",    \"power\": {      \"voltage\": ");
  json.number(m.batteryVoltage, 3);
  json.append("    }  },  \"measurement\": ");
  if (metadata_.temperature || metadata_.pressure || metadata_.humidity ||
      metadata_.gasResistance) {
    json.append("{");
    bool previous = false;
    if (metadata_.temperature) {
      json.append("    \"temperature\": {      \"value\": ");
      json.number(m.temperature, 3);
      json.append(",      \"unit\": \"C\"    }");
      previous = true;
    }
    if (metadata_.pressure) {
      json.append("%s    \"airPressure\": {      \"value\": ", previous ? "," : "");
      json.number(m.pressureHpa(), 3);
      json.append(",      \"unit\": \"hPa\"    }");
      previous = true;
    }
    if (metadata_.humidity) {
      json.append("%s    \"humidity\": {      \"value\": ", previous ? "," : "");
      json.number(m.humidity, 3);
      json.append(",      \"unit\": \"%%\"    }");
      previous = true;
    }
    if (metadata_.gasResistance) {
      // Preserve the legacy value/unit pairing; correcting it is a contract change.
      json.append("%s    \"gasResistance\": {      \"value\": ", previous ? "," : "");
      json.number(m.gasResistanceOhm, 1);
      json.append(",      \"unit\": \"kOhm\"    }");
    }
    json.append("}");
  } else {
    json.append("null");
  }
  json.append("}");
  if (!json.ok() || !output.reserve(json.size()) ||
      !output.concat(scratch, static_cast<unsigned int>(json.size()))) {
    output = String();
    return false;
  }
  return true;
}
