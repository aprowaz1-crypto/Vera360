/**
 * Vera360 — Xenia Edge
 * STFS Container — reads Xbox 360 STFS/SVOD containers (game packages)
 */

#include "xenia/base/logging.h"
#include <string>
#include <vector>
#include <fstream>
#include <cstdint>
#include <cstring>

namespace xe::vfs {

/// STFS header magic values
constexpr uint32_t kStfsMagicCon  = 0x434F4E20;  // "CON "
constexpr uint32_t kStfsMagicLive = 0x4C495645;  // "LIVE"
constexpr uint32_t kStfsMagicPirs = 0x50495253;  // "PIRS"

struct StfsHeader {
  uint32_t magic;
  char display_name[0x80];
  uint32_t content_type;
  uint32_t title_id;
};

class StfsContainer {
 public:
  StfsContainer(const std::string& path) : path_(path) {}

  bool Open() {
    std::ifstream f(path_, std::ios::binary);
    if (!f.is_open()) {
      XELOGW("Failed to open STFS container: {}", path_);
      return false;
    }

    // Read header
    uint8_t raw_header[0x200];
    f.read(reinterpret_cast<char*>(raw_header), sizeof(raw_header));
    if (f.gcount() < 4) return false;

    uint32_t magic;
    memcpy(&magic, raw_header, 4);
    magic = __builtin_bswap32(magic);

    if (magic != kStfsMagicCon && magic != kStfsMagicLive && magic != kStfsMagicPirs) {
      XELOGW("Not a valid STFS container (magic=0x{:08X})", magic);
      return false;
    }

    header_.magic = magic;
    XELOGI("STFS container opened: {} (magic=0x{:08X})", path_, magic);
    return true;
  }

  void Close() {
    XELOGI("STFS container closed: {}", path_);
  }

 private:
  std::string path_;
  StfsHeader header_ = {};
};

}  // namespace xe::vfs
