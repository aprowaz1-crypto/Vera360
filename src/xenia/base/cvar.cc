/**
 * Vera360 — Xenia Edge
 * CVar registry implementation
 */

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"

#include <fstream>
#include <sstream>

namespace xe::cvar {

CvarRegistry& CvarRegistry::Get() {
  static CvarRegistry instance;
  return instance;
}

void CvarRegistry::Register(const std::string& name, CvarValue default_val,
                             const std::string& desc, const std::string& category) {
  if (entries_.count(name)) return;
  entries_[name] = CvarEntry{name, desc, category, default_val, default_val};
}

bool CvarRegistry::LoadFromFile(const std::string& path) {
  std::ifstream f(path);
  if (!f.is_open()) return false;

  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#' || line[0] == ';') continue;
    auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    
    std::string key = line.substr(0, eq);
    std::string val = line.substr(eq + 1);
    
    // Trim
    while (!key.empty() && key.back() == ' ') key.pop_back();
    while (!val.empty() && val.front() == ' ') val.erase(val.begin());

    auto it = entries_.find(key);
    if (it == entries_.end()) continue;

    // Try to parse based on default type
    auto& entry = it->second;
    if (std::holds_alternative<bool>(entry.default_value)) {
      entry.current_value = (val == "true" || val == "1");
    } else if (std::holds_alternative<int32_t>(entry.default_value)) {
      entry.current_value = static_cast<int32_t>(std::stoi(val));
    } else if (std::holds_alternative<int64_t>(entry.default_value)) {
      entry.current_value = static_cast<int64_t>(std::stoll(val));
    } else if (std::holds_alternative<float>(entry.default_value)) {
      entry.current_value = std::stof(val);
    } else if (std::holds_alternative<double>(entry.default_value)) {
      entry.current_value = std::stod(val);
    } else {
      entry.current_value = val;
    }
  }

  XELOGI("CVars loaded from {}", path);
  return true;
}

bool CvarRegistry::SaveToFile(const std::string& path) const {
  std::ofstream f(path);
  if (!f.is_open()) return false;

  f << "# Vera360 — Xenia Edge Configuration\n\n";

  for (auto& [name, entry] : entries_) {
    f << "# " << entry.description << "\n";
    f << name << " = ";
    std::visit([&f](auto&& v) {
      using T = std::decay_t<decltype(v)>;
      if constexpr (std::is_same_v<T, bool>) {
        f << (v ? "true" : "false");
      } else if constexpr (std::is_same_v<T, std::string>) {
        f << v;
      } else {
        f << v;
      }
    }, entry.current_value);
    f << "\n\n";
  }

  return true;
}

}  // namespace xe::cvar
