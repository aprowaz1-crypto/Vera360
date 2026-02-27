/**
 * Vera360 — Xenia Edge
 * String utilities
 */

#include "xenia/base/string_util.h"
#include <algorithm>
#include <cctype>

namespace xe::string_util {

std::string ToLower(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return out;
}

std::string ToUpper(std::string_view s) {
  std::string out(s);
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return std::toupper(c); });
  return out;
}

std::vector<std::string> Split(std::string_view s, char delimiter) {
  std::vector<std::string> tokens;
  size_t start = 0;
  while (start < s.size()) {
    size_t end = s.find(delimiter, start);
    if (end == std::string_view::npos) end = s.size();
    if (end > start) {
      tokens.emplace_back(s.substr(start, end - start));
    }
    start = end + 1;
  }
  return tokens;
}

std::string Trim(std::string_view s) {
  auto start = s.find_first_not_of(" \t\r\n");
  if (start == std::string_view::npos) return "";
  auto end = s.find_last_not_of(" \t\r\n");
  return std::string(s.substr(start, end - start + 1));
}

std::u16string ToUTF16(std::string_view utf8) {
  std::u16string result;
  result.reserve(utf8.size());
  size_t i = 0;
  while (i < utf8.size()) {
    uint32_t cp = 0;
    uint8_t c = static_cast<uint8_t>(utf8[i]);
    if (c < 0x80) {
      cp = c; i += 1;
    } else if ((c >> 5) == 0x06) {
      cp = (c & 0x1F) << 6;
      if (i + 1 < utf8.size()) cp |= (utf8[i+1] & 0x3F);
      i += 2;
    } else if ((c >> 4) == 0x0E) {
      cp = (c & 0x0F) << 12;
      if (i + 1 < utf8.size()) cp |= (utf8[i+1] & 0x3F) << 6;
      if (i + 2 < utf8.size()) cp |= (utf8[i+2] & 0x3F);
      i += 3;
    } else {
      cp = (c & 0x07) << 18;
      if (i + 1 < utf8.size()) cp |= (utf8[i+1] & 0x3F) << 12;
      if (i + 2 < utf8.size()) cp |= (utf8[i+2] & 0x3F) << 6;
      if (i + 3 < utf8.size()) cp |= (utf8[i+3] & 0x3F);
      i += 4;
      if (cp > 0xFFFF) {
        cp -= 0x10000;
        result.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
        result.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        continue;
      }
    }
    result.push_back(static_cast<char16_t>(cp));
  }
  return result;
}

}  // namespace xe::string_util
