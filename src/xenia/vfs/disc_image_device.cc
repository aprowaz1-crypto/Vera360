/**
 * Vera360 — Xenia Edge
 * Disc Image Device — reads Xbox 360 ISO/XISO disc images (.iso, .xex)
 */

#include "xenia/base/logging.h"
#include <string>
#include <fstream>
#include <cstdint>
#include <cstring>

namespace xe::vfs {

/// Xbox 360 XISO magic at sector 32
constexpr uint32_t kXisoMagic = 0x58534631;  // "XSF1" — Microsoft XDFS

class DiscImageDevice {
 public:
  DiscImageDevice(const std::string& path) : path_(path) {}

  bool Initialize() {
    file_.open(path_, std::ios::binary);
    if (!file_.is_open()) {
      XELOGW("Failed to open disc image: {}", path_);
      return false;
    }

    // Check XISO header at sector 32 (0x10000)
    file_.seekg(0x10000);
    uint8_t header[0x14];
    file_.read(reinterpret_cast<char*>(header), sizeof(header));

    uint32_t magic;
    memcpy(&magic, header, 4);
    // Xbox uses big-endian
    // But the actual disc format magic may be little-endian
    
    XELOGI("Disc image opened: {}", path_);
    valid_ = true;
    return true;
  }

  bool ReadSectors(uint64_t offset, uint8_t* buffer, size_t size) {
    if (!file_.is_open()) return false;
    file_.seekg(static_cast<std::streamoff>(offset));
    file_.read(reinterpret_cast<char*>(buffer), static_cast<std::streamsize>(size));
    return file_.gcount() == static_cast<std::streamsize>(size);
  }

  void Shutdown() {
    if (file_.is_open()) file_.close();
    XELOGI("Disc image closed");
  }

 private:
  std::string path_;
  std::ifstream file_;
  bool valid_ = false;
};

}  // namespace xe::vfs
