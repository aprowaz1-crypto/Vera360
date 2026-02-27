/**
 * Vera360 — Xenia Edge
 * Logging implementation (Android logcat)
 */

#include "xenia/base/logging.h"

#if defined(__ANDROID__)
#include <android/log.h>
#endif

#include <cstdio>

namespace xe {

void LogInit() {
  // Could open a file log here in the future
}

void LogShutdown() {
  // Flush / close file log
}

void LogLine(LogLevel level, const char* tag, const std::string& msg) {
#if defined(__ANDROID__)
  android_LogPriority prio;
  switch (level) {
    case LogLevel::kVerbose: prio = ANDROID_LOG_VERBOSE; break;
    case LogLevel::kDebug:   prio = ANDROID_LOG_DEBUG;   break;
    case LogLevel::kInfo:    prio = ANDROID_LOG_INFO;    break;
    case LogLevel::kWarning: prio = ANDROID_LOG_WARN;    break;
    case LogLevel::kError:   prio = ANDROID_LOG_ERROR;   break;
    case LogLevel::kFatal:   prio = ANDROID_LOG_FATAL;   break;
    default:                 prio = ANDROID_LOG_DEFAULT;  break;
  }
  __android_log_write(prio, tag, msg.c_str());
#else
  // Fallback: stderr
  const char* level_str = "?";
  switch (level) {
    case LogLevel::kVerbose: level_str = "V"; break;
    case LogLevel::kDebug:   level_str = "D"; break;
    case LogLevel::kInfo:    level_str = "I"; break;
    case LogLevel::kWarning: level_str = "W"; break;
    case LogLevel::kError:   level_str = "E"; break;
    case LogLevel::kFatal:   level_str = "F"; break;
  }
  fprintf(stderr, "[%s/%s] %s\n", level_str, tag, msg.c_str());
#endif
}

}  // namespace xe
