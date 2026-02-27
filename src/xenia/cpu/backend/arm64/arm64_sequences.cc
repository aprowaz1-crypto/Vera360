/**
 * Vera360 — Xenia Edge
 * ARM64 Instruction Sequences — PPC → AArch64 lowering implementation
 *
 * Each handler translates one PPC instruction to equivalent ARM64 code.
 * This is the heart of the JIT recompiler.
 */

#include "xenia/cpu/backend/arm64/arm64_sequences.h"
#include "xenia/cpu/backend/arm64/arm64_backend.h"
#include "xenia/base/logging.h"

namespace xe::cpu::backend::arm64 {

using R = RegisterAllocation;

/// Map PPC GPR index to ARM64 register.
/// Hot GPRs (3-12) are mapped directly, others go through context spill.
static Reg MapGPR(ARM64Emitter& e, uint32_t ppc_reg) {
  if (ppc_reg >= 3 && ppc_reg <= 12) {
    return R::kPpcGpr[ppc_reg - 3];
  }
  // Cold register: load from context struct
  // Context layout: offset = ppc_reg * 8 (64-bit per GPR)
  e.LDR(R::kScratch0, R::kContextPtr, static_cast<int32_t>(ppc_reg * 8));
  return R::kScratch0;
}

static void StoreGPR(ARM64Emitter& e, uint32_t ppc_reg, Reg value) {
  if (ppc_reg >= 3 && ppc_reg <= 12) {
    if (value != R::kPpcGpr[ppc_reg - 3]) {
      e.MOV(R::kPpcGpr[ppc_reg - 3], value);
    }
    return;
  }
  // Spill to context
  e.STR(value, R::kContextPtr, static_cast<int32_t>(ppc_reg * 8));
}

/// Compute effective address: guest_base + (rA|0 + offset)
static void EmitEA(ARM64Emitter& e, uint32_t rA, int16_t offset) {
  if (rA == 0) {
    // EA = offset (absolute)
    if (offset >= 0) {
      e.ADD_imm(R::kScratch2, R::kGuestMemBase, static_cast<uint32_t>(offset));
    } else {
      e.MOV_imm(R::kScratch2, static_cast<uint64_t>(static_cast<int64_t>(offset) & 0xFFFFFFFF));
      e.ADD(R::kScratch2, R::kGuestMemBase, R::kScratch2);
    }
  } else {
    Reg rA_arm = MapGPR(e, rA);
    // EA = guest_base + rA + offset
    e.ADD(R::kScratch2, R::kGuestMemBase, rA_arm);
    if (offset > 0) {
      e.ADD_imm(R::kScratch2, R::kScratch2, static_cast<uint32_t>(offset));
    } else if (offset < 0) {
      e.SUB_imm(R::kScratch2, R::kScratch2, static_cast<uint32_t>(-offset));
    }
  }
}

// ── Main dispatcher ─────────────────────────────────────────────────────────

bool ARM64Sequences::Emit(ARM64Emitter& e, uint32_t guest_addr, uint32_t instr) {
  uint32_t opcd = PPC_OPCD(instr);

  switch (opcd) {
    // Integer arithmetic immediate
    case 14: return Emit_ADDI(e, instr);
    case 15: return Emit_ADDIS(e, instr);
    case 7:  return Emit_MULLI(e, instr);

    // Integer logical immediate
    case 24: return Emit_ORI(e, instr);
    case 25: return Emit_ORIS(e, instr);
    case 28: return Emit_ANDI(e, instr);
    case 29: return Emit_ANDIS(e, instr);
    case 26: return Emit_XORI(e, instr);

    // Rotate/shift immediate
    case 21: return Emit_RLWINM(e, instr);
    case 20: return Emit_RLWIMI(e, instr);

    // Compare immediate
    case 11: return Emit_CMPI(e, instr);
    case 10: return Emit_CMPLI(e, instr);

    // Load
    case 34: return Emit_LBZ(e, instr);
    case 40: return Emit_LHZ(e, instr);
    case 32: return Emit_LWZ(e, instr);
    case 58: return Emit_LD(e, instr);

    // Store
    case 38: return Emit_STB(e, instr);
    case 44: return Emit_STH(e, instr);
    case 36: return Emit_STW(e, instr);
    case 62: return Emit_STD(e, instr);

    // Branch
    case 18: return Emit_B(e, instr, guest_addr);
    case 16: return Emit_BC(e, instr, guest_addr);

    // Floating point load/store
    case 48: return Emit_LFS(e, instr);
    case 50: return Emit_LFD(e, instr);
    case 52: return Emit_STFS(e, instr);
    case 54: return Emit_STFD(e, instr);

    // System call
    case 17: return Emit_SC(e, instr);

    // VMX128 
    case 4: return Emit_VMX128(e, instr);

    // Extended opcodes
    case 31: {
      uint32_t xo = PPC_XO_31(instr);
      switch (xo) {
        case 266: return Emit_ADD_XO(e, instr);
        case 40:  return Emit_SUBF_XO(e, instr);
        case 235: return Emit_MULLW(e, instr);
        case 491: return Emit_DIVW(e, instr);
        case 459: return Emit_DIVWU(e, instr);
        case 104: return Emit_NEG(e, instr);
        case 444: return Emit_OR_XO(e, instr);
        case 28:  return Emit_AND_XO(e, instr);
        case 316: return Emit_XOR_XO(e, instr);
        case 24:  return Emit_SLW(e, instr);
        case 536: return Emit_SRW(e, instr);
        case 792: return Emit_SRAW(e, instr);
        case 824: return Emit_SRAWI(e, instr);
        case 0:   return Emit_CMP_XO(e, instr);
        case 32:  return Emit_CMPL_XO(e, instr);
        case 339: return Emit_MFSPR(e, instr);
        case 467: return Emit_MTSPR(e, instr);
        case 19:  return Emit_MFCR(e, instr);
        case 144: return Emit_MTCRF(e, instr);
        default:
          XELOGW("Unimplemented PPC XO-31 opcode: xo={}", xo);
          e.NOP();
          return true;
      }
    }

    case 19: {
      uint32_t xo = PPC_XO_19(instr);
      switch (xo) {
        case 16:  return Emit_BCLR(e, instr);
        case 528: return Emit_BCCTR(e, instr);
        default:
          e.NOP();
          return true;
      }
    }

    default:
      XELOGW("Unimplemented PPC opcode: {}", opcd);
      e.NOP();
      return true;
  }
}

// ── Integer Arithmetic ──────────────────────────────────────────────────────

bool ARM64Sequences::Emit_ADDI(ARM64Emitter& e, uint32_t i) {
  // rD = (rA|0) + SIMM
  uint32_t rD = PPC_RD(i);
  uint32_t rA = PPC_RA(i);
  int16_t simm = PPC_SIMM(i);

  if (rA == 0) {
    // li rD, simm  → MOV Xd, #simm
    e.MOV_imm(R::kScratch0, static_cast<uint64_t>(static_cast<int64_t>(simm) & 0xFFFFFFFF));
  } else {
    Reg src = MapGPR(e, rA);
    if (simm >= 0 && simm < 4096) {
      e.ADD_imm(R::kScratch0, src, static_cast<uint32_t>(simm));
    } else if (simm < 0 && (-simm) < 4096) {
      e.SUB_imm(R::kScratch0, src, static_cast<uint32_t>(-simm));
    } else {
      e.MOV_imm(R::kScratch1, static_cast<uint64_t>(static_cast<int64_t>(simm) & 0xFFFFFFFF));
      e.ADD(R::kScratch0, src, R::kScratch1);
    }
  }
  StoreGPR(e, rD, R::kScratch0);
  return true;
}

bool ARM64Sequences::Emit_ADDIS(ARM64Emitter& e, uint32_t i) {
  uint32_t rD = PPC_RD(i);
  uint32_t rA = PPC_RA(i);
  int32_t simm = static_cast<int32_t>(PPC_SIMM(i)) << 16;

  if (rA == 0) {
    e.MOV_imm(R::kScratch0, static_cast<uint64_t>(static_cast<int64_t>(simm) & 0xFFFFFFFF));
  } else {
    Reg src = MapGPR(e, rA);
    e.MOV_imm(R::kScratch1, static_cast<uint64_t>(static_cast<int64_t>(simm) & 0xFFFFFFFF));
    e.ADD(R::kScratch0, src, R::kScratch1);
  }
  StoreGPR(e, rD, R::kScratch0);
  return true;
}

bool ARM64Sequences::Emit_ADD_XO(ARM64Emitter& e, uint32_t i) {
  uint32_t rD = PPC_RD(i);
  uint32_t rA = PPC_RA(i);
  uint32_t rB = PPC_RB(i);
  
  Reg a = MapGPR(e, rA);
  Reg b = MapGPR(e, rB);
  e.ADD(R::kScratch0, a, b);
  StoreGPR(e, rD, R::kScratch0);
  return true;
}

bool ARM64Sequences::Emit_SUBF_XO(ARM64Emitter& e, uint32_t i) {
  uint32_t rD = PPC_RD(i);
  uint32_t rA = PPC_RA(i);
  uint32_t rB = PPC_RB(i);
  
  Reg a = MapGPR(e, rA);
  Reg b = MapGPR(e, rB);
  // subf: rD = rB - rA (note order!)
  e.SUB(R::kScratch0, b, a);
  StoreGPR(e, rD, R::kScratch0);
  return true;
}

bool ARM64Sequences::Emit_MULLI(ARM64Emitter& e, uint32_t i) {
  uint32_t rD = PPC_RD(i);
  uint32_t rA = PPC_RA(i);
  int16_t simm = PPC_SIMM(i);

  Reg a = MapGPR(e, rA);
  e.MOV_imm(R::kScratch1, static_cast<uint64_t>(static_cast<int64_t>(simm) & 0xFFFFFFFF));
  e.MUL(R::kScratch0, a, R::kScratch1);
  StoreGPR(e, rD, R::kScratch0);
  return true;
}

bool ARM64Sequences::Emit_MULLW(ARM64Emitter& e, uint32_t i) {
  uint32_t rD = PPC_RD(i);
  Reg a = MapGPR(e, PPC_RA(i));
  Reg b = MapGPR(e, PPC_RB(i));
  e.MUL(R::kScratch0, a, b);
  StoreGPR(e, rD, R::kScratch0);
  return true;
}

bool ARM64Sequences::Emit_DIVW(ARM64Emitter& e, uint32_t i) {
  uint32_t rD = PPC_RD(i);
  Reg a = MapGPR(e, PPC_RA(i));
  Reg b = MapGPR(e, PPC_RB(i));
  e.SDIV(R::kScratch0, a, b);
  StoreGPR(e, rD, R::kScratch0);
  return true;
}

bool ARM64Sequences::Emit_DIVWU(ARM64Emitter& e, uint32_t i) {
  uint32_t rD = PPC_RD(i);
  Reg a = MapGPR(e, PPC_RA(i));
  Reg b = MapGPR(e, PPC_RB(i));
  e.UDIV(R::kScratch0, a, b);
  StoreGPR(e, rD, R::kScratch0);
  return true;
}

bool ARM64Sequences::Emit_NEG(ARM64Emitter& e, uint32_t i) {
  uint32_t rD = PPC_RD(i);
  Reg a = MapGPR(e, PPC_RA(i));
  e.SUB(R::kScratch0, Reg::XZR, a);
  StoreGPR(e, rD, R::kScratch0);
  return true;
}

// ── Integer Logical ─────────────────────────────────────────────────────────

bool ARM64Sequences::Emit_ORI(ARM64Emitter& e, uint32_t i) {
  uint32_t rS = PPC_RS(i);
  uint32_t rA = PPC_RA(i);
  uint16_t uimm = PPC_UIMM(i);

  if (uimm == 0) {
    // nop or mr
    Reg s = MapGPR(e, rS);
    StoreGPR(e, rA, s);
    return true;
  }

  Reg s = MapGPR(e, rS);
  e.MOV_imm(R::kScratch1, uimm);
  e.ORR(R::kScratch0, s, R::kScratch1);
  StoreGPR(e, rA, R::kScratch0);
  return true;
}

bool ARM64Sequences::Emit_ORIS(ARM64Emitter& e, uint32_t i) {
  uint32_t rS = PPC_RS(i);
  uint32_t rA = PPC_RA(i);
  uint32_t uimm = static_cast<uint32_t>(PPC_UIMM(i)) << 16;

  Reg s = MapGPR(e, rS);
  e.MOV_imm(R::kScratch1, uimm);
  e.ORR(R::kScratch0, s, R::kScratch1);
  StoreGPR(e, rA, R::kScratch0);
  return true;
}

bool ARM64Sequences::Emit_ANDI(ARM64Emitter& e, uint32_t i) {
  uint32_t rS = PPC_RS(i);
  uint32_t rA = PPC_RA(i);
  uint16_t uimm = PPC_UIMM(i);

  Reg s = MapGPR(e, rS);
  e.MOV_imm(R::kScratch1, uimm);
  e.AND(R::kScratch0, s, R::kScratch1);
  StoreGPR(e, rA, R::kScratch0);
  return true;
}

bool ARM64Sequences::Emit_ANDIS(ARM64Emitter& e, uint32_t i) {
  uint32_t rS = PPC_RS(i);
  uint32_t rA = PPC_RA(i);
  uint32_t uimm = static_cast<uint32_t>(PPC_UIMM(i)) << 16;

  Reg s = MapGPR(e, rS);
  e.MOV_imm(R::kScratch1, uimm);
  e.AND(R::kScratch0, s, R::kScratch1);
  StoreGPR(e, rA, R::kScratch0);
  return true;
}

bool ARM64Sequences::Emit_XORI(ARM64Emitter& e, uint32_t i) {
  uint32_t rS = PPC_RS(i);
  uint32_t rA = PPC_RA(i);
  uint16_t uimm = PPC_UIMM(i);

  Reg s = MapGPR(e, rS);
  e.MOV_imm(R::kScratch1, uimm);
  e.EOR(R::kScratch0, s, R::kScratch1);
  StoreGPR(e, rA, R::kScratch0);
  return true;
}

bool ARM64Sequences::Emit_OR_XO(ARM64Emitter& e, uint32_t i) {
  uint32_t rA = PPC_RA(i);
  Reg s = MapGPR(e, PPC_RS(i));
  Reg b = MapGPR(e, PPC_RB(i));
  e.ORR(R::kScratch0, s, b);
  StoreGPR(e, rA, R::kScratch0);
  return true;
}

bool ARM64Sequences::Emit_AND_XO(ARM64Emitter& e, uint32_t i) {
  uint32_t rA = PPC_RA(i);
  Reg s = MapGPR(e, PPC_RS(i));
  Reg b = MapGPR(e, PPC_RB(i));
  e.AND(R::kScratch0, s, b);
  StoreGPR(e, rA, R::kScratch0);
  return true;
}

bool ARM64Sequences::Emit_XOR_XO(ARM64Emitter& e, uint32_t i) {
  uint32_t rA = PPC_RA(i);
  Reg s = MapGPR(e, PPC_RS(i));
  Reg b = MapGPR(e, PPC_RB(i));
  e.EOR(R::kScratch0, s, b);
  StoreGPR(e, rA, R::kScratch0);
  return true;
}

// ── Shifts & Rotates ────────────────────────────────────────────────────────

bool ARM64Sequences::Emit_RLWINM(ARM64Emitter& e, uint32_t i) {
  // rlwinm rA, rS, SH, MB, ME
  // Result = ROTL32(rS, SH) & MASK(MB, ME)
  uint32_t rA = PPC_RA(i);
  Reg s = MapGPR(e, PPC_RS(i));
  uint32_t sh = PPC_SH(i);
  uint32_t mb = PPC_MB(i);
  uint32_t me = PPC_ME(i);

  // Rotate left by sh using ROR (32-sh for 32-bit rotate)
  if (sh != 0) {
    e.MOV_imm(R::kScratch1, 32 - sh);
    e.ROR_reg(R::kScratch0, s, R::kScratch1);
  } else {
    e.MOV(R::kScratch0, s);
  }

  // Apply mask
  uint32_t mask = 0;
  if (mb <= me) {
    for (uint32_t bit = mb; bit <= me; ++bit) {
      mask |= (1u << (31 - bit));
    }
  } else {
    mask = 0xFFFFFFFF;
    for (uint32_t bit = me + 1; bit < mb; ++bit) {
      mask &= ~(1u << (31 - bit));
    }
  }

  e.MOV_imm(R::kScratch1, mask);
  e.AND(R::kScratch0, R::kScratch0, R::kScratch1);
  StoreGPR(e, rA, R::kScratch0);
  return true;
}

bool ARM64Sequences::Emit_RLWIMI(ARM64Emitter& e, uint32_t i) {
  // rlwimi rA, rS, SH, MB, ME — insert under mask
  uint32_t rA_idx = PPC_RA(i);
  Reg s = MapGPR(e, PPC_RS(i));
  Reg a = MapGPR(e, rA_idx);
  uint32_t sh = PPC_SH(i);
  uint32_t mb = PPC_MB(i);
  uint32_t me = PPC_ME(i);

  // Rotate source
  if (sh != 0) {
    e.MOV_imm(R::kScratch1, 32 - sh);
    e.ROR_reg(R::kScratch0, s, R::kScratch1);
  } else {
    e.MOV(R::kScratch0, s);
  }

  // Build mask
  uint32_t mask = 0;
  if (mb <= me) {
    for (uint32_t bit = mb; bit <= me; ++bit) mask |= (1u << (31 - bit));
  } else {
    mask = 0xFFFFFFFF;
    for (uint32_t bit = me + 1; bit < mb; ++bit) mask &= ~(1u << (31 - bit));
  }

  // rA = (rotated & mask) | (rA & ~mask)
  e.MOV_imm(R::kScratch1, mask);
  e.AND(R::kScratch0, R::kScratch0, R::kScratch1);  // rotated & mask
  e.MOV_imm(R::kScratch1, ~mask);
  e.AND(R::kScratch3, a, R::kScratch1);              // rA & ~mask
  e.ORR(R::kScratch0, R::kScratch0, R::kScratch3);
  StoreGPR(e, rA_idx, R::kScratch0);
  return true;
}

bool ARM64Sequences::Emit_SLW(ARM64Emitter& e, uint32_t i) {
  uint32_t rA = PPC_RA(i);
  Reg s = MapGPR(e, PPC_RS(i));
  Reg b = MapGPR(e, PPC_RB(i));
  e.LSL_reg(R::kScratch0, s, b);
  StoreGPR(e, rA, R::kScratch0);
  return true;
}

bool ARM64Sequences::Emit_SRW(ARM64Emitter& e, uint32_t i) {
  uint32_t rA = PPC_RA(i);
  Reg s = MapGPR(e, PPC_RS(i));
  Reg b = MapGPR(e, PPC_RB(i));
  e.LSR_reg(R::kScratch0, s, b);
  StoreGPR(e, rA, R::kScratch0);
  return true;
}

bool ARM64Sequences::Emit_SRAW(ARM64Emitter& e, uint32_t i) {
  uint32_t rA = PPC_RA(i);
  Reg s = MapGPR(e, PPC_RS(i));
  Reg b = MapGPR(e, PPC_RB(i));
  e.ASR_reg(R::kScratch0, s, b);
  StoreGPR(e, rA, R::kScratch0);
  return true;
}

bool ARM64Sequences::Emit_SRAWI(ARM64Emitter& e, uint32_t i) {
  uint32_t rA = PPC_RA(i);
  Reg s = MapGPR(e, PPC_RS(i));
  uint32_t sh = PPC_SH(i);
  e.MOV_imm(R::kScratch1, sh);
  e.ASR_reg(R::kScratch0, s, R::kScratch1);
  StoreGPR(e, rA, R::kScratch0);
  return true;
}

// ── Compare ─────────────────────────────────────────────────────────────────

bool ARM64Sequences::Emit_CMPI(ARM64Emitter& e, uint32_t i) {
  Reg a = MapGPR(e, PPC_RA(i));
  int16_t simm = PPC_SIMM(i);
  if (simm >= 0 && simm < 4096) {
    e.CMP_imm(a, static_cast<uint32_t>(simm));
  } else {
    e.MOV_imm(R::kScratch1, static_cast<uint64_t>(static_cast<int64_t>(simm) & 0xFFFFFFFF));
    e.CMP(a, R::kScratch1);
  }
  // TODO: Store CR field from NZCV flags
  return true;
}

bool ARM64Sequences::Emit_CMPLI(ARM64Emitter& e, uint32_t i) {
  Reg a = MapGPR(e, PPC_RA(i));
  uint16_t uimm = PPC_UIMM(i);
  if (uimm < 4096) {
    e.CMP_imm(a, uimm);
  } else {
    e.MOV_imm(R::kScratch1, uimm);
    e.CMP(a, R::kScratch1);
  }
  return true;
}

bool ARM64Sequences::Emit_CMP_XO(ARM64Emitter& e, uint32_t i) {
  Reg a = MapGPR(e, PPC_RA(i));
  Reg b = MapGPR(e, PPC_RB(i));
  e.CMP(a, b);
  return true;
}

bool ARM64Sequences::Emit_CMPL_XO(ARM64Emitter& e, uint32_t i) {
  Reg a = MapGPR(e, PPC_RA(i));
  Reg b = MapGPR(e, PPC_RB(i));
  e.CMP(a, b);
  return true;
}

// ── Load ────────────────────────────────────────────────────────────────────

bool ARM64Sequences::Emit_LBZ(ARM64Emitter& e, uint32_t i) {
  uint32_t rD = PPC_RD(i);
  EmitEA(e, PPC_RA(i), PPC_SIMM(i));
  e.LDRB(R::kScratch0, R::kScratch2, 0);
  StoreGPR(e, rD, R::kScratch0);
  return true;
}

bool ARM64Sequences::Emit_LHZ(ARM64Emitter& e, uint32_t i) {
  uint32_t rD = PPC_RD(i);
  EmitEA(e, PPC_RA(i), PPC_SIMM(i));
  e.LDRH(R::kScratch0, R::kScratch2, 0);
  // Byte-swap for big-endian guest → little-endian host
  e.REV16(R::kScratch0, R::kScratch0);
  StoreGPR(e, rD, R::kScratch0);
  return true;
}

bool ARM64Sequences::Emit_LWZ(ARM64Emitter& e, uint32_t i) {
  uint32_t rD = PPC_RD(i);
  EmitEA(e, PPC_RA(i), PPC_SIMM(i));
  e.LDRW(R::kScratch0, R::kScratch2, 0);
  e.REV32(R::kScratch0, R::kScratch0);  // Endian swap
  StoreGPR(e, rD, R::kScratch0);
  return true;
}

bool ARM64Sequences::Emit_LD(ARM64Emitter& e, uint32_t i) {
  uint32_t rD = PPC_RD(i);
  EmitEA(e, PPC_RA(i), PPC_SIMM(i));
  e.LDR(R::kScratch0, R::kScratch2, 0);
  e.REV(R::kScratch0, R::kScratch0);  // 64-bit endian swap
  StoreGPR(e, rD, R::kScratch0);
  return true;
}

// ── Store ───────────────────────────────────────────────────────────────────

bool ARM64Sequences::Emit_STB(ARM64Emitter& e, uint32_t i) {
  Reg s = MapGPR(e, PPC_RS(i));
  EmitEA(e, PPC_RA(i), PPC_SIMM(i));
  e.STRB(s, R::kScratch2, 0);
  return true;
}

bool ARM64Sequences::Emit_STH(ARM64Emitter& e, uint32_t i) {
  Reg s = MapGPR(e, PPC_RS(i));
  e.REV16(R::kScratch0, s);
  EmitEA(e, PPC_RA(i), PPC_SIMM(i));
  e.STRH(R::kScratch0, R::kScratch2, 0);
  return true;
}

bool ARM64Sequences::Emit_STW(ARM64Emitter& e, uint32_t i) {
  Reg s = MapGPR(e, PPC_RS(i));
  e.REV32(R::kScratch0, s);
  EmitEA(e, PPC_RA(i), PPC_SIMM(i));
  e.STRW(R::kScratch0, R::kScratch2, 0);
  return true;
}

bool ARM64Sequences::Emit_STD(ARM64Emitter& e, uint32_t i) {
  Reg s = MapGPR(e, PPC_RS(i));
  e.REV(R::kScratch0, s);
  EmitEA(e, PPC_RA(i), PPC_SIMM(i));
  e.STR(R::kScratch0, R::kScratch2, 0);
  return true;
}

// ── Branch ──────────────────────────────────────────────────────────────────

bool ARM64Sequences::Emit_B(ARM64Emitter& e, uint32_t i, uint32_t guest_addr) {
  // For now, emit a NOP placeholder — real branching requires block linking
  // TODO: Implement block dispatcher
  (void)guest_addr;
  e.NOP();
  return true;
}

bool ARM64Sequences::Emit_BC(ARM64Emitter& e, uint32_t i, uint32_t guest_addr) {
  (void)guest_addr;
  e.NOP();  // TODO
  return true;
}

bool ARM64Sequences::Emit_BCLR(ARM64Emitter& e, uint32_t i) {
  // blr → return to caller (function return)
  // In our JIT model, this ends the function
  e.NOP();  // Epilogue handles actual return
  return true;
}

bool ARM64Sequences::Emit_BCCTR(ARM64Emitter& e, uint32_t i) {
  e.NOP();  // TODO: CTR-based branch
  return true;
}

// ── System ──────────────────────────────────────────────────────────────────

bool ARM64Sequences::Emit_SC(ARM64Emitter& e, uint32_t i) {
  // Xbox 360 syscall — trap to host handler
  // We use BRK to signal the emulator to handle the syscall
  e.BRK(0xE360);  // Custom trap code for Xbox 360 syscall
  return true;
}

bool ARM64Sequences::Emit_MFSPR(ARM64Emitter& e, uint32_t i) {
  // Move from SPR — load from context
  uint32_t rD = PPC_RD(i);
  uint32_t spr = ((PPC_RA(i) & 0x1F) << 5) | (PPC_RB(i) & 0x1F);
  
  // SPR offset in context: 256 (after 32 GPRs * 8 bytes)
  uint32_t offset = 256 + spr * 8;
  e.LDR(R::kScratch0, R::kContextPtr, static_cast<int32_t>(offset));
  StoreGPR(e, rD, R::kScratch0);
  return true;
}

bool ARM64Sequences::Emit_MTSPR(ARM64Emitter& e, uint32_t i) {
  uint32_t rS = PPC_RS(i);
  uint32_t spr = ((PPC_RA(i) & 0x1F) << 5) | (PPC_RB(i) & 0x1F);
  
  Reg s = MapGPR(e, rS);
  uint32_t offset = 256 + spr * 8;
  e.STR(s, R::kContextPtr, static_cast<int32_t>(offset));
  return true;
}

bool ARM64Sequences::Emit_MFCR(ARM64Emitter& e, uint32_t i) {
  uint32_t rD = PPC_RD(i);
  // CR is stored at context offset 512
  e.LDR(R::kScratch0, R::kContextPtr, 512);
  StoreGPR(e, rD, R::kScratch0);
  return true;
}

bool ARM64Sequences::Emit_MTCRF(ARM64Emitter& e, uint32_t i) {
  Reg s = MapGPR(e, PPC_RS(i));
  e.STR(s, R::kContextPtr, 512);
  return true;
}

// ── Floating Point ──────────────────────────────────────────────────────────

bool ARM64Sequences::Emit_LFS(ARM64Emitter& e, uint32_t i) {
  EmitEA(e, PPC_RA(i), PPC_SIMM(i));
  e.LDRW(R::kScratch0, R::kScratch2, 0);
  e.REV32(R::kScratch0, R::kScratch0);
  // Store to FPR in context (offset 1024 + fpr * 8)
  uint32_t frd = PPC_RD(i);
  e.STR(R::kScratch0, R::kContextPtr, static_cast<int32_t>(1024 + frd * 8));
  return true;
}

bool ARM64Sequences::Emit_LFD(ARM64Emitter& e, uint32_t i) {
  EmitEA(e, PPC_RA(i), PPC_SIMM(i));
  e.LDR(R::kScratch0, R::kScratch2, 0);
  e.REV(R::kScratch0, R::kScratch0);
  uint32_t frd = PPC_RD(i);
  e.STR(R::kScratch0, R::kContextPtr, static_cast<int32_t>(1024 + frd * 8));
  return true;
}

bool ARM64Sequences::Emit_STFS(ARM64Emitter& e, uint32_t i) {
  uint32_t frs = PPC_RS(i);
  e.LDR(R::kScratch0, R::kContextPtr, static_cast<int32_t>(1024 + frs * 8));
  e.REV32(R::kScratch0, R::kScratch0);
  EmitEA(e, PPC_RA(i), PPC_SIMM(i));
  e.STRW(R::kScratch0, R::kScratch2, 0);
  return true;
}

bool ARM64Sequences::Emit_STFD(ARM64Emitter& e, uint32_t i) {
  uint32_t frs = PPC_RS(i);
  e.LDR(R::kScratch0, R::kContextPtr, static_cast<int32_t>(1024 + frs * 8));
  e.REV(R::kScratch0, R::kScratch0);
  EmitEA(e, PPC_RA(i), PPC_SIMM(i));
  e.STR(R::kScratch0, R::kScratch2, 0);
  return true;
}

// ── VMX128 (Xbox 360 SIMD) ─────────────────────────────────────────────────

bool ARM64Sequences::Emit_VMX128(ARM64Emitter& e, uint32_t i) {
  // VMX128 opcode decoding — placeholder for Xbox 360 specific SIMD
  // These will be lowered to ARM64 NEON instructions
  // TODO: Full VMX128 instruction set implementation
  e.NOP();
  return true;
}

}  // namespace xe::cpu::backend::arm64
