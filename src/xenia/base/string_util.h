/**
 * Vera360 — Xenia Edge
 * String utilities header
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace xe::string_util {

std::string ToLower(std::string_view s);
std::string ToUpper(std::string_view s);
std::vector<std::string> Split(std::string_view s, char delimiter);
std::string Trim(std::string_view s);
std::u16string ToUTF16(std::string_view utf8);

}  // namespace xe::string_util
