/**
 * Vera360 — Xenia Edge
 * Logging subsystem (Android logcat + file backend)
 *
 * Uses a custom xe::fmt() that supports {}-style placeholders.
 * Compatible with Android NDK (no std::format dependency).
 */
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <sstream>
#include <type_traits>

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

// ── Lightweight {}-style formatter (no std::format needed) ──────────────────

namespace detail {

// Convert a single value to string based on format spec
inline void AppendArg(std::string& out, const char* spec_begin,
                      const char* spec_end, int32_t val) {
  char buf[64];
  // Check for hex format specifier
  std::string spec(spec_begin, spec_end);
  if (spec.find('X') != std::string::npos || spec.find('x') != std::string::npos) {
    // Extract width
    int width = 0;
    bool zero_pad = false;
    for (auto c : spec) {
      if (c == '0' && width == 0) zero_pad = true;
      if (c >= '0' && c <= '9') width = width * 10 + (c - '0');
    }
    bool upper = spec.find('X') != std::string::npos;
    if (zero_pad && width > 0) {
      snprintf(buf, sizeof(buf), upper ? "%0*X" : "%0*x", width, static_cast<uint32_t>(val));
    } else {
      snprintf(buf, sizeof(buf), upper ? "%X" : "%x", static_cast<uint32_t>(val));
    }
  } else {
    snprintf(buf, sizeof(buf), "%d", val);
  }
  out += buf;
}

inline void AppendArg(std::string& out, const char* spec_begin,
                      const char* spec_end, uint32_t val) {
  char buf[64];
  std::string spec(spec_begin, spec_end);
  if (spec.find('X') != std::string::npos || spec.find('x') != std::string::npos) {
    int width = 0;
    bool zero_pad = false;
    for (auto c : spec) {
      if (c == '0' && width == 0) zero_pad = true;
      if (c >= '0' && c <= '9') width = width * 10 + (c - '0');
    }
    bool upper = spec.find('X') != std::string::npos;
    if (zero_pad && width > 0) {
      snprintf(buf, sizeof(buf), upper ? "%0*X" : "%0*x", width, val);
    } else {
      snprintf(buf, sizeof(buf), upper ? "%X" : "%x", val);
    }
  } else {
    snprintf(buf, sizeof(buf), "%u", val);
  }
  out += buf;
}

inline void AppendArg(std::string& out, const char* spec_begin,
                      const char* spec_end, int64_t val) {
  char buf[64];
  std::string spec(spec_begin, spec_end);
  if (spec.find('X') != std::string::npos || spec.find('x') != std::string::npos) {
    int width = 0;
    bool zero_pad = false;
    for (auto c : spec) {
      if (c == '0' && width == 0) zero_pad = true;
      if (c >= '0' && c <= '9') width = width * 10 + (c - '0');
    }
    bool upper = spec.find('X') != std::string::npos;
    if (zero_pad && width > 0) {
      snprintf(buf, sizeof(buf), upper ? "%0*llX" : "%0*llx", width,
               static_cast<unsigned long long>(val));
    } else {
      snprintf(buf, sizeof(buf), upper ? "%llX" : "%llx",
               static_cast<unsigned long long>(val));
    }
  } else {
    snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(val));
  }
  out += buf;
}

inline void AppendArg(std::string& out, const char* spec_begin,
                      const char* spec_end, uint64_t val) {
  char buf[64];
  std::string spec(spec_begin, spec_end);
  if (spec.find('X') != std::string::npos || spec.find('x') != std::string::npos) {
    int width = 0;
    bool zero_pad = false;
    for (auto c : spec) {
      if (c == '0' && width == 0) zero_pad = true;
      if (c >= '0' && c <= '9') width = width * 10 + (c - '0');
    }
    bool upper = spec.find('X') != std::string::npos;
    if (zero_pad && width > 0) {
      snprintf(buf, sizeof(buf), upper ? "%0*llX" : "%0*llx", width,
               static_cast<unsigned long long>(val));
    } else {
      snprintf(buf, sizeof(buf), upper ? "%llX" : "%llx",
               static_cast<unsigned long long>(val));
    }
  } else {
    snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(val));
  }
  out += buf;
}

inline void AppendArg(std::string& out, const char*, const char*, double val) {
  char buf[64];
  snprintf(buf, sizeof(buf), "%g", val);
  out += buf;
}

inline void AppendArg(std::string& out, const char*, const char*, float val) {
  char buf[64];
  snprintf(buf, sizeof(buf), "%g", static_cast<double>(val));
  out += buf;
}

inline void AppendArg(std::string& out, const char*, const char*, const char* val) {
  out += (val ? val : "(null)");
}

inline void AppendArg(std::string& out, const char*, const char*, const std::string& val) {
  out += val;
}

inline void AppendArg(std::string& out, const char*, const char*, bool val) {
  out += val ? "true" : "false";
}

inline void AppendArg(std::string& out, const char* spec_begin,
                      const char* spec_end, const void* val) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%p", val);
  out += buf;
}

// size_t overloads (avoid ambiguity on some platforms)
#if !defined(__LP64__) || defined(__aarch64__)
// On 64-bit, size_t == uint64_t, already handled
// On 32-bit LP32, size_t == uint32_t, already handled
// But we need explicit overload if size_t is a distinct type
#endif

// Base case: no more arguments
inline void FormatImpl(std::string& out, const char* fmt_str) {
  while (*fmt_str) {
    if (*fmt_str == '{' && *(fmt_str + 1) != '\0') {
      // Unmatched placeholder — just output literally
      const char* end = strchr(fmt_str + 1, '}');
      if (end) {
        out.append(fmt_str, end + 1);
        fmt_str = end + 1;
        continue;
      }
    }
    out += *fmt_str++;
  }
}

// Recursive case: consume one {} placeholder per argument
template <typename T, typename... Args>
void FormatImpl(std::string& out, const char* fmt_str, const T& val,
                const Args&... args) {
  while (*fmt_str) {
    if (*fmt_str == '{') {
      // Find matching '}'
      const char* spec_begin = fmt_str + 1;
      const char* end = strchr(spec_begin, '}');
      if (end) {
        // Check for escaped {{ → literal {
        if (*spec_begin == '{') {
          out += '{';
          fmt_str = spec_begin + 1;
          continue;
        }
        // Format spec is between { and }
        AppendArg(out, spec_begin, end, val);
        FormatImpl(out, end + 1, args...);
        return;
      }
    }
    out += *fmt_str++;
  }
}

}  // namespace detail

/// xe::fmt("Hello {} world {:08X}", name, value) — NDK-compatible formatter
template <typename... Args>
std::string fmt(const char* format_str, const Args&... args) {
  std::string result;
  result.reserve(128);
  detail::FormatImpl(result, format_str, args...);
  return result;
}

// Overload for no-arg case
inline std::string fmt(const char* format_str) {
  return std::string(format_str);
}

}  // namespace xe

// ── Macros ──────────────────────────────────────────────────────────────────
#define XELOGV(fmt_str, ...) ::xe::LogLine(::xe::LogLevel::kVerbose, "XE", ::xe::fmt(fmt_str __VA_OPT__(,) __VA_ARGS__))
#define XELOGD(fmt_str, ...) ::xe::LogLine(::xe::LogLevel::kDebug,   "XE", ::xe::fmt(fmt_str __VA_OPT__(,) __VA_ARGS__))
#define XELOGI(fmt_str, ...) ::xe::LogLine(::xe::LogLevel::kInfo,    "XE", ::xe::fmt(fmt_str __VA_OPT__(,) __VA_ARGS__))
#define XELOGW(fmt_str, ...) ::xe::LogLine(::xe::LogLevel::kWarning, "XE", ::xe::fmt(fmt_str __VA_OPT__(,) __VA_ARGS__))
#define XELOGE(fmt_str, ...) ::xe::LogLine(::xe::LogLevel::kError,   "XE", ::xe::fmt(fmt_str __VA_OPT__(,) __VA_ARGS__))
#define XELOGF(fmt_str, ...) ::xe::LogLine(::xe::LogLevel::kFatal,   "XE", ::xe::fmt(fmt_str __VA_OPT__(,) __VA_ARGS__))
