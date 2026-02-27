/**
 * Vera360 — Xenia Edge
 * Logging subsystem (Android logcat + file backend)
 */
#pragma once

#include <cstdint>
#include <string>
#include <format>

// Forward declarations for fmt-like logging macros
namespace xe {

enum class LogLevel : uint8_t {
  kVerbose = 0,
  kDebug,
  kInfo,
  kWarning,
  kError,
  kFatal,
};

void LogInit();
void LogShutdown();
void LogLine(LogLevel level, const char* tag, const std::string& msg);

}  // namespace xe

// ── Macros ──────────────────────────────────────────────────────────────────
#define XELOGV(fmt, ...) ::xe::LogLine(::xe::LogLevel::kVerbose, "XE", std::format(fmt __VA_OPT__(,) __VA_ARGS__))
#define XELOGD(fmt, ...) ::xe::LogLine(::xe::LogLevel::kDebug,   "XE", std::format(fmt __VA_OPT__(,) __VA_ARGS__))
#define XELOGI(fmt, ...) ::xe::LogLine(::xe::LogLevel::kInfo,    "XE", std::format(fmt __VA_OPT__(,) __VA_ARGS__))
#define XELOGW(fmt, ...) ::xe::LogLine(::xe::LogLevel::kWarning, "XE", std::format(fmt __VA_OPT__(,) __VA_ARGS__))
#define XELOGE(fmt, ...) ::xe::LogLine(::xe::LogLevel::kError,   "XE", std::format(fmt __VA_OPT__(,) __VA_ARGS__))
#define XELOGF(fmt, ...) ::xe::LogLine(::xe::LogLevel::kFatal,   "XE", std::format(fmt __VA_OPT__(,) __VA_ARGS__))
