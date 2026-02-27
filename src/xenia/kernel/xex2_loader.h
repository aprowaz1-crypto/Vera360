/**
 * Vera360 — Xenia Edge
 * XEX2 Loader — parses Xbox 360 executable format (XEX2)
 *
 * XEX2 is Microsoft's encrypted/compressed executable format for Xbox 360.
 * Structure:
 *   - XEX2 Header (magic, flags, security info offset, header count)
 *   - Optional Headers (import libs, TLS, entry point, etc.)
 *   - Security Info (AES keys, RSA signature, page descriptors)
 *   - Compressed/encrypted PE image
 *
 * For unencrypted/dev-signed XEX files, we can load directly.
 * For retail XEX files, decryption keys are needed.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

namespace xe::kernel {
class KernelState;
class XModule;
}

namespace xe::cpu {
class Processor;
}

namespace xe::loader {

/// XEX2 magic: "XEX2" = 0x58455832
constexpr uint32_t kXex2Magic = 0x58455832;
/// XEX1 magic: "XEX1" (old devkit format)
constexpr uint32_t kXex1Magic = 0x58455831;

/// XEX2 module flags
enum XexModuleFlags : uint32_t {
  kModuleFlagTitleModule   = 0x00000001,
  kModuleFlagExportsToTitle= 0x00000002,
  kModuleFlagSystemDebugger= 0x00000004,
  kModuleFlagDllModule     = 0x00000008,
  kModuleFlagModulePatch   = 0x00000010,
  kModuleFlagPatchFull     = 0x00000020,
  kModuleFlagPatchDelta    = 0x00000040,
  kModuleFlagUserMode      = 0x00000080,
};

/// XEX2 system flags
enum XexSystemFlags : uint32_t {
  kSystemFlagNoForceReboot       = 0x00000001,
  kSystemFlagForegroundTasks     = 0x00000002,
  kSystemFlagNoOddMapping        = 0x00000004,
  kSystemFlagHandlesGamepadDisconnect = 0x00000008,
  kSystemFlagInsecureSockets     = 0x00000040,
  kSystemFlagXamHooks            = 0x00000080,
  kSystemFlagDashContext          = 0x00000100,
  kSystemFlagGameVoiceRequired   = 0x00001000,
  kSystemFlagPal50Incompatible   = 0x04000000,
  kSystemFlagInsecureUtilityDrive= 0x08000000,
  kSystemFlagXamOnlinePolicyEnforced = 0x10000000,
  kSystemFlagXamOnlineXboxLiveOnly   = 0x20000000,
};

/// XEX2 optional header keys
enum XexHeaderKey : uint32_t {
  kHeaderResourceInfo               = 0x000002FF,
  kHeaderBaseFileFormat              = 0x000003FF,
  kHeaderBaseReference               = 0x00000405,
  kHeaderDeltaPatchDescriptor        = 0x000005FF,
  kHeaderBoundingPath                = 0x000080FF,
  kHeaderDeviceId                    = 0x00008105,
  kHeaderOriginalBaseAddress         = 0x00010001,
  kHeaderEntryPoint                  = 0x00010100,
  kHeaderImageBaseAddress            = 0x00010201,
  kHeaderImportLibraries             = 0x000103FF,
  kHeaderChecksumTimestamp           = 0x00018002,
  kHeaderEnabledForCallcap           = 0x00018102,
  kHeaderEnabledForFastcap           = 0x00018200,
  kHeaderOriginalPEName              = 0x000183FF,
  kHeaderStaticLibraries             = 0x000200FF,
  kHeaderTLSInfo                     = 0x00020104,
  kHeaderDefaultStackSize            = 0x00020200,
  kHeaderDefaultFilesystemCacheSize  = 0x00020301,
  kHeaderDefaultHeapSize             = 0x00020401,
  kHeaderPageHeapSizeAndFlags        = 0x00028002,
  kHeaderSystemFlags                 = 0x00030000,
  kHeaderExecutionInfo               = 0x00040006,
  kHeaderTitleWorkspaceSize          = 0x00040201,
  kHeaderGameRatings                 = 0x00040310,
  kHeaderLANKey                      = 0x00040404,
  kHeaderXbox360Logo                 = 0x000405FF,
  kHeaderMultidiscMediaIds           = 0x000406FF,
  kHeaderAlternateTitleIds           = 0x000407FF,
  kHeaderAdditionalTitleMemory       = 0x00040801,
  kHeaderExportsByName               = 0x00E10402,
};

/// XEX2 file header (on-disk, big-endian)
struct Xex2Header {
  uint32_t magic;               // "XEX2"
  uint32_t module_flags;
  uint32_t pe_data_offset;      // Offset to compressed PE data
  uint32_t reserved;
  uint32_t security_offset;     // Offset to security info
  uint32_t opt_header_count;    // Number of optional headers
};

/// XEX2 optional header entry
struct Xex2OptHeader {
  uint32_t key;   // XexHeaderKey | size info in low bits
  uint32_t value; // Direct value or offset depending on key
};

/// XEX2 security info
struct Xex2SecurityInfo {
  uint32_t header_size;
  uint32_t image_size;
  uint8_t  rsa_signature[256];
  uint32_t unknown_count;
  uint8_t  image_hash[20];      // SHA-1
  uint32_t import_table_count;
  uint8_t  import_table_hash[20];
  uint8_t  xgd3_media_id[16];
  uint8_t  aes_key[16];         // Decryption key
  uint32_t export_table;
  uint8_t  header_hash[20];
  uint32_t region;
  uint32_t allowed_media_types;
  uint32_t page_descriptor_count;
};

/// Page descriptor for compressed/encrypted sections
struct Xex2PageDescriptor {
  uint32_t size_and_info;  // [31:4] = page count, [3:0] = info flags
  uint8_t  hash[20];       // SHA-1 of page data
};

/// Compression type
enum class XexCompressionType : uint16_t {
  kNone     = 0,
  kRaw      = 1,
  kCompressed = 2,  // LZX delta compressed
  kDeltaCompressed = 3,
};

/// Encryption type
enum class XexEncryptionType : uint16_t {
  kNone      = 0,
  kNormal    = 1,
};

/// Base file format info
struct Xex2FileFormatInfo {
  uint32_t info_size;
  XexEncryptionType encryption_type;
  XexCompressionType compression_type;
};

/// Raw compression block
struct Xex2RawDataDescriptor {
  uint32_t data_size;
  uint32_t zero_size;
};

/// LZX compression info
struct Xex2CompressedBlockInfo {
  uint32_t block_size;
  uint8_t  block_hash[20];
};

/// TLS info
struct Xex2TlsInfo {
  uint32_t slot_count;
  uint32_t raw_data_address;
  uint32_t data_size;
  uint32_t raw_data_size;
};

/// Execution info
struct Xex2ExecutionInfo {
  uint32_t media_id;
  uint32_t version;
  uint32_t base_version;
  uint32_t title_id;
  uint8_t  platform;
  uint8_t  executable_type;
  uint8_t  disc_number;
  uint8_t  disc_count;
  uint32_t savegame_id;
};

/// Import library entry
struct XexImportLibrary {
  std::string name;
  uint32_t version_min;
  uint32_t version;
  std::vector<uint32_t> records;  // Import record addresses
};

/// Section from the PE image
struct XexSection {
  std::string name;
  uint32_t virtual_address;
  uint32_t virtual_size;
  uint32_t raw_address;
  uint32_t raw_size;
  uint32_t flags;
};

/// Parsed XEX module
struct XexModule {
  // Headers
  Xex2Header header;
  std::vector<Xex2OptHeader> opt_headers;
  
  // Extracted info
  uint32_t entry_point = 0;
  uint32_t base_address = 0x82000000;
  uint32_t image_size = 0;
  uint32_t stack_size = 0x40000;  // Default 256KB
  uint32_t heap_size = 0;
  uint32_t title_id = 0;
  uint32_t system_flags = 0;
  uint32_t module_flags = 0;
  
  Xex2FileFormatInfo format_info = {};
  Xex2ExecutionInfo exec_info = {};
  Xex2TlsInfo tls_info = {};
  
  std::vector<XexImportLibrary> import_libs;
  std::vector<XexSection> sections;
  
  // Decompressed PE image
  std::vector<uint8_t> pe_image;
  
  std::string name;
  std::string path;
};

class Xex2Loader {
 public:
  Xex2Loader() = default;
  
  /// Load a XEX2 file from disk
  bool Load(const std::string& path);
  
  /// Load from memory buffer
  bool LoadFromMemory(const uint8_t* data, size_t size);
  
  /// Map the loaded module into guest memory
  bool MapIntoMemory(uint8_t* guest_base);
  
  /// Resolve imports against kernel exports
  bool ResolveImports(uint8_t* guest_base);
  
  /// Get the parsed module info
  const XexModule& module() const { return module_; }
  XexModule& module() { return module_; }
  
 private:
  bool ParseHeader(const uint8_t* data, size_t size);
  bool ParseOptionalHeaders(const uint8_t* data, size_t size);
  bool ParseSecurityInfo(const uint8_t* data, size_t size);
  bool DecompressImage(const uint8_t* data, size_t size);
  bool DecompressRaw(const uint8_t* data, size_t size, uint32_t pe_offset);
  bool ParsePEHeaders();
  bool ParseImportLibraries(const uint8_t* data, size_t size);
  
  /// Byte-swap a 32-bit big-endian value
  static uint32_t BE32(uint32_t v) { return __builtin_bswap32(v); }
  static uint16_t BE16(uint16_t v) { return __builtin_bswap16(v); }
  
  XexModule module_;
  std::vector<uint8_t> raw_data_;
};

}  // namespace xe::loader
