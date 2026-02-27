/**
 * Vera360 — Xenia Edge
 * PPC Function Scanner — finds function boundaries in guest code
 */

#include "xenia/cpu/frontend/ppc_decoder.h"
#include "xenia/base/memory/memory.h"
#include "xenia/base/logging.h"

#include <cstring>
#include <vector>

namespace xe::cpu::frontend {

struct FunctionInfo {
  uint32_t start_address;
  uint32_t end_address;
  uint32_t size_bytes;
  bool is_leaf;          // No function calls inside
};

/// Scan guest memory starting at 'start' to find function boundaries.
/// Returns when a blr (function return) or invalid instruction is hit.
FunctionInfo ScanFunction(uint32_t start_address) {
  FunctionInfo info = {};
  info.start_address = start_address;
  info.is_leaf = true;

  uint8_t* guest_base = xe::memory::GetGuestBase();
  if (!guest_base) {
    info.end_address = start_address;
    return info;
  }

  uint32_t pc = start_address;
  uint32_t max_scan = 0x40000;  // 256KB max function size

  while (pc < start_address + max_scan) {
    uint32_t code;
    memcpy(&code, guest_base + pc, sizeof(uint32_t));
    code = __builtin_bswap32(code);  // Big-endian → little-endian

    PPCInstruction instr = DecodePPC(pc, code);

    if (instr.type == PPCOpcodeType::kInvalid) {
      break;
    }

    if (IsFunctionCall(instr)) {
      info.is_leaf = false;
    }

    if (IsReturn(instr)) {
      pc += 4;
      break;
    }

    pc += 4;
  }

  info.end_address = pc;
  info.size_bytes = pc - start_address;

  XELOGD("Scanned function: 0x{:08X}–0x{:08X} ({} bytes, {})",
         info.start_address, info.end_address, info.size_bytes,
         info.is_leaf ? "leaf" : "non-leaf");

  return info;
}

}  // namespace xe::cpu::frontend
