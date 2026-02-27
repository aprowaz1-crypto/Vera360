/**
 * Vera360 — Xenia Edge
 * ARM64 Instruction Sequences — PPC → AArch64 lowering rules
 * Full PPC instruction set coverage for Xbox 360 emulation.
 */
#pragma once

#include "xenia/cpu/backend/arm64/arm64_emitter.h"
#include <cstdint>

namespace xe::cpu::backend::arm64 {

/**
 * Translates individual PPC instructions to ARM64 instruction sequences.
 * Covers: integer, FP, VMX128, load/store, branch, compare, rotate, shift,
 * condition register, system, and atomic operations.
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
  static uint32_t PPC_RC(uint32_t i)   { return i & 1; }
  static int16_t  PPC_SIMM(uint32_t i) { return static_cast<int16_t>(i & 0xFFFF); }
  static uint16_t PPC_UIMM(uint32_t i) { return i & 0xFFFF; }
  static uint32_t PPC_XO_31(uint32_t i){ return (i >> 1) & 0x3FF; }
  static uint32_t PPC_XO_19(uint32_t i){ return (i >> 1) & 0x3FF; }
  static uint32_t PPC_XO_59(uint32_t i){ return (i >> 1) & 0x1F; }  // opcd=59 short XO
  static uint32_t PPC_XO_63(uint32_t i){ return (i >> 1) & 0x3FF; } // opcd=63 full XO
  static uint32_t PPC_XO_63s(uint32_t i){return (i >> 1) & 0x1F; }  // opcd=63 short XO
  static uint32_t PPC_BO(uint32_t i)   { return (i >> 21) & 0x1F; }
  static uint32_t PPC_BI(uint32_t i)   { return (i >> 16) & 0x1F; }
  static uint32_t PPC_SH(uint32_t i)   { return (i >> 11) & 0x1F; }
  static uint32_t PPC_MB(uint32_t i)   { return (i >> 6) & 0x1F; }
  static uint32_t PPC_ME(uint32_t i)   { return (i >> 1) & 0x1F; }
  static uint32_t PPC_CRF(uint32_t i)  { return (i >> 23) & 0x7; } // CR field
  static uint32_t PPC_FRT(uint32_t i)  { return (i >> 21) & 0x1F; }
  static uint32_t PPC_FRA(uint32_t i)  { return (i >> 16) & 0x1F; }
  static uint32_t PPC_FRB(uint32_t i)  { return (i >> 11) & 0x1F; }
  static uint32_t PPC_FRC(uint32_t i)  { return (i >> 6) & 0x1F; }
  static uint32_t PPC_OE(uint32_t i)   { return (i >> 10) & 1; }
  static uint32_t PPC_L(uint32_t i)    { return (i >> 21) & 1; }  // Compare L bit

  // Helper: build PPC rotate mask
  static uint32_t BuildMask(uint32_t mb, uint32_t me);

  // ── Integer arithmetic ────────────────────────────────────────────────
  static bool Emit_ADDI(ARM64Emitter& e, uint32_t i);
  static bool Emit_ADDIS(ARM64Emitter& e, uint32_t i);
  static bool Emit_ADD_XO(ARM64Emitter& e, uint32_t i);
  static bool Emit_ADDC_XO(ARM64Emitter& e, uint32_t i);   // xo=10
  static bool Emit_ADDE_XO(ARM64Emitter& e, uint32_t i);   // xo=138
  static bool Emit_ADDZE_XO(ARM64Emitter& e, uint32_t i);  // xo=202
  static bool Emit_ADDME_XO(ARM64Emitter& e, uint32_t i);  // xo=234
  static bool Emit_SUBF_XO(ARM64Emitter& e, uint32_t i);
  static bool Emit_SUBFC_XO(ARM64Emitter& e, uint32_t i);  // xo=8
  static bool Emit_SUBFE_XO(ARM64Emitter& e, uint32_t i);  // xo=136
  static bool Emit_SUBFZE_XO(ARM64Emitter& e, uint32_t i); // xo=200
  static bool Emit_SUBFME_XO(ARM64Emitter& e, uint32_t i); // xo=232
  static bool Emit_SUBFIC(ARM64Emitter& e, uint32_t i);    // opcd=8
  static bool Emit_MULLI(ARM64Emitter& e, uint32_t i);
  static bool Emit_MULLW(ARM64Emitter& e, uint32_t i);
  static bool Emit_MULHW(ARM64Emitter& e, uint32_t i);     // xo=75
  static bool Emit_MULHWU(ARM64Emitter& e, uint32_t i);    // xo=11
  static bool Emit_DIVW(ARM64Emitter& e, uint32_t i);
  static bool Emit_DIVWU(ARM64Emitter& e, uint32_t i);
  static bool Emit_NEG(ARM64Emitter& e, uint32_t i);
  static bool Emit_MULLD(ARM64Emitter& e, uint32_t i);     // xo=233
  static bool Emit_DIVD(ARM64Emitter& e, uint32_t i);      // xo=489
  static bool Emit_DIVDU(ARM64Emitter& e, uint32_t i);     // xo=457

  // ── Integer logical ───────────────────────────────────────────────────
  static bool Emit_ORI(ARM64Emitter& e, uint32_t i);
  static bool Emit_ORIS(ARM64Emitter& e, uint32_t i);
  static bool Emit_ANDI(ARM64Emitter& e, uint32_t i);
  static bool Emit_ANDIS(ARM64Emitter& e, uint32_t i);
  static bool Emit_XORI(ARM64Emitter& e, uint32_t i);
  static bool Emit_XORIS(ARM64Emitter& e, uint32_t i);    // opcd=27
  static bool Emit_OR_XO(ARM64Emitter& e, uint32_t i);
  static bool Emit_AND_XO(ARM64Emitter& e, uint32_t i);
  static bool Emit_XOR_XO(ARM64Emitter& e, uint32_t i);
  static bool Emit_NOR_XO(ARM64Emitter& e, uint32_t i);    // xo=124
  static bool Emit_NAND_XO(ARM64Emitter& e, uint32_t i);   // xo=476
  static bool Emit_EQV_XO(ARM64Emitter& e, uint32_t i);    // xo=284
  static bool Emit_ANDC_XO(ARM64Emitter& e, uint32_t i);   // xo=60
  static bool Emit_ORC_XO(ARM64Emitter& e, uint32_t i);    // xo=412
  static bool Emit_EXTSB_XO(ARM64Emitter& e, uint32_t i);  // xo=954
  static bool Emit_EXTSH_XO(ARM64Emitter& e, uint32_t i);  // xo=922
  static bool Emit_EXTSW_XO(ARM64Emitter& e, uint32_t i);  // xo=986
  static bool Emit_CNTLZW_XO(ARM64Emitter& e, uint32_t i); // xo=26
  static bool Emit_CNTLZD_XO(ARM64Emitter& e, uint32_t i); // xo=58

  // ── Shifts & rotates (32-bit) ─────────────────────────────────────────
  static bool Emit_RLWINM(ARM64Emitter& e, uint32_t i);
  static bool Emit_RLWIMI(ARM64Emitter& e, uint32_t i);
  static bool Emit_RLWNM(ARM64Emitter& e, uint32_t i);    // opcd=23
  static bool Emit_SLW(ARM64Emitter& e, uint32_t i);
  static bool Emit_SRW(ARM64Emitter& e, uint32_t i);
  static bool Emit_SRAW(ARM64Emitter& e, uint32_t i);
  static bool Emit_SRAWI(ARM64Emitter& e, uint32_t i);

  // ── Shifts & rotates (64-bit) ─────────────────────────────────────────
  static bool Emit_RLDICL(ARM64Emitter& e, uint32_t i);   // opcd=30, xo=0
  static bool Emit_RLDICR(ARM64Emitter& e, uint32_t i);   // opcd=30, xo=1
  static bool Emit_RLDIC(ARM64Emitter& e, uint32_t i);    // opcd=30, xo=2
  static bool Emit_RLDIMI(ARM64Emitter& e, uint32_t i);   // opcd=30, xo=3
  static bool Emit_RLDCL(ARM64Emitter& e, uint32_t i);    // opcd=30, xo=8
  static bool Emit_SLD(ARM64Emitter& e, uint32_t i);      // xo=27
  static bool Emit_SRD(ARM64Emitter& e, uint32_t i);      // xo=539
  static bool Emit_SRAD(ARM64Emitter& e, uint32_t i);     // xo=794
  static bool Emit_SRADI(ARM64Emitter& e, uint32_t i);    // xo=413/826

  // ── Compare ───────────────────────────────────────────────────────────
  static bool Emit_CMPI(ARM64Emitter& e, uint32_t i);
  static bool Emit_CMPLI(ARM64Emitter& e, uint32_t i);
  static bool Emit_CMP_XO(ARM64Emitter& e, uint32_t i);
  static bool Emit_CMPL_XO(ARM64Emitter& e, uint32_t i);

  // ── Load integer ──────────────────────────────────────────────────────
  static bool Emit_LBZ(ARM64Emitter& e, uint32_t i);
  static bool Emit_LBZU(ARM64Emitter& e, uint32_t i);     // opcd=35
  static bool Emit_LBZX(ARM64Emitter& e, uint32_t i);     // xo=87
  static bool Emit_LBZUX(ARM64Emitter& e, uint32_t i);    // xo=119
  static bool Emit_LHZ(ARM64Emitter& e, uint32_t i);
  static bool Emit_LHZU(ARM64Emitter& e, uint32_t i);     // opcd=41
  static bool Emit_LHZX(ARM64Emitter& e, uint32_t i);     // xo=279
  static bool Emit_LHZUX(ARM64Emitter& e, uint32_t i);    // xo=311
  static bool Emit_LHA(ARM64Emitter& e, uint32_t i);      // opcd=42
  static bool Emit_LHAX(ARM64Emitter& e, uint32_t i);     // xo=343
  static bool Emit_LWZ(ARM64Emitter& e, uint32_t i);
  static bool Emit_LWZU(ARM64Emitter& e, uint32_t i);     // opcd=33
  static bool Emit_LWZX(ARM64Emitter& e, uint32_t i);     // xo=23
  static bool Emit_LWZUX(ARM64Emitter& e, uint32_t i);    // xo=55
  static bool Emit_LWAX(ARM64Emitter& e, uint32_t i);     // xo=341
  static bool Emit_LD(ARM64Emitter& e, uint32_t i);
  static bool Emit_LDX(ARM64Emitter& e, uint32_t i);      // xo=21
  static bool Emit_LDU(ARM64Emitter& e, uint32_t i);
  static bool Emit_LHBRX(ARM64Emitter& e, uint32_t i);    // xo=790
  static bool Emit_LWBRX(ARM64Emitter& e, uint32_t i);    // xo=534
  static bool Emit_LWARX(ARM64Emitter& e, uint32_t i);    // xo=20
  static bool Emit_LDARX(ARM64Emitter& e, uint32_t i);    // xo=84
  static bool Emit_LMW(ARM64Emitter& e, uint32_t i);      // opcd=46

  // ── Store integer ─────────────────────────────────────────────────────
  static bool Emit_STB(ARM64Emitter& e, uint32_t i);
  static bool Emit_STBU(ARM64Emitter& e, uint32_t i);     // opcd=39
  static bool Emit_STBX(ARM64Emitter& e, uint32_t i);     // xo=215
  static bool Emit_STBUX(ARM64Emitter& e, uint32_t i);    // xo=247
  static bool Emit_STH(ARM64Emitter& e, uint32_t i);
  static bool Emit_STHU(ARM64Emitter& e, uint32_t i);     // opcd=45
  static bool Emit_STHX(ARM64Emitter& e, uint32_t i);     // xo=407
  static bool Emit_STHUX(ARM64Emitter& e, uint32_t i);    // xo=439
  static bool Emit_STW(ARM64Emitter& e, uint32_t i);
  static bool Emit_STWU(ARM64Emitter& e, uint32_t i);     // opcd=37
  static bool Emit_STWX(ARM64Emitter& e, uint32_t i);     // xo=151
  static bool Emit_STWUX(ARM64Emitter& e, uint32_t i);    // xo=183
  static bool Emit_STD(ARM64Emitter& e, uint32_t i);
  static bool Emit_STDX(ARM64Emitter& e, uint32_t i);     // xo=149
  static bool Emit_STDU(ARM64Emitter& e, uint32_t i);
  static bool Emit_STHBRX(ARM64Emitter& e, uint32_t i);   // xo=918
  static bool Emit_STWBRX(ARM64Emitter& e, uint32_t i);   // xo=662
  static bool Emit_STWCX(ARM64Emitter& e, uint32_t i);    // xo=150
  static bool Emit_STDCX(ARM64Emitter& e, uint32_t i);    // xo=214
  static bool Emit_STMW(ARM64Emitter& e, uint32_t i);     // opcd=47

  // ── FP load/store ─────────────────────────────────────────────────────
  static bool Emit_LFS(ARM64Emitter& e, uint32_t i);
  static bool Emit_LFSU(ARM64Emitter& e, uint32_t i);     // opcd=49
  static bool Emit_LFSX(ARM64Emitter& e, uint32_t i);     // xo=535
  static bool Emit_LFD(ARM64Emitter& e, uint32_t i);
  static bool Emit_LFDU(ARM64Emitter& e, uint32_t i);     // opcd=51
  static bool Emit_LFDX(ARM64Emitter& e, uint32_t i);     // xo=599
  static bool Emit_STFS(ARM64Emitter& e, uint32_t i);
  static bool Emit_STFSU(ARM64Emitter& e, uint32_t i);    // opcd=53
  static bool Emit_STFSX(ARM64Emitter& e, uint32_t i);    // xo=663
  static bool Emit_STFD(ARM64Emitter& e, uint32_t i);
  static bool Emit_STFDU(ARM64Emitter& e, uint32_t i);    // opcd=55
  static bool Emit_STFDX(ARM64Emitter& e, uint32_t i);    // xo=727

  // ── FP arithmetic (opcd=63) ───────────────────────────────────────────
  static bool Emit_FADD(ARM64Emitter& e, uint32_t i);     // xo=21
  static bool Emit_FSUB(ARM64Emitter& e, uint32_t i);     // xo=20
  static bool Emit_FMUL(ARM64Emitter& e, uint32_t i);     // xo=25
  static bool Emit_FDIV(ARM64Emitter& e, uint32_t i);     // xo=18
  static bool Emit_FMADD(ARM64Emitter& e, uint32_t i);    // xo=29
  static bool Emit_FMSUB(ARM64Emitter& e, uint32_t i);    // xo=28
  static bool Emit_FNMADD(ARM64Emitter& e, uint32_t i);   // xo=31
  static bool Emit_FNMSUB(ARM64Emitter& e, uint32_t i);   // xo=30
  static bool Emit_FABS(ARM64Emitter& e, uint32_t i);     // full xo=264
  static bool Emit_FNEG(ARM64Emitter& e, uint32_t i);     // full xo=40
  static bool Emit_FMR(ARM64Emitter& e, uint32_t i);      // full xo=72
  static bool Emit_FNABS(ARM64Emitter& e, uint32_t i);    // full xo=136
  static bool Emit_FSQRT(ARM64Emitter& e, uint32_t i);    // xo=22
  static bool Emit_FSEL(ARM64Emitter& e, uint32_t i);     // xo=23
  static bool Emit_FRES(ARM64Emitter& e, uint32_t i);     // xo=24
  static bool Emit_FRSQRTE(ARM64Emitter& e, uint32_t i);  // full xo=26
  static bool Emit_FCTIW(ARM64Emitter& e, uint32_t i);    // full xo=14
  static bool Emit_FCTIWZ(ARM64Emitter& e, uint32_t i);   // full xo=15
  static bool Emit_FCTID(ARM64Emitter& e, uint32_t i);    // full xo=814
  static bool Emit_FCTIDZ(ARM64Emitter& e, uint32_t i);   // full xo=815
  static bool Emit_FCFID(ARM64Emitter& e, uint32_t i);    // full xo=846
  static bool Emit_FRSP(ARM64Emitter& e, uint32_t i);     // full xo=12
  static bool Emit_FCMPU(ARM64Emitter& e, uint32_t i);    // full xo=0
  static bool Emit_FCMPO(ARM64Emitter& e, uint32_t i);    // full xo=32
  static bool Emit_MFFS(ARM64Emitter& e, uint32_t i);     // full xo=583
  static bool Emit_MTFSF(ARM64Emitter& e, uint32_t i);    // full xo=711
  static bool Emit_MTFSB0(ARM64Emitter& e, uint32_t i);   // full xo=70
  static bool Emit_MTFSB1(ARM64Emitter& e, uint32_t i);   // full xo=38
  static bool Emit_MTFSFI(ARM64Emitter& e, uint32_t i);   // full xo=134

  // ── FP single-precision (opcd=59) ─────────────────────────────────────
  static bool Emit_FADDS(ARM64Emitter& e, uint32_t i);
  static bool Emit_FSUBS(ARM64Emitter& e, uint32_t i);
  static bool Emit_FMULS(ARM64Emitter& e, uint32_t i);
  static bool Emit_FDIVS(ARM64Emitter& e, uint32_t i);
  static bool Emit_FMADDS(ARM64Emitter& e, uint32_t i);
  static bool Emit_FMSUBS(ARM64Emitter& e, uint32_t i);
  static bool Emit_FNMADDS(ARM64Emitter& e, uint32_t i);
  static bool Emit_FNMSUBS(ARM64Emitter& e, uint32_t i);
  static bool Emit_FSQRTS(ARM64Emitter& e, uint32_t i);
  static bool Emit_FRESS(ARM64Emitter& e, uint32_t i);

  // ── Branch ────────────────────────────────────────────────────────────
  static bool Emit_B(ARM64Emitter& e, uint32_t i, uint32_t guest_addr);
  static bool Emit_BC(ARM64Emitter& e, uint32_t i, uint32_t guest_addr);
  static bool Emit_BCLR(ARM64Emitter& e, uint32_t i);
  static bool Emit_BCCTR(ARM64Emitter& e, uint32_t i);

  // ── Condition register ────────────────────────────────────────────────
  static bool Emit_CRAND(ARM64Emitter& e, uint32_t i);    // xo=257
  static bool Emit_CROR(ARM64Emitter& e, uint32_t i);     // xo=449
  static bool Emit_CRXOR(ARM64Emitter& e, uint32_t i);    // xo=193
  static bool Emit_CRANDC(ARM64Emitter& e, uint32_t i);   // xo=129
  static bool Emit_CRORC(ARM64Emitter& e, uint32_t i);    // xo=417
  static bool Emit_CRNOR(ARM64Emitter& e, uint32_t i);    // xo=33
  static bool Emit_CRNAND(ARM64Emitter& e, uint32_t i);   // xo=225
  static bool Emit_CREQV(ARM64Emitter& e, uint32_t i);    // xo=289
  static bool Emit_MCRF(ARM64Emitter& e, uint32_t i);     // xo=0

  // ── System & SPR ──────────────────────────────────────────────────────
  static bool Emit_SC(ARM64Emitter& e, uint32_t i);
  static bool Emit_MFSPR(ARM64Emitter& e, uint32_t i);
  static bool Emit_MTSPR(ARM64Emitter& e, uint32_t i);
  static bool Emit_MFCR(ARM64Emitter& e, uint32_t i);
  static bool Emit_MTCRF(ARM64Emitter& e, uint32_t i);
  static bool Emit_MFMSR(ARM64Emitter& e, uint32_t i);    // xo=83
  static bool Emit_MTMSR(ARM64Emitter& e, uint32_t i);    // xo=146
  static bool Emit_MTMSRD(ARM64Emitter& e, uint32_t i);   // xo=178
  static bool Emit_TW(ARM64Emitter& e, uint32_t i);       // xo=4
  static bool Emit_TWI(ARM64Emitter& e, uint32_t i);      // opcd=3
  static bool Emit_TD(ARM64Emitter& e, uint32_t i);       // xo=68
  static bool Emit_TDI(ARM64Emitter& e, uint32_t i);      // opcd=2
  static bool Emit_SYNC(ARM64Emitter& e, uint32_t i);     // xo=598
  static bool Emit_EIEIO(ARM64Emitter& e, uint32_t i);    // xo=854
  static bool Emit_ISYNC(ARM64Emitter& e, uint32_t i);    // opcd=19, xo=150
  static bool Emit_DCBF(ARM64Emitter& e, uint32_t i);     // xo=86
  static bool Emit_DCBST(ARM64Emitter& e, uint32_t i);    // xo=54
  static bool Emit_DCBT(ARM64Emitter& e, uint32_t i);     // xo=278
  static bool Emit_DCBTST(ARM64Emitter& e, uint32_t i);   // xo=246
  static bool Emit_DCBZ(ARM64Emitter& e, uint32_t i);     // xo=1014
  static bool Emit_ICBI(ARM64Emitter& e, uint32_t i);     // xo=982

  // ── Float load/store ──────────────────────────────────────────────────
  // (declared above in FP load/store)

  // ── VMX128 (Xbox 360 SIMD) ───────────────────────────────────────────
  static bool Emit_VMX128(ARM64Emitter& e, uint32_t i);

  // ── Helpers ───────────────────────────────────────────────────────────
  static void UpdateCR0(ARM64Emitter& e, Reg result);
  static VReg MapFPR(uint32_t fpr_index);
  static VReg MapVR(uint32_t vr_index);

  // ── Load/Store indexed helpers ────────────────────────────────────────
  static void EmitLoadIndexed(ARM64Emitter& e, uint32_t i, int bytes, bool sign_extend);
  static void EmitStoreIndexed(ARM64Emitter& e, uint32_t i, int bytes);
  static void EmitLoadUpdate(ARM64Emitter& e, uint32_t i, int bytes);
  static void EmitStoreUpdate(ARM64Emitter& e, uint32_t i, int bytes);
};

}  // namespace xe::cpu::backend::arm64
