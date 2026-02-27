/**
 * Vera360 — Xenia Edge
 * PPC Disassembler — text output for debugging
 */

#include "xenia/cpu/frontend/ppc_decoder.h"
#include <cstdio>
#include <string>

namespace xe::cpu::frontend {

std::string DisassemblePPC(uint32_t address, uint32_t code) {
  PPCInstruction instr = DecodePPC(address, code);
  char buf[256];

  const char* mnemonic = GetPPCMnemonic(instr);

  switch (instr.type) {
    case PPCOpcodeType::kInteger:
      if (instr.opcode == 14 || instr.opcode == 15) {
        snprintf(buf, sizeof(buf), "%08X  %s r%u, r%u, %d",
                 address, mnemonic, instr.rD, instr.rA, instr.simm);
      } else if (instr.opcode == 24 || instr.opcode == 26) {
        snprintf(buf, sizeof(buf), "%08X  %s r%u, r%u, 0x%04X",
                 address, mnemonic, instr.rA, instr.rS, instr.uimm);
      } else {
        snprintf(buf, sizeof(buf), "%08X  %s r%u, r%u, r%u",
                 address, mnemonic, instr.rD, instr.rA, instr.rB);
      }
      break;

    case PPCOpcodeType::kLoad:
      snprintf(buf, sizeof(buf), "%08X  %s r%u, %d(r%u)",
               address, mnemonic, instr.rD, instr.simm, instr.rA);
      break;

    case PPCOpcodeType::kStore:
      snprintf(buf, sizeof(buf), "%08X  %s r%u, %d(r%u)",
               address, mnemonic, instr.rS, instr.simm, instr.rA);
      break;

    case PPCOpcodeType::kBranch:
      if (instr.opcode == 18) {
        uint32_t target = instr.absolute ? instr.branch_offset :
                          address + instr.branch_offset;
        snprintf(buf, sizeof(buf), "%08X  %s 0x%08X",
                 address, mnemonic, target);
      } else {
        snprintf(buf, sizeof(buf), "%08X  %s %u,%u,0x%X",
                 address, mnemonic, instr.bo, instr.bi, instr.branch_offset);
      }
      break;

    case PPCOpcodeType::kSystem:
      snprintf(buf, sizeof(buf), "%08X  sc", address);
      break;

    default:
      snprintf(buf, sizeof(buf), "%08X  .word 0x%08X  ; %s",
               address, code, mnemonic);
      break;
  }

  return std::string(buf);
}

}  // namespace xe::cpu::frontend
