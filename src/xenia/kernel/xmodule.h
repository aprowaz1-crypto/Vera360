/**
 * Vera360 — Xenia Edge
 * XModule — emulated Xbox 360 executable/library module (XEX)
 */
#pragma once

#include "xenia/kernel/xobject.h"
#include <cstdint>
#include <string>
#include <unordered_map>

namespace xe::kernel {

class XModule : public XObject {
 public:
  XModule(KernelState* state, const std::string& path)
      : XObject(state, Type::kModule), path_(path) {
    // Extract module name from path
    auto pos = path.find_last_of("/\\");
    name_ = (pos != std::string::npos) ? path.substr(pos + 1) : path;
  }
  ~XModule() override = default;

  const std::string& path() const { return path_; }
  const std::string& name() const { return name_; }

  /// Guest address where the module is loaded
  uint32_t base_address() const { return base_address_; }
  void set_base_address(uint32_t addr) { base_address_ = addr; }

  uint32_t entry_point() const { return entry_point_; }
  void set_entry_point(uint32_t addr) { entry_point_ = addr; }

  /// Import resolution
  void AddImport(uint32_t ordinal, uint32_t thunk_address) {
    imports_[ordinal] = thunk_address;
  }

  uint32_t GetImportAddress(uint32_t ordinal) const {
    auto it = imports_.find(ordinal);
    return it != imports_.end() ? it->second : 0;
  }

 private:
  std::string path_;
  std::string name_;
  uint32_t base_address_ = 0;
  uint32_t entry_point_ = 0;
  std::unordered_map<uint32_t, uint32_t> imports_;
};

}  // namespace xe::kernel
