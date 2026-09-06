#pragma once
#include <Arduino.h>
#include <stdarg.h>

enum LogLevel : uint8_t { LOG_ERROR=0, LOG_WARN=1, LOG_INFO=2, LOG_DEBUG=3 };
class Logger {
public:
  void setLogLevel(int level) { level_ = constrain(level, 0, 3); }
  int getLogLevel() const { return level_; }
  void debug(const char* fmt, ...); void info(const char* fmt, ...);
  void warn(const char* fmt, ...); void error(const char* fmt, ...);
private:
  int level_ = LOG_DEBUG;
  void vlog(int level, const char* fmt, va_list ap);
};
