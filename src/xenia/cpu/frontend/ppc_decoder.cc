/**
 * Vera360 — Xenia Edge
 * PPC Instruction Decoder implementation
 */

#include "xenia/cpu/frontend/ppc_decoder.h"

namespace xe::cpu::frontend {

PPCInstruction DecodePPC(uint32_t address, uint32_t code) {
  PPCInstruction i = {};
  i.address = address;
  i.code = code;
  i.opcode = (code >> 26) & 0x3F;
  i.rD = (code >> 21) & 0x1F;
  i.rS = i.rD;
  i.rA = (code >> 16) & 0x1F;
  i.rB = (code >> 11) & 0x1F;
  i.rC = (code >> 6) & 0x1F;
  i.simm = static_cast<int16_t>(code & 0xFFFF);
  i.uimm = code & 0xFFFF;
  i.sh = (code >> 11) & 0x1F;
  i.mb = (code >> 6) & 0x1F;
  i.me = (code >> 1) & 0x1F;
  i.record = code & 1;
  i.link = code & 1;

  switch (i.opcode) {
    // Integer arithmetic immediate
    case 14: case 15: case 7: case 8:
    case 12: case 13:
      i.type = PPCOpcodeType::kInteger;
      break;

    // Integer logical immediate  
    case 24: case 25: case 26: case 27:
    case 28: case 29:
      i.type = PPCOpcodeType::kInteger;
      break;

    // Rotate/shift
    case 20: case 21: case 23:
      i.type = PPCOpcodeType::kInteger;
      break;

    // Load
    case 32: case 33: case 34: case 35:
    case 40: case 41: case 42: case 43:
    case 58:
      i.type = PPCOpcodeType::kLoad;
      break;

    // Store
    case 36: case 37: case 38: case 39:
    case 44: case 45: case 46: case 47:
    case 62:
      i.type = PPCOpcodeType::kStore;
      break;

    // Float load/store
    case 48: case 49: case 50: case 51:
    case 52: case 53: case 54: case 55:
      i.type = PPCOpcodeType::kFloat;
      break;

    // Branch
    case 18: {
      i.type = PPCOpcodeType::kBranch;
      int32_t li = code & 0x03FFFFFC;
      if (li & 0x02000000) li |= 0xFC000000;  // Sign extend
      i.branch_offset = li;
      i.absolute = (code >> 1) & 1;
      i.link = code & 1;
      break;
    }

    case 16: {
      i.type = PPCOpcodeType::kBranch;
      i.bo = (code >> 21) & 0x1F;
      i.bi = (code >> 16) & 0x1F;
      int32_t bd = code & 0xFFFC;
      if (bd & 0x8000) bd |= 0xFFFF0000;  // Sign extend
      i.branch_offset = bd;
      i.absolute = (code >> 1) & 1;
      i.link = code & 1;
      break;
    }

    case 19: {
      i.xo = (code >> 1) & 0x3FF;
      if (i.xo == 16 || i.xo == 528) {
        i.type = PPCOpcodeType::kBranch;
        i.bo = (code >> 21) & 0x1F;
        i.bi = (code >> 16) & 0x1F;
        i.link = code & 1;
      } else {
        i.type = PPCOpcodeType::kCR;
      }
      break;
    }

    case 31: {
      i.xo = (code >> 1) & 0x3FF;
      i.oe = (code >> 10) & 1;
      i.type = PPCOpcodeType::kInteger;
      break;
    }

    case 17:
      i.type = PPCOpcodeType::kSystem;
      break;

    case 4:
      i.type = PPCOpcodeType::kVector;
      break;

    case 10: case 11:
      i.type = PPCOpcodeType::kInteger;  // Compare
      break;

    case 59: case 63:
      i.type = PPCOpcodeType::kFloat;
      i.xo = (code >> 1) & 0x3FF;
      break;

    default:
      i.type = PPCOpcodeType::kInvalid;
      break;
  }

  return i;
}

bool IsReturn(const PPCInstruction& instr) {
  // bclr (blr): opcode=19, xo=16, BO=20 (unconditional)
  return instr.opcode == 19 && instr.xo == 16 && instr.bo == 20;
}

bool IsFunctionCall(const PPCInstruction& instr) {
  // bl: opcode=18 with LK=1
  return instr.opcode == 18 && instr.link;
}

bool IsUnconditionalBranch(const PPCInstruction& instr) {
  return instr.opcode == 18 && !instr.link;
}

const char* GetPPCMnemonic(const PPCInstruction& instr) {
  switch (instr.opcode) {
    case 14: return instr.rA == 0 ? "li" : "addi";
    case 15: return instr.rA == 0 ? "lis" : "addis";
    case 18: return instr.link ? "bl" : "b";
    case 16: return "bc";
    case 24: return "ori";
    case 25: return "oris";
    case 28: return "andi.";
    case 29: return "andis.";
    case 32: return "lwz";
    case 36: return "stw";
    case 34: return "lbz";
    case 38: return "stb";
    case 40: return "lhz";
    case 44: return "sth";
    case 48: return "lfs";
    case 50: return "lfd";
    case 52: return "stfs";
    case 54: return "stfd";
    case 17: return "sc";
    case 21: return "rlwinm";
    case 7:  return "mulli";
    case 11: return "cmpi";
    case 10: return "cmpli";
    default: return "???";
  }
}

}  // namespace xe::cpu::frontend
