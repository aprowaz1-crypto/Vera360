/**
 * Vera360 — Xenia Edge
 * xboxkrnl I/O shim — NtCreateFile, NtReadFile, NtWriteFile, etc.
 *
 * The guest uses NT-style paths:
 *   \\Device\\Harddisk0\\Partition1\\path   → game content
 *   \\Device\\CdRom0\\path                  → disc
 *   \\Device\\Mu0\\path                     → memory unit
 *   game:\\path                              → aliased game root
 *   d:\\path                                 → aliased game root
 *
 * We resolve these to host paths via the VFS mount table.
 */

#include "xenia/kernel/kernel_state.h"
#include "xenia/base/logging.h"
#include "xenia/base/memory/memory.h"
#include <functional>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <cerrno>
#include <vector>

namespace xe::kernel::xboxkrnl {

extern void RegisterExport(uint32_t ordinal, std::function<uint32_t(uint32_t*)> thunk);

// ── Status codes ─────────────────────────────────────────────────────────────
static constexpr uint32_t STATUS_SUCCESS              = 0x00000000;
static constexpr uint32_t STATUS_PENDING              = 0x00000103;
static constexpr uint32_t STATUS_INVALID_HANDLE       = 0xC0000008;
static constexpr uint32_t STATUS_INVALID_PARAMETER    = 0xC000000D;
static constexpr uint32_t STATUS_NO_SUCH_FILE         = 0xC000000F;
static constexpr uint32_t STATUS_END_OF_FILE          = 0xC0000011;
static constexpr uint32_t STATUS_ACCESS_DENIED        = 0xC0000022;
static constexpr uint32_t STATUS_OBJECT_NAME_NOT_FOUND= 0xC0000034;
static constexpr uint32_t STATUS_OBJECT_NAME_COLLISION= 0xC0000035;
static constexpr uint32_t STATUS_NOT_IMPLEMENTED      = 0xC0000002;

// ── Guest-host helpers ───────────────────────────────────────────────────────
static inline void GW32(uint32_t addr, uint32_t v) {
  auto* p = static_cast<uint8_t*>(xe::memory::TranslateVirtual(addr));
  p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16);
  p[2] = uint8_t(v >> 8);  p[3] = uint8_t(v);
}
static inline uint32_t GR32(uint32_t addr) {
  auto* p = static_cast<uint8_t*>(xe::memory::TranslateVirtual(addr));
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
         (uint32_t(p[2]) << 8)  | p[3];
}

// ── Simple file handle table ─────────────────────────────────────────────────
namespace {

struct OpenFile {
  int fd = -1;
  std::string host_path;
  uint64_t position = 0;
  bool is_directory = false;
};

std::unordered_map<uint32_t, OpenFile> g_open_files;
uint32_t g_next_file_handle = 0x1000;

/// Guest path prefix → host directory mapping
std::unordered_map<std::string, std::string> g_mount_table;

void InitMountTable() {
  // Default mount: game:\ → /sdcard/Vera360/Games/
  // Can be overridden later
  g_mount_table["\\Device\\Harddisk0\\Partition1"] = "/sdcard/Vera360/HDD";
  g_mount_table["\\Device\\CdRom0"] = "/sdcard/Vera360/Games";
  g_mount_table["\\Device\\Mu0"] = "/sdcard/Vera360/MU";
  g_mount_table["game:"] = "/sdcard/Vera360/Games";
  g_mount_table["d:"] = "/sdcard/Vera360/Games";
  g_mount_table["GAME:"] = "/sdcard/Vera360/Games";
  g_mount_table["D:"] = "/sdcard/Vera360/Games";
}

std::string ResolveGuestPath(const std::string& guest_path) {
  if (g_mount_table.empty()) InitMountTable();

  for (auto& [prefix, host_root] : g_mount_table) {
    if (guest_path.substr(0, prefix.size()) == prefix) {
      std::string remainder = guest_path.substr(prefix.size());
      // Convert backslashes to forward slashes
      for (auto& c : remainder) {
        if (c == '\\') c = '/';
      }
      return host_root + remainder;
    }
  }

  // Try as-is with backslash conversion
  std::string result = guest_path;
  for (auto& c : result) {
    if (c == '\\') c = '/';
  }
  XELOGW("IO: Could not resolve guest path: {}", guest_path);
  return result;
}

/// Read a guest OBJECT_ATTRIBUTES structure to extract the path
std::string ReadObjectName(uint32_t obj_attrs_ptr) {
  if (!obj_attrs_ptr) return "";

  // OBJECT_ATTRIBUTES (Xbox):
  //   +0: HANDLE RootDirectory (4 bytes, BE)
  //   +4: PUNICODE_STRING ObjectName (4 bytes, BE)
  //   +8: ULONG Attributes (4 bytes, BE)
  uint32_t name_ptr = GR32(obj_attrs_ptr + 4);
  if (!name_ptr) return "";

  // UNICODE_STRING:
  //   +0: USHORT Length (2 bytes, BE)
  //   +2: USHORT MaximumLength (2 bytes, BE)
  //   +4: PWSTR Buffer (4 bytes, BE)
  auto* nsp = static_cast<uint8_t*>(xe::memory::TranslateVirtual(name_ptr));
  uint16_t len = (uint16_t(nsp[0]) << 8) | nsp[1];
  uint32_t buf_ptr = GR32(name_ptr + 4);

  if (!buf_ptr || len == 0) return "";

  // Read wide string (big-endian UTF-16)
  auto* wstr = static_cast<uint8_t*>(xe::memory::TranslateVirtual(buf_ptr));
  std::string result;
  result.reserve(len / 2);
  for (uint16_t i = 0; i < len; i += 2) {
    uint16_t ch = (uint16_t(wstr[i]) << 8) | wstr[i + 1];
    if (ch < 128) {
      result.push_back(static_cast<char>(ch));
    } else {
      result.push_back('?');
    }
  }
  return result;
}

}  // anonymous namespace

void RegisterIoExports() {

  // ═══════════════════════════════════════════════════════════════════════════
  // File creation / opening
  // ═══════════════════════════════════════════════════════════════════════════

  // NtCreateFile (186)
  RegisterExport(186, [](uint32_t* args) -> uint32_t {
    // NTSTATUS NtCreateFile(
    //   PHANDLE FileHandle,          // args[0] — out
    //   ACCESS_MASK DesiredAccess,    // args[1]
    //   POBJECT_ATTRIBUTES ObjAttrs, // args[2]
    //   PIO_STATUS_BLOCK IoStatus,   // args[3]
    //   PLARGE_INTEGER AllocSize,    // args[4]
    //   ULONG FileAttributes,        // args[5]
    //   ULONG ShareAccess,           // args[6]
    //   ULONG CreateDisposition,     // args[7]
    //   ULONG CreateOptions)         // args[8]
    uint32_t handle_out = args[0];
    uint32_t access = args[1];
    uint32_t obj_attrs_ptr = args[2];
    uint32_t io_status_ptr = args[3];
    uint32_t create_disp = args[7];
    uint32_t create_options = args[8];

    std::string guest_path = ReadObjectName(obj_attrs_ptr);
    std::string host_path = ResolveGuestPath(guest_path);

    XELOGI("NtCreateFile: '{}' -> '{}' access=0x{:08X} disp=0x{:X}",
           guest_path, host_path, access, create_disp);

    // Check if it's a directory open
    bool is_dir = (create_options & 0x01) != 0; // FILE_DIRECTORY_FILE

    struct stat st;
    bool exists = (stat(host_path.c_str(), &st) == 0);

    if (is_dir) {
      if (!exists || !S_ISDIR(st.st_mode)) {
        if (io_status_ptr) {
          GW32(io_status_ptr, STATUS_OBJECT_NAME_NOT_FOUND);
          GW32(io_status_ptr + 4, 0);
        }
        return STATUS_OBJECT_NAME_NOT_FOUND;
      }
      // "Open" the directory
      uint32_t handle = g_next_file_handle++;
      OpenFile of;
      of.host_path = host_path;
      of.is_directory = true;
      g_open_files[handle] = of;
      if (handle_out) GW32(handle_out, handle);
      if (io_status_ptr) {
        GW32(io_status_ptr, STATUS_SUCCESS);
        GW32(io_status_ptr + 4, 1);  // FILE_OPENED
      }
      return STATUS_SUCCESS;
    }

    // Create disposition:
    // 0 = FILE_SUPERSEDE, 1 = FILE_OPEN, 2 = FILE_CREATE,
    // 3 = FILE_OPEN_IF, 4 = FILE_OVERWRITE, 5 = FILE_OVERWRITE_IF
    int flags = 0;
    bool want_write = (access & 0x40000000) || (access & 0x00000002);
    
    switch (create_disp) {
      case 0: // FILE_SUPERSEDE
      case 5: // FILE_OVERWRITE_IF
        flags = O_CREAT | O_TRUNC | (want_write ? O_RDWR : O_RDONLY);
        break;
      case 1: // FILE_OPEN
        if (!exists) {
          if (io_status_ptr) GW32(io_status_ptr, STATUS_OBJECT_NAME_NOT_FOUND);
          return STATUS_OBJECT_NAME_NOT_FOUND;
        }
        flags = want_write ? O_RDWR : O_RDONLY;
        break;
      case 2: // FILE_CREATE
        if (exists) {
          if (io_status_ptr) GW32(io_status_ptr, STATUS_OBJECT_NAME_COLLISION);
          return STATUS_OBJECT_NAME_COLLISION;
        }
        flags = O_CREAT | O_EXCL | (want_write ? O_RDWR : O_RDONLY);
        break;
      case 3: // FILE_OPEN_IF
        flags = O_CREAT | (want_write ? O_RDWR : O_RDONLY);
        break;
      case 4: // FILE_OVERWRITE
        if (!exists) {
          if (io_status_ptr) GW32(io_status_ptr, STATUS_OBJECT_NAME_NOT_FOUND);
          return STATUS_OBJECT_NAME_NOT_FOUND;
        }
        flags = O_TRUNC | (want_write ? O_RDWR : O_RDONLY);
        break;
      default:
        flags = want_write ? O_RDWR : O_RDONLY;
        break;
    }

    int fd = open(host_path.c_str(), flags, 0666);
    if (fd < 0) {
      XELOGW("NtCreateFile: open() failed: {} (errno={})", host_path, errno);
      if (io_status_ptr) GW32(io_status_ptr, STATUS_OBJECT_NAME_NOT_FOUND);
      return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    uint32_t handle = g_next_file_handle++;
    OpenFile of;
    of.fd = fd;
    of.host_path = host_path;
    g_open_files[handle] = of;

    if (handle_out) GW32(handle_out, handle);
    if (io_status_ptr) {
      GW32(io_status_ptr, STATUS_SUCCESS);
      GW32(io_status_ptr + 4, exists ? 1 : 2);  // FILE_OPENED / FILE_CREATED
    }

    XELOGI("NtCreateFile: handle=0x{:08X} fd={}", handle, fd);
    return STATUS_SUCCESS;
  });

  // NtOpenFile (202) — simplified version of NtCreateFile
  RegisterExport(202, [](uint32_t* args) -> uint32_t {
    uint32_t handle_out = args[0];
    uint32_t access = args[1];
    uint32_t obj_attrs_ptr = args[2];
    uint32_t io_status_ptr = args[3];

    std::string guest_path = ReadObjectName(obj_attrs_ptr);
    std::string host_path = ResolveGuestPath(guest_path);

    XELOGI("NtOpenFile: '{}' -> '{}'", guest_path, host_path);

    int fd = open(host_path.c_str(), O_RDONLY);
    if (fd < 0) {
      if (io_status_ptr) GW32(io_status_ptr, STATUS_OBJECT_NAME_NOT_FOUND);
      return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    uint32_t handle = g_next_file_handle++;
    OpenFile of;
    of.fd = fd;
    of.host_path = host_path;
    g_open_files[handle] = of;

    if (handle_out) GW32(handle_out, handle);
    if (io_status_ptr) {
      GW32(io_status_ptr, STATUS_SUCCESS);
      GW32(io_status_ptr + 4, 1);
    }
    return STATUS_SUCCESS;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // Read / Write
  // ═══════════════════════════════════════════════════════════════════════════

  // NtReadFile (209)
  RegisterExport(209, [](uint32_t* args) -> uint32_t {
    // NTSTATUS NtReadFile(
    //   HANDLE FileHandle,           // args[0]
    //   HANDLE Event,                // args[1] — optional
    //   PIO_APC_ROUTINE ApcRoutine,  // args[2]
    //   PVOID ApcContext,            // args[3]
    //   PIO_STATUS_BLOCK IoStatus,   // args[4]
    //   PVOID Buffer,               // args[5]
    //   ULONG Length,               // args[6]
    //   PLARGE_INTEGER ByteOffset,  // args[7]
    //   PULONG Key)                 // args[8]
    uint32_t handle = args[0];
    uint32_t io_status_ptr = args[4];
    uint32_t buffer_ptr = args[5];
    uint32_t length = args[6];
    uint32_t offset_ptr = args[7];

    auto it = g_open_files.find(handle);
    if (it == g_open_files.end()) {
      XELOGW("NtReadFile: invalid handle 0x{:08X}", handle);
      return STATUS_INVALID_HANDLE;
    }

    auto& of = it->second;
    if (of.fd < 0) return STATUS_INVALID_HANDLE;

    // Seek if offset provided
    if (offset_ptr) {
      auto* op = static_cast<uint8_t*>(xe::memory::TranslateVirtual(offset_ptr));
      int64_t offset = (int64_t(op[0]) << 56) | (int64_t(op[1]) << 48) |
                       (int64_t(op[2]) << 40) | (int64_t(op[3]) << 32) |
                       (int64_t(op[4]) << 24) | (int64_t(op[5]) << 16) |
                       (int64_t(op[6]) << 8)  | int64_t(op[7]);
      lseek(of.fd, offset, SEEK_SET);
    }

    void* host_buf = xe::memory::TranslateVirtual(buffer_ptr);
    ssize_t bytes_read = read(of.fd, host_buf, length);

    if (bytes_read < 0) {
      XELOGW("NtReadFile: read() failed (errno={})", errno);
      if (io_status_ptr) {
        GW32(io_status_ptr, STATUS_ACCESS_DENIED);
        GW32(io_status_ptr + 4, 0);
      }
      return STATUS_ACCESS_DENIED;
    }

    if (bytes_read == 0) {
      if (io_status_ptr) {
        GW32(io_status_ptr, STATUS_END_OF_FILE);
        GW32(io_status_ptr + 4, 0);
      }
      return STATUS_END_OF_FILE;
    }

    of.position += bytes_read;
    if (io_status_ptr) {
      GW32(io_status_ptr, STATUS_SUCCESS);
      GW32(io_status_ptr + 4, static_cast<uint32_t>(bytes_read));
    }

    return STATUS_SUCCESS;
  });

  // NtWriteFile (225)
  RegisterExport(225, [](uint32_t* args) -> uint32_t {
    uint32_t handle = args[0];
    uint32_t io_status_ptr = args[4];
    uint32_t buffer_ptr = args[5];
    uint32_t length = args[6];
    uint32_t offset_ptr = args[7];

    auto it = g_open_files.find(handle);
    if (it == g_open_files.end()) return STATUS_INVALID_HANDLE;

    auto& of = it->second;
    if (of.fd < 0) return STATUS_INVALID_HANDLE;

    if (offset_ptr) {
      auto* op = static_cast<uint8_t*>(xe::memory::TranslateVirtual(offset_ptr));
      int64_t offset = (int64_t(op[0]) << 56) | (int64_t(op[1]) << 48) |
                       (int64_t(op[2]) << 40) | (int64_t(op[3]) << 32) |
                       (int64_t(op[4]) << 24) | (int64_t(op[5]) << 16) |
                       (int64_t(op[6]) << 8)  | int64_t(op[7]);
      lseek(of.fd, offset, SEEK_SET);
    }

    void* host_buf = xe::memory::TranslateVirtual(buffer_ptr);
    ssize_t bytes_written = write(of.fd, host_buf, length);

    if (bytes_written < 0) {
      if (io_status_ptr) {
        GW32(io_status_ptr, STATUS_ACCESS_DENIED);
        GW32(io_status_ptr + 4, 0);
      }
      return STATUS_ACCESS_DENIED;
    }

    of.position += bytes_written;
    if (io_status_ptr) {
      GW32(io_status_ptr, STATUS_SUCCESS);
      GW32(io_status_ptr + 4, static_cast<uint32_t>(bytes_written));
    }

    return STATUS_SUCCESS;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // File info
  // ═══════════════════════════════════════════════════════════════════════════

  // NtQueryInformationFile (206)
  RegisterExport(206, [](uint32_t* args) -> uint32_t {
    uint32_t handle = args[0];
    uint32_t io_status_ptr = args[1];
    uint32_t info_ptr = args[2];
    uint32_t info_length = args[3];
    uint32_t info_class = args[4];

    XELOGI("NtQueryInformationFile: handle=0x{:08X} class={}", handle, info_class);

    auto it = g_open_files.find(handle);
    if (it == g_open_files.end()) return STATUS_INVALID_HANDLE;

    auto& of = it->second;
    struct stat st;
    bool have_stat = false;

    if (of.fd >= 0) {
      have_stat = (fstat(of.fd, &st) == 0);
    } else if (!of.host_path.empty()) {
      have_stat = (stat(of.host_path.c_str(), &st) == 0);
    }

    switch (info_class) {
      case 5: {
        // FileStandardInformation
        // {LARGE_INTEGER AllocationSize, LARGE_INTEGER EndOfFile,
        //  ULONG NumberOfLinks, BOOLEAN DeletePending, BOOLEAN Directory}
        if (info_ptr && info_length >= 24 && have_stat) {
          auto* p = static_cast<uint8_t*>(xe::memory::TranslateVirtual(info_ptr));
          memset(p, 0, 24);
          uint64_t file_size = static_cast<uint64_t>(st.st_size);
          // AllocationSize (8 bytes, BE)
          for (int i = 0; i < 8; ++i) p[i] = uint8_t(file_size >> ((7-i)*8));
          // EndOfFile (8 bytes, BE)
          for (int i = 0; i < 8; ++i) p[8+i] = uint8_t(file_size >> ((7-i)*8));
          // NumberOfLinks
          GW32(info_ptr + 16, 1);
          // DeletePending
          p[20] = 0;
          // Directory
          p[21] = S_ISDIR(st.st_mode) ? 1 : 0;
        }
        break;
      }
      case 34: {
        // FilePositionInformation
        if (info_ptr && info_length >= 8) {
          uint64_t pos = of.position;
          auto* p = static_cast<uint8_t*>(xe::memory::TranslateVirtual(info_ptr));
          for (int i = 0; i < 8; ++i) p[i] = uint8_t(pos >> ((7-i)*8));
        }
        break;
      }
      case 4: {
        // FileBasicInformation
        // {CreationTime, LastAccessTime, LastWriteTime, ChangeTime, FileAttributes}
        if (info_ptr && info_length >= 40) {
          auto* p = static_cast<uint8_t*>(xe::memory::TranslateVirtual(info_ptr));
          memset(p, 0, 40);
          // FileAttributes at offset 32
          uint32_t attrs = 0x80; // FILE_ATTRIBUTE_NORMAL
          if (have_stat && S_ISDIR(st.st_mode)) attrs = 0x10; // DIRECTORY
          GW32(info_ptr + 32, attrs);
        }
        break;
      }
      case 35: {
        // FileNetworkOpenInformation
        if (info_ptr && info_length >= 56) {
          memset(xe::memory::TranslateVirtual(info_ptr), 0, 56);
          if (have_stat) {
            uint64_t file_size = static_cast<uint64_t>(st.st_size);
            auto* p = static_cast<uint8_t*>(xe::memory::TranslateVirtual(info_ptr));
            // AllocationSize at 32
            for (int i = 0; i < 8; ++i) p[32+i] = uint8_t(file_size >> ((7-i)*8));
            // EndOfFile at 40
            for (int i = 0; i < 8; ++i) p[40+i] = uint8_t(file_size >> ((7-i)*8));
          }
        }
        break;
      }
      default:
        XELOGW("NtQueryInformationFile: unhandled class {}", info_class);
        break;
    }

    if (io_status_ptr) {
      GW32(io_status_ptr, STATUS_SUCCESS);
      GW32(io_status_ptr + 4, 0);
    }
    return STATUS_SUCCESS;
  });

  // NtSetInformationFile (218)
  RegisterExport(218, [](uint32_t* args) -> uint32_t {
    uint32_t handle = args[0];
    uint32_t info_class = args[4];
    XELOGI("NtSetInformationFile: handle=0x{:08X} class={}", handle, info_class);

    if (info_class == 34) {
      // FilePositionInformation — update file position
      uint32_t info_ptr = args[2];
      auto it = g_open_files.find(handle);
      if (it != g_open_files.end() && info_ptr) {
        auto* p = static_cast<uint8_t*>(xe::memory::TranslateVirtual(info_ptr));
        int64_t new_pos = 0;
        for (int i = 0; i < 8; ++i) new_pos |= int64_t(p[i]) << ((7-i)*8);
        if (it->second.fd >= 0) {
          lseek(it->second.fd, new_pos, SEEK_SET);
        }
        it->second.position = new_pos;
      }
    }

    return STATUS_SUCCESS;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // Directory enumeration
  // ═══════════════════════════════════════════════════════════════════════════

  // NtQueryDirectoryFile (205)
  RegisterExport(205, [](uint32_t* args) -> uint32_t {
    uint32_t handle = args[0];
    uint32_t io_status_ptr = args[4];
    uint32_t buffer_ptr = args[5];
    uint32_t buffer_length = args[6];
    uint32_t info_class = args[7];
    uint32_t file_name_ptr = args[9];

    XELOGI("NtQueryDirectoryFile: handle=0x{:08X} class={}", handle, info_class);

    auto it = g_open_files.find(handle);
    if (it == g_open_files.end() || !it->second.is_directory) {
      return STATUS_INVALID_HANDLE;
    }

    // For now, return "no more files"
    if (io_status_ptr) {
      GW32(io_status_ptr, STATUS_NO_SUCH_FILE);
      GW32(io_status_ptr + 4, 0);
    }
    return STATUS_NO_SUCH_FILE;
  });

  // NtQueryFullAttributesFile (208)
  RegisterExport(208, [](uint32_t* args) -> uint32_t {
    uint32_t obj_attrs_ptr = args[0];
    uint32_t info_ptr = args[1];

    std::string guest_path = ReadObjectName(obj_attrs_ptr);
    std::string host_path = ResolveGuestPath(guest_path);

    XELOGI("NtQueryFullAttributesFile: '{}' -> '{}'", guest_path, host_path);

    struct stat st;
    if (stat(host_path.c_str(), &st) != 0) {
      return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    // Fill FILE_NETWORK_OPEN_INFORMATION (56 bytes)
    if (info_ptr) {
      auto* p = static_cast<uint8_t*>(xe::memory::TranslateVirtual(info_ptr));
      memset(p, 0, 56);
      uint64_t file_size = static_cast<uint64_t>(st.st_size);
      // AllocationSize at 32
      for (int i = 0; i < 8; ++i) p[32+i] = uint8_t(file_size >> ((7-i)*8));
      // EndOfFile at 40
      for (int i = 0; i < 8; ++i) p[40+i] = uint8_t(file_size >> ((7-i)*8));
      // FileAttributes at 48
      uint32_t attrs = S_ISDIR(st.st_mode) ? 0x10 : 0x80;
      GW32(info_ptr + 48, attrs);
    }

    return STATUS_SUCCESS;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // Close / DeviceIoControl
  // ═══════════════════════════════════════════════════════════════════════════

  // NtClose (184) — handled in module, but also close our file table
  // (The module version just removes from kernel object table)

  // NtDeviceIoControlFile (198)
  RegisterExport(198, [](uint32_t* args) -> uint32_t {
    uint32_t handle = args[0];
    uint32_t io_control_code = args[5];
    XELOGI("NtDeviceIoControlFile: handle=0x{:08X} ioctl=0x{:08X}",
           handle, io_control_code);
    return STATUS_NOT_IMPLEMENTED;
  });

  // NtFsControlFile (201)
  RegisterExport(201, [](uint32_t* args) -> uint32_t {
    XELOGI("NtFsControlFile");
    return STATUS_NOT_IMPLEMENTED;
  });

  // NtQueryVolumeInformationFile (— ordinal 224)
  RegisterExport(224, [](uint32_t* args) -> uint32_t {
    uint32_t handle = args[0];
    uint32_t io_status_ptr = args[1];
    uint32_t buffer_ptr = args[2];
    uint32_t buffer_length = args[3];
    uint32_t info_class = args[4];

    XELOGI("NtQueryVolumeInformationFile: class={}", info_class);

    // FileFsSizeInformation (3)
    if (info_class == 3 && buffer_ptr && buffer_length >= 24) {
      auto* p = static_cast<uint8_t*>(xe::memory::TranslateVirtual(buffer_ptr));
      memset(p, 0, 24);
      // TotalAllocationUnits = 512MB / 16KB = 32768
      GW32(buffer_ptr + 0, 0);
      GW32(buffer_ptr + 4, 32768);
      // AvailableAllocationUnits
      GW32(buffer_ptr + 8, 0);
      GW32(buffer_ptr + 12, 16384);
      // SectorsPerAllocationUnit = 32
      GW32(buffer_ptr + 16, 32);
      // BytesPerSector = 512
      GW32(buffer_ptr + 20, 512);
    }

    if (io_status_ptr) {
      GW32(io_status_ptr, STATUS_SUCCESS);
      GW32(io_status_ptr + 4, 0);
    }
    return STATUS_SUCCESS;
  });

  // ═══════════════════════════════════════════════════════════════════════════
  // Symbolic links (ObSymbolicLink)
  // ═══════════════════════════════════════════════════════════════════════════

  // ObCreateSymbolicLink (— ordinal 351)
  RegisterExport(351, [](uint32_t* args) -> uint32_t {
    XELOGI("ObCreateSymbolicLink");
    return STATUS_SUCCESS;
  });

  // ObDeleteSymbolicLink (352)
  RegisterExport(352, [](uint32_t* args) -> uint32_t {
    XELOGI("ObDeleteSymbolicLink");
    return STATUS_SUCCESS;
  });

  // IoCreateDevice (— ordinal 85)
  RegisterExport(85, [](uint32_t* args) -> uint32_t {
    XELOGI("IoCreateDevice");
    return STATUS_SUCCESS;
  });

  // IoDeleteDevice (86)
  RegisterExport(86, [](uint32_t* args) -> uint32_t {
    XELOGI("IoDeleteDevice");
    return STATUS_SUCCESS;
  });

  XELOGI("Registered xboxkrnl I/O exports");
}

}  // namespace xe::kernel::xboxkrnl
