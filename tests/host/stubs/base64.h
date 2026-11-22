#pragma once
#include <Arduino.h>

class base64 {
 public:
  static String encode(const String& input) {
    static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < input.length(); i += 3) {
      const uint32_t a = static_cast<uint8_t>(input[i]);
      const uint32_t b = i + 1 < input.length() ? static_cast<uint8_t>(input[i + 1]) : 0;
      const uint32_t c = i + 2 < input.length() ? static_cast<uint8_t>(input[i + 2]) : 0;
      const uint32_t bits = (a << 16) | (b << 8) | c;
      out += chars[(bits >> 18) & 63];
      out += chars[(bits >> 12) & 63];
      out += i + 1 < input.length() ? chars[(bits >> 6) & 63] : '=';
      out += i + 2 < input.length() ? chars[bits & 63] : '=';
    }
    return String(out);
  }
};
