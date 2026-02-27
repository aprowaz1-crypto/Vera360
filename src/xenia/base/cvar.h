/**
 * Vera360 — Xenia Edge
 * Console Variables (CVars) — runtime configuration system
 */
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>

namespace xe::cvar {

using CvarValue = std::variant<bool, int32_t, int64_t, float, double, std::string>;

struct CvarEntry {
  std::string name;
  std::string description;
  std::string category;
  CvarValue default_value;
  CvarValue current_value;
};

/// Global CVar registry
class CvarRegistry {
 public:
  static CvarRegistry& Get();

  void Register(const std::string& name, CvarValue default_val,
                const std::string& desc, const std::string& category = "");

  template <typename T>
  T GetValue(const std::string& name, T fallback = {}) const {
    auto it = entries_.find(name);
    if (it == entries_.end()) return fallback;
    auto* p = std::get_if<T>(&it->second.current_value);
    return p ? *p : fallback;
  }

  template <typename T>
  void SetValue(const std::string& name, T value) {
    auto it = entries_.find(name);
    if (it != entries_.end()) {
      it->second.current_value = std::move(value);
    }
  }

  bool LoadFromFile(const std::string& path);
  bool SaveToFile(const std::string& path) const;

  const std::unordered_map<std::string, CvarEntry>& GetAll() const { return entries_; }

 private:
  CvarRegistry() = default;
  std::unordered_map<std::string, CvarEntry> entries_;
};

// ── Convenience macros ──────────────────────────────────────────────────────
#define DEFINE_bool(name, default_val, desc) \
  static inline struct CvarInit_##name { \
    CvarInit_##name() { ::xe::cvar::CvarRegistry::Get().Register(#name, default_val, desc); } \
  } g_cvar_init_##name;

#define DEFINE_int32(name, default_val, desc) \
  static inline struct CvarInit_##name { \
    CvarInit_##name() { ::xe::cvar::CvarRegistry::Get().Register(#name, static_cast<int32_t>(default_val), desc); } \
  } g_cvar_init_##name;

#define DEFINE_string(name, default_val, desc) \
  static inline struct CvarInit_##name { \
    CvarInit_##name() { ::xe::cvar::CvarRegistry::Get().Register(#name, std::string(default_val), desc); } \
  } g_cvar_init_##name;

#define cvars ::xe::cvar::CvarRegistry::Get()

}  // namespace xe::cvar
