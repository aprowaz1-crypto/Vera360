/**
 * Vera360 — Xenia Edge
 * Virtual File System — main controller
 */

#include "xenia/base/logging.h"
#include <string>
#include <unordered_map>
#include <memory>

namespace xe::vfs {

class VfsDevice;

/// Represents a file entry in the virtual file system
class VfsEntry {
 public:
  VfsEntry(const std::string& name, bool is_directory, uint64_t size = 0)
      : name_(name), is_directory_(is_directory), size_(size) {}
  
  const std::string& name() const { return name_; }
  bool is_directory() const { return is_directory_; }
  uint64_t size() const { return size_; }

 private:
  std::string name_;
  bool is_directory_;
  uint64_t size_;
};

/// Manages all mounted devices and file path resolution
class VirtualFileSystem {
 public:
  VirtualFileSystem() = default;
  ~VirtualFileSystem() = default;

  bool Initialize() {
    XELOGI("Virtual file system initialized");
    return true;
  }

  void Shutdown() {
    mounts_.clear();
    XELOGI("Virtual file system shut down");
  }

  /// Mount a device at a target path (e.g., "\\Device\\Harddisk0\\Partition1" -> host path)
  bool MountDevice(const std::string& mount_path, const std::string& device_host_path) {
    mounts_[mount_path] = device_host_path;
    XELOGI("VFS mount: {} -> {}", mount_path, device_host_path);
    return true;
  }

  /// Resolve a guest path to a host path
  std::string ResolvePath(const std::string& guest_path) const {
    for (auto& [prefix, host_root] : mounts_) {
      if (guest_path.substr(0, prefix.size()) == prefix) {
        return host_root + guest_path.substr(prefix.size());
      }
    }
    XELOGW("VFS: unresolved path: {}", guest_path);
    return "";
  }

 private:
  std::unordered_map<std::string, std::string> mounts_;
};

}  // namespace xe::vfs
