#pragma once

#include <Arduino.h>

enum BugSeverity : uint8_t {
  BUGLOG_INFO = 0,
  BUGLOG_WARN = 1,
  BUGLOG_ERROR = 2,
  BUGLOG_FATAL = 3,
};

bool buglog_begin();
void buglog_tick();
bool buglog_flush(size_t maxEntries = 1);
bool log_event(BugSeverity severity, uint16_t code, const char *msg = nullptr);
inline bool log_event(BugSeverity severity, uint16_t code, const String &msg) {
  return log_event(severity, code, msg.c_str());
}
void log_dump(Stream &out);
String buglog_dump_string();
bool log_clear();
const char *buglog_severity_name(BugSeverity severity);
