/**
 * Vera360 — Xenia Edge
 * ARM64 Instruction Sequences — PPC → AArch64 lowering rules
 */
#pragma once

#include "xenia/cpu/backend/arm64/arm64_emitter.h"
#include <cstdint>

namespace xe::cpu::backend::arm64 {

/**
 * Translates individual PPC instructions to ARM64 instruction sequences.
 * Each PPC opcode maps to one or more ARM64 instructions.
 */
class ARM64Sequences {
 public:
  /// Main dispatch: emit ARM64 for one PPC instruction
  static bool Emit(ARM64Emitter& e, uint32_t guest_addr, uint32_t instr);

 private:
  // ── PPC instruction field extractors ──────────────────────────────────
  static uint32_t PPC_OPCD(uint32_t i) { return (i >> 26) & 0x3F; }
  static uint32_t PPC_RD(uint32_t i)   { return (i >> 21) & 0x1F; }
  static uint32_t PPC_RS(uint32_t i)   { return (i >> 21) & 0x1F; }
  static uint32_t PPC_RA(uint32_t i)   { return (i >> 16) & 0x1F; }
  static uint32_t PPC_RB(uint32_t i)   { return (i >> 11) & 0x1F; }
  static uint32_t PPC_RC(uint32_t i)   { return i & 1; }           // Record bit
  static int16_t  PPC_SIMM(uint32_t i) { return static_cast<int16_t>(i & 0xFFFF); }
  static uint16_t PPC_UIMM(uint32_t i) { return i & 0xFFFF; }
  static uint32_t PPC_XO_31(uint32_t i){ return (i >> 1) & 0x3FF; }
  static uint32_t PPC_XO_19(uint32_t i){ return (i >> 1) & 0x3FF; }
  static uint32_t PPC_BO(uint32_t i)   { return (i >> 21) & 0x1F; }
  static uint32_t PPC_BI(uint32_t i)   { return (i >> 16) & 0x1F; }
  static uint32_t PPC_SH(uint32_t i)   { return (i >> 11) & 0x1F; }
  static uint32_t PPC_MB(uint32_t i)   { return (i >> 6) & 0x1F; }
  static uint32_t PPC_ME(uint32_t i)   { return (i >> 1) & 0x1F; }

  // ── Opcode handlers ───────────────────────────────────────────────────

  // Integer arithmetic
  static bool Emit_ADDI(ARM64Emitter& e, uint32_t i);      // opcd=14
  static bool Emit_ADDIS(ARM64Emitter& e, uint32_t i);     // opcd=15
  static bool Emit_ADD_XO(ARM64Emitter& e, uint32_t i);    // opcd=31, xo=266
  static bool Emit_SUBF_XO(ARM64Emitter& e, uint32_t i);   // opcd=31, xo=40
  static bool Emit_MULLI(ARM64Emitter& e, uint32_t i);     // opcd=7
  static bool Emit_MULLW(ARM64Emitter& e, uint32_t i);     // opcd=31, xo=235
  static bool Emit_DIVW(ARM64Emitter& e, uint32_t i);      // opcd=31, xo=491
  static bool Emit_DIVWU(ARM64Emitter& e, uint32_t i);     // opcd=31, xo=459
  static bool Emit_NEG(ARM64Emitter& e, uint32_t i);       // opcd=31, xo=104

  // Integer logical
  static bool Emit_ORI(ARM64Emitter& e, uint32_t i);       // opcd=24
  static bool Emit_ORIS(ARM64Emitter& e, uint32_t i);      // opcd=25
  static bool Emit_ANDI(ARM64Emitter& e, uint32_t i);      // opcd=28
  static bool Emit_ANDIS(ARM64Emitter& e, uint32_t i);     // opcd=29
  static bool Emit_XORI(ARM64Emitter& e, uint32_t i);      // opcd=26
  static bool Emit_OR_XO(ARM64Emitter& e, uint32_t i);     // opcd=31, xo=444
  static bool Emit_AND_XO(ARM64Emitter& e, uint32_t i);    // opcd=31, xo=28
  static bool Emit_XOR_XO(ARM64Emitter& e, uint32_t i);    // opcd=31, xo=316

  // Shifts & rotates
  static bool Emit_RLWINM(ARM64Emitter& e, uint32_t i);    // opcd=21
  static bool Emit_RLWIMI(ARM64Emitter& e, uint32_t i);    // opcd=20
  static bool Emit_SLW(ARM64Emitter& e, uint32_t i);       // opcd=31, xo=24
  static bool Emit_SRW(ARM64Emitter& e, uint32_t i);       // opcd=31, xo=536
  static bool Emit_SRAW(ARM64Emitter& e, uint32_t i);      // opcd=31, xo=792
  static bool Emit_SRAWI(ARM64Emitter& e, uint32_t i);     // opcd=31, xo=824

  // Compare
  static bool Emit_CMPI(ARM64Emitter& e, uint32_t i);      // opcd=11
  static bool Emit_CMPLI(ARM64Emitter& e, uint32_t i);     // opcd=10
  static bool Emit_CMP_XO(ARM64Emitter& e, uint32_t i);    // opcd=31, xo=0
  static bool Emit_CMPL_XO(ARM64Emitter& e, uint32_t i);   // opcd=31, xo=32

  // Load
  static bool Emit_LBZ(ARM64Emitter& e, uint32_t i);       // opcd=34
  static bool Emit_LHZ(ARM64Emitter& e, uint32_t i);       // opcd=40
  static bool Emit_LWZ(ARM64Emitter& e, uint32_t i);       // opcd=32
  static bool Emit_LD(ARM64Emitter& e, uint32_t i);        // opcd=58

  // Store
  static bool Emit_STB(ARM64Emitter& e, uint32_t i);       // opcd=38
  static bool Emit_STH(ARM64Emitter& e, uint32_t i);       // opcd=44
  static bool Emit_STW(ARM64Emitter& e, uint32_t i);       // opcd=36
  static bool Emit_STD(ARM64Emitter& e, uint32_t i);       // opcd=62

  // Branch
  static bool Emit_B(ARM64Emitter& e, uint32_t i, uint32_t guest_addr);    // opcd=18
  static bool Emit_BC(ARM64Emitter& e, uint32_t i, uint32_t guest_addr);   // opcd=16
  static bool Emit_BCLR(ARM64Emitter& e, uint32_t i);      // opcd=19, xo=16
  static bool Emit_BCCTR(ARM64Emitter& e, uint32_t i);     // opcd=19, xo=528

  // System
  static bool Emit_SC(ARM64Emitter& e, uint32_t i);        // opcd=17 (syscall)
  static bool Emit_MFSPR(ARM64Emitter& e, uint32_t i);     // opcd=31, xo=339
  static bool Emit_MTSPR(ARM64Emitter& e, uint32_t i);     // opcd=31, xo=467
  static bool Emit_MFCR(ARM64Emitter& e, uint32_t i);      // opcd=31, xo=19
  static bool Emit_MTCRF(ARM64Emitter& e, uint32_t i);     // opcd=31, xo=144

  // Floating point
  static bool Emit_LFS(ARM64Emitter& e, uint32_t i);       // opcd=48
  static bool Emit_LFD(ARM64Emitter& e, uint32_t i);       // opcd=50
  static bool Emit_STFS(ARM64Emitter& e, uint32_t i);      // opcd=52
  static bool Emit_STFD(ARM64Emitter& e, uint32_t i);      // opcd=54

  // VMX128 (Xbox 360 specific SIMD) — opcd=4
  static bool Emit_VMX128(ARM64Emitter& e, uint32_t i);
};

}  // namespace xe::cpu::backend::arm64
