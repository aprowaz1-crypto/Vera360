/**
 * Vera360 — Xenia Edge
 * PPC Instruction Decoder — decodes PowerPC (Xenon) instruction stream
 */
#pragma once

#include <cstdint>

namespace xe::cpu::frontend {

/// PPC instruction types
enum class PPCOpcodeType : uint8_t {
  kInvalid = 0,
  kInteger,
  kFloat,
  kVector,       // VMX128
  kBranch,
  kSystem,
  kLoad,
  kStore,
  kCR,           // Condition Register
};

/// Decoded PPC instruction
struct PPCInstruction {
  uint32_t address;         // Guest address
  uint32_t code;            // Raw instruction word
  PPCOpcodeType type;
  uint8_t  opcode;          // Primary opcode (bits 0-5)
  uint16_t xo;              // Extended opcode

  // Operands (not all used for every instruction)
  uint8_t  rD, rS, rA, rB, rC;
  int16_t  simm;
  uint16_t uimm;
  uint8_t  sh, mb, me;
  uint8_t  bo, bi;
  int32_t  branch_offset;
  bool     link;            // LK bit
  bool     record;          // Rc bit
  bool     oe;              // OE bit (overflow)
  bool     absolute;        // AA bit for branches
};

/// Decode a single PPC instruction
PPCInstruction DecodePPC(uint32_t address, uint32_t code);

/// Get mnemonic string for an instruction
const char* GetPPCMnemonic(const PPCInstruction& instr);

/// Check if instruction is a function return (blr)
bool IsReturn(const PPCInstruction& instr);

/// Check if instruction is a function call (bl)
bool IsFunctionCall(const PPCInstruction& instr);

/// Check if instruction is unconditional branch
bool IsUnconditionalBranch(const PPCInstruction& instr);

}  // namespace xe::cpu::frontend
