/**
 * Vera360 — Xenia Edge
 * Host Path Device — maps guest FS paths to Android host paths
 */

#include "xenia/base/logging.h"
#include <string>
#include <cstdio>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <cerrno>

namespace xe::vfs {

class HostPathDevice {
 public:
  HostPathDevice(const std::string& mount, const std::string& host_root)
      : mount_path_(mount), host_root_(host_root) {}

  bool Initialize() {
    struct stat st;
    if (stat(host_root_.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) {
      XELOGW("Host path does not exist or is not a directory: {}", host_root_);
      return false;
    }
    XELOGI("Host path device: {} -> {}", mount_path_, host_root_);
    return true;
  }

  std::string ResolvePath(const std::string& relative) const {
    return host_root_ + "/" + relative;
  }

  bool FileExists(const std::string& path) const {
    std::string full = ResolvePath(path);
    return access(full.c_str(), F_OK) == 0;
  }

  uint64_t FileSize(const std::string& path) const {
    struct stat st;
    if (stat(ResolvePath(path).c_str(), &st) == 0) {
      return static_cast<uint64_t>(st.st_size);
    }
    return 0;
  }

 private:
  std::string mount_path_;
  std::string host_root_;
};

}  // namespace xe::vfs
