#include "Logger.h"
void Logger::vlog(int level, const char* fmt, va_list ap) {
  if (level > level_) return;
  char buf[384];
  vsnprintf(buf, sizeof(buf), fmt, ap);
  Serial.print(buf);
}
void Logger::debug(const char* f, ...) { va_list a; va_start(a,f); vlog(LOG_DEBUG,f,a); va_end(a); }
void Logger::info(const char* f, ...) { va_list a; va_start(a,f); vlog(LOG_INFO,f,a); va_end(a); }
void Logger::warn(const char* f, ...) { va_list a; va_start(a,f); vlog(LOG_WARN,f,a); va_end(a); }
void Logger::error(const char* f, ...) { va_list a; va_start(a,f); vlog(LOG_ERROR,f,a); va_end(a); }
