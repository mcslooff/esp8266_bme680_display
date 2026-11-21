#pragma once
#include <ESP8266WiFi.h>

#define HTTPC_ERROR_CONNECTION_FAILED -1
#define HTTPC_ERROR_READ_TIMEOUT -11

class HTTPClient {
 public:
  static int calls, result;
  static std::string payload;
  bool begin(WiFiClient&, const char*) { return true; }
  void setTimeout(uint16_t) {}
  void addHeader(const char*, const char*) {}
  void setAuthorization(const char*, const char*) {}
  int POST(const String& body) {
    ++calls;
    payload = body.c_str();
    return result;
  }
  String getString() { return ""; }
  void end() {}
};
