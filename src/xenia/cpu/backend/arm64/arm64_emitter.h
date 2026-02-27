/**
 * Vera360 — Xenia Edge
 * ARM64 (AArch64) Machine Code Emitter
 *
 * Generates raw ARM64 instructions for the JIT backend.
 * This replaces the x86-64 backend from upstream Xenia.
 *
 * Encoding reference: ARM Architecture Reference Manual (ARMv8-A)
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace xe::cpu::backend::arm64 {

/// ARM64 general-purpose registers
enum class Reg : uint8_t {
  X0 = 0, X1, X2, X3, X4, X5, X6, X7,
  X8, X9, X10, X11, X12, X13, X14, X15,
  X16, X17, X18, X19, X20, X21, X22, X23,
  X24, X25, X26, X27, X28, X29, X30,
  SP = 31,
  XZR = 31,  // Zero register (context-dependent)
  // Aliases
  FP = 29,   // Frame pointer
  LR = 30,   // Link register
};

/// ARM64 NEON/FP registers (128-bit)
enum class VReg : uint8_t {
  V0 = 0, V1, V2, V3, V4, V5, V6, V7,
  V8, V9, V10, V11, V12, V13, V14, V15,
  V16, V17, V18, V19, V20, V21, V22, V23,
  V24, V25, V26, V27, V28, V29, V30, V31,
};

/// Condition codes
enum class Cond : uint8_t {
  EQ = 0b0000,  // Equal
  NE = 0b0001,  // Not equal
  CS = 0b0010,  // Carry set / unsigned >=
  CC = 0b0011,  // Carry clear / unsigned <
  MI = 0b0100,  // Negative
  PL = 0b0101,  // Positive or zero
  VS = 0b0110,  // Overflow set
  VC = 0b0111,  // Overflow clear
  HI = 0b1000,  // Unsigned >
  LS = 0b1001,  // Unsigned <=
  GE = 0b1010,  // Signed >=
  LT = 0b1011,  // Signed <
  GT = 0b1100,  // Signed >
  LE = 0b1101,  // Signed <=
  AL = 0b1110,  // Always
  NV = 0b1111,
  // Aliases
  HS = CS, LO = CC,
};

/// Shift types for data processing
enum class Shift : uint8_t {
  LSL = 0b00,
  LSR = 0b01,
  ASR = 0b10,
  ROR = 0b11,
};

/**
 * ARM64 code emitter — writes instructions to a growable buffer.
 */
class ARM64Emitter {
 public:
  ARM64Emitter();
  ~ARM64Emitter();

  /// Reset emitter to empty state
  void Reset();

  /// Get pointer to emitted code
  const uint8_t* GetCode() const;
  size_t GetCodeSize() const;

  /// Copy code to executable memory, flush icache, return entry
  void* FinalizeToExecutable();

  // ── Data Processing (Immediate) ───────────────────────────────────────

  void MOV(Reg rd, Reg rn);                           // MOV Xd, Xn
  void MOV_imm(Reg rd, uint64_t imm);                 // MOVZ/MOVK sequence
  void MOVZ(Reg rd, uint16_t imm, uint8_t shift = 0); // MOVZ Xd, #imm, LSL #shift
  void MOVK(Reg rd, uint16_t imm, uint8_t shift = 0); // MOVK Xd, #imm, LSL #shift

  void ADD_imm(Reg rd, Reg rn, uint32_t imm12);
  void SUB_imm(Reg rd, Reg rn, uint32_t imm12);
  void ADDS_imm(Reg rd, Reg rn, uint32_t imm12);      // Sets flags
  void SUBS_imm(Reg rd, Reg rn, uint32_t imm12);

  // ── Data Processing (Register) ────────────────────────────────────────

  void ADD(Reg rd, Reg rn, Reg rm, Shift sh = Shift::LSL, uint8_t amount = 0);
  void SUB(Reg rd, Reg rn, Reg rm, Shift sh = Shift::LSL, uint8_t amount = 0);
  void ADDS(Reg rd, Reg rn, Reg rm);
  void SUBS(Reg rd, Reg rn, Reg rm);

  void AND(Reg rd, Reg rn, Reg rm);
  void ORR(Reg rd, Reg rn, Reg rm);
  void EOR(Reg rd, Reg rn, Reg rm);
  void ORN(Reg rd, Reg rn, Reg rm);

  void LSL_reg(Reg rd, Reg rn, Reg rm);   // LSLV
  void LSR_reg(Reg rd, Reg rn, Reg rm);
  void ASR_reg(Reg rd, Reg rn, Reg rm);
  void ROR_reg(Reg rd, Reg rn, Reg rm);

  void MUL(Reg rd, Reg rn, Reg rm);
  void SMULL(Reg rd, Reg rn, Reg rm);      // Signed multiply long
  void UMULL(Reg rd, Reg rn, Reg rm);      // Unsigned multiply long
  void SDIV(Reg rd, Reg rn, Reg rm);
  void UDIV(Reg rd, Reg rn, Reg rm);

  void CLZ(Reg rd, Reg rn);
  void RBIT(Reg rd, Reg rn);
  void REV(Reg rd, Reg rn);               // Byte reverse (endian swap)
  void REV16(Reg rd, Reg rn);
  void REV32(Reg rd, Reg rn);

  // ── Comparison ────────────────────────────────────────────────────────

  void CMP(Reg rn, Reg rm);
  void CMP_imm(Reg rn, uint32_t imm12);
  void CMN(Reg rn, Reg rm);
  void TST(Reg rn, Reg rm);

  // ── Conditional select ────────────────────────────────────────────────

  void CSEL(Reg rd, Reg rn, Reg rm, Cond cc);
  void CSINC(Reg rd, Reg rn, Reg rm, Cond cc);
  void CSET(Reg rd, Cond cc);

  // ── Branches ──────────────────────────────────────────────────────────

  void B(int32_t offset_bytes);
  void B(Cond cc, int32_t offset_bytes);
  void BL(int32_t offset_bytes);
  void BR(Reg rn);
  void BLR(Reg rn);
  void RET(Reg rn = Reg::LR);
  void CBZ(Reg rt, int32_t offset_bytes);
  void CBNZ(Reg rt, int32_t offset_bytes);

  // ── Memory Access ─────────────────────────────────────────────────────

  void LDR(Reg rt, Reg rn, int32_t offset = 0);       // 64-bit load
  void LDRW(Reg rt, Reg rn, int32_t offset = 0);      // 32-bit load
  void LDRH(Reg rt, Reg rn, int32_t offset = 0);      // 16-bit load
  void LDRB(Reg rt, Reg rn, int32_t offset = 0);      // 8-bit load

  void STR(Reg rt, Reg rn, int32_t offset = 0);       // 64-bit store
  void STRW(Reg rt, Reg rn, int32_t offset = 0);      // 32-bit store
  void STRH(Reg rt, Reg rn, int32_t offset = 0);
  void STRB(Reg rt, Reg rn, int32_t offset = 0);

  void LDP(Reg rt1, Reg rt2, Reg rn, int32_t offset); // Load pair
  void STP(Reg rt1, Reg rt2, Reg rn, int32_t offset); // Store pair

  // Pre/post-indexed
  void LDR_pre(Reg rt, Reg rn, int32_t offset);
  void LDR_post(Reg rt, Reg rn, int32_t offset);
  void STR_pre(Reg rt, Reg rn, int32_t offset);
  void STR_post(Reg rt, Reg rn, int32_t offset);

  // ── NEON / SIMD (for Xbox 360 VMX128 emulation) ──────────────────────

  void FMOV_vtog(Reg rd, VReg vn);                    // FMOV Xd, Dn
  void FMOV_gtov(VReg vd, Reg rn);                    // FMOV Dd, Xn

  void LDR_v128(VReg vt, Reg rn, int32_t offset = 0); // LDR Qt, [Xn, #off]
  void STR_v128(VReg vt, Reg rn, int32_t offset = 0); // STR Qt, [Xn, #off]

  void FADD_4s(VReg vd, VReg vn, VReg vm);            // FADD Vd.4S, Vn.4S, Vm.4S
  void FSUB_4s(VReg vd, VReg vn, VReg vm);
  void FMUL_4s(VReg vd, VReg vn, VReg vm);
  void FDIV_4s(VReg vd, VReg vn, VReg vm);

  void FMLA_4s(VReg vd, VReg vn, VReg vm);            // Fused multiply-add
  void FABS_4s(VReg vd, VReg vn);
  void FNEG_4s(VReg vd, VReg vn);
  void FSQRT_4s(VReg vd, VReg vn);
  void FMIN_4s(VReg vd, VReg vn, VReg vm);
  void FMAX_4s(VReg vd, VReg vn, VReg vm);

  void AND_v(VReg vd, VReg vn, VReg vm);              // Bitwise AND
  void ORR_v(VReg vd, VReg vn, VReg vm);
  void EOR_v(VReg vd, VReg vn, VReg vm);

  void DUP_4s(VReg vd, VReg vn, uint8_t index);       // Broadcast element
  void INS_4s(VReg vd, uint8_t dst_idx, VReg vn, uint8_t src_idx); // Insert element

  void FCVTZS_4s(VReg vd, VReg vn);                   // Float to int
  void SCVTF_4s(VReg vd, VReg vn);                    // Int to float

  // ── Extended integer ──────────────────────────────────────────────────

  void MADD(Reg rd, Reg rn, Reg rm, Reg ra);          // rd = rn*rm + ra
  void MSUB(Reg rd, Reg rn, Reg rm, Reg ra);          // rd = ra - rn*rm
  void SMADDL(Reg rd, Reg rn, Reg rm, Reg ra);        // 32×32+64→64 signed
  void UMADDL(Reg rd, Reg rn, Reg rm, Reg ra);        // unsigned
  void SMULH(Reg rd, Reg rn, Reg rm);                 // signed high multiply
  void UMULH(Reg rd, Reg rn, Reg rm);                 // unsigned high multiply
  void ADC(Reg rd, Reg rn, Reg rm);                   // add with carry
  void ADCS(Reg rd, Reg rn, Reg rm);                  // add with carry, set flags
  void SBC(Reg rd, Reg rn, Reg rm);                   // subtract with carry
  void SBCS(Reg rd, Reg rn, Reg rm);                  // subtract with carry, set flags
  void SXTW(Reg rd, Reg rn);                          // sign-extend word
  void SXTH(Reg rd, Reg rn);                          // sign-extend halfword
  void SXTB(Reg rd, Reg rn);                          // sign-extend byte
  void UXTW(Reg rd, Reg rn);                          // zero-extend word
  void UXTH(Reg rd, Reg rn);
  void UXTB(Reg rd, Reg rn);
  void BIC(Reg rd, Reg rn, Reg rm);                   // bit clear
  void BICS(Reg rd, Reg rn, Reg rm);                  // bit clear set flags
  void MVN(Reg rd, Reg rm);                           // bitwise NOT
  void EON(Reg rd, Reg rn, Reg rm);                   // exclusive OR NOT
  void ANDS(Reg rd, Reg rn, Reg rm);                  // AND set flags
  void UBFM(Reg rd, Reg rn, uint8_t immr, uint8_t imms); // unsigned bitfield move
  void SBFM(Reg rd, Reg rn, uint8_t immr, uint8_t imms); // signed bitfield move
  void EXTR(Reg rd, Reg rn, Reg rm, uint8_t lsb);    // extract
  void CCMP(Reg rn, Reg rm, uint8_t nzcv, Cond cc);   // conditional compare
  void CSINV(Reg rd, Reg rn, Reg rm, Cond cc);        // conditional select invert
  void CSNEG(Reg rd, Reg rn, Reg rm, Cond cc);        // conditional select negate

  // ── Load/store register offset ────────────────────────────────────────

  void LDR_reg(Reg rt, Reg rn, Reg rm);               // LDR Xt, [Xn, Xm]
  void LDRW_reg(Reg rt, Reg rn, Reg rm);
  void LDRH_reg(Reg rt, Reg rn, Reg rm);
  void LDRB_reg(Reg rt, Reg rn, Reg rm);
  void STR_reg(Reg rt, Reg rn, Reg rm);
  void STRW_reg(Reg rt, Reg rn, Reg rm);
  void STRH_reg(Reg rt, Reg rn, Reg rm);
  void STRB_reg(Reg rt, Reg rn, Reg rm);
  void LDRSW(Reg rt, Reg rn, int32_t offset = 0);     // Load signed word
  void LDRSH(Reg rt, Reg rn, int32_t offset = 0);     // Load signed halfword
  void LDRSB(Reg rt, Reg rn, int32_t offset = 0);     // Load signed byte
  void LDAXR(Reg rt, Reg rn);                         // Load-acquire exclusive
  void STLXR(Reg rs, Reg rt, Reg rn);                 // Store-release exclusive
  void LDAXRW(Reg rt, Reg rn);                        // 32-bit load-acquire exclusive
  void STLXRW(Reg rs, Reg rt, Reg rn);                // 32-bit store-release exclusive
  void CLREX();                                        // Clear exclusive

  // ── Scalar FP ─────────────────────────────────────────────────────────

  void FADD_d(VReg vd, VReg vn, VReg vm);             // Double-precision add
  void FSUB_d(VReg vd, VReg vn, VReg vm);
  void FMUL_d(VReg vd, VReg vn, VReg vm);
  void FDIV_d(VReg vd, VReg vn, VReg vm);
  void FMADD_d(VReg vd, VReg vn, VReg vm, VReg va);   // d = n*m + a
  void FMSUB_d(VReg vd, VReg vn, VReg vm, VReg va);   // d = n*m - a
  void FNMADD_d(VReg vd, VReg vn, VReg vm, VReg va);  // d = -(n*m + a)
  void FNMSUB_d(VReg vd, VReg vn, VReg vm, VReg va);  // d = -(n*m - a)
  void FABS_d(VReg vd, VReg vn);
  void FNEG_d(VReg vd, VReg vn);
  void FSQRT_d(VReg vd, VReg vn);
  void FMOV_d(VReg vd, VReg vn);
  void FCMP_d(VReg vn, VReg vm);                      // Compare, set NZCV
  void FCMP_dz(VReg vn);                              // Compare with zero
  void FADD_s(VReg vd, VReg vn, VReg vm);             // Single-precision
  void FSUB_s(VReg vd, VReg vn, VReg vm);
  void FMUL_s(VReg vd, VReg vn, VReg vm);
  void FDIV_s(VReg vd, VReg vn, VReg vm);
  void FMADD_s(VReg vd, VReg vn, VReg vm, VReg va);
  void FMSUB_s(VReg vd, VReg vn, VReg vm, VReg va);
  void FABS_s(VReg vd, VReg vn);
  void FNEG_s(VReg vd, VReg vn);
  void FSQRT_s(VReg vd, VReg vn);
  void FMOV_s(VReg vd, VReg vn);
  void FCVT_sd(VReg vd, VReg vn);                     // Single→Double
  void FCVT_ds(VReg vd, VReg vn);                     // Double→Single
  void FCVTZS_xd(Reg rd, VReg vn);                    // FP double→int64
  void FCVTZS_wd(Reg rd, VReg vn);                    // FP double→int32
  void SCVTF_dx(VReg vd, Reg rn);                     // int64→FP double
  void SCVTF_dw(VReg vd, Reg rn);                     // int32→FP double
  void UCVTF_dx(VReg vd, Reg rn);                     // uint64→FP double
  void FMOV_dtog(Reg rd, VReg vn);                    // FMOV Xd, Dn
  void FMOV_gtod(VReg vd, Reg rn);                    // FMOV Dn, Xn
  void FMOV_stog(Reg rd, VReg vn);                    // FMOV Wd, Sn
  void FMOV_gtos(VReg vd, Reg rn);                    // FMOV Sn, Wn
  void FRECPE_d(VReg vd, VReg vn);                    // FP reciprocal estimate
  void FRSQRTE_d(VReg vd, VReg vn);                   // FP reciprocal sqrt estimate
  void FCSEL_d(VReg vd, VReg vn, VReg vm, Cond cc);   // FP conditional select
  void LDR_d(VReg vt, Reg rn, int32_t offset = 0);    // Load double
  void STR_d(VReg vt, Reg rn, int32_t offset = 0);    // Store double
  void LDR_s(VReg vt, Reg rn, int32_t offset = 0);    // Load single
  void STR_s(VReg vt, Reg rn, int32_t offset = 0);    // Store single

  // ── Additional NEON ───────────────────────────────────────────────────

  void MOV_v(VReg vd, VReg vn);                       // Copy vector
  void MOVI_v(VReg vd, uint8_t imm8);                 // Move immediate to vector
  void NOT_v(VReg vd, VReg vn);                       // Bitwise NOT
  void BSL_v(VReg vd, VReg vn, VReg vm);              // Bit select
  void BIF_v(VReg vd, VReg vn, VReg vm);              // Bit insert if false
  void BIT_v(VReg vd, VReg vn, VReg vm);              // Bit insert if true
  void FRINTM_4s(VReg vd, VReg vn);                   // Round toward -inf
  void FRINTP_4s(VReg vd, VReg vn);                   // Round toward +inf
  void FRINTZ_4s(VReg vd, VReg vn);                   // Round toward zero
  void FRINTN_4s(VReg vd, VReg vn);                   // Round to nearest
  void FRECPE_4s(VReg vd, VReg vn);                   // Reciprocal estimate
  void FRSQRTE_4s(VReg vd, VReg vn);                  // Reciprocal sqrt estimate
  void ADDV_4s(VReg vd, VReg vn);                     // Horizontal add
  void FCMEQ_4s(VReg vd, VReg vn, VReg vm);           // Float compare equal
  void FCMGT_4s(VReg vd, VReg vn, VReg vm);           // Float compare greater
  void FCMGE_4s(VReg vd, VReg vn, VReg vm);           // Float compare greater-equal
  void ADD_4s(VReg vd, VReg vn, VReg vm);             // Integer add (4x32)
  void SUB_4s(VReg vd, VReg vn, VReg vm);             // Integer sub (4x32)
  void MUL_4s(VReg vd, VReg vn, VReg vm);             // Integer mul (4x32)
  void SHL_4s(VReg vd, VReg vn, uint8_t shift);       // Left shift
  void SSHR_4s(VReg vd, VReg vn, uint8_t shift);      // Signed right shift
  void USHR_4s(VReg vd, VReg vn, uint8_t shift);      // Unsigned right shift
  void CMPEQ_4s(VReg vd, VReg vn, VReg vm);           // Integer compare eq
  void CMGT_4s(VReg vd, VReg vn, VReg vm);            // Integer compare gt
  void TBL_v(VReg vd, VReg vn, VReg vm);              // Table lookup
  void ZIP1_4s(VReg vd, VReg vn, VReg vm);            // Interleave low
  void ZIP2_4s(VReg vd, VReg vn, VReg vm);            // Interleave high
  void UZP1_4s(VReg vd, VReg vn, VReg vm);            // Deinterleave even
  void UZP2_4s(VReg vd, VReg vn, VReg vm);            // Deinterleave odd
  void REV64_4s(VReg vd, VReg vn);                    // Reverse 32-bit elements in 64-bit
  void EXT_v(VReg vd, VReg vn, VReg vm, uint8_t idx); // Extract from pair
  void SMIN_4s(VReg vd, VReg vn, VReg vm);            // Signed minimum
  void SMAX_4s(VReg vd, VReg vn, VReg vm);            // Signed maximum
  void ABS_4s(VReg vd, VReg vn);                      // Absolute value
  void NEG_4s(VReg vd, VReg vn);                      // Negate

  // ── System ────────────────────────────────────────────────────────────

  void NOP();
  void BRK(uint16_t imm = 0);                         // Debug breakpoint
  void DMB_ISH();                                      // Data memory barrier
  void DSB_ISH();                                      // Data sync barrier
  void ISB();                                          // Instruction sync barrier
  void SVC(uint16_t imm);                              // Supervisor call

  void MRS(Reg rt, uint32_t sysreg);                   // Read system register
  void MSR(uint32_t sysreg, Reg rt);                   // Write system register

  // ── Label support ─────────────────────────────────────────────────────

  /// Get current write offset (for label tracking)
  size_t GetOffset() const { return code_.size(); }

  /// Patch a branch at 'branch_offset' to point to 'target_offset'
  void PatchBranch(size_t branch_offset, size_t target_offset);
  void PatchCondBranch(size_t branch_offset, size_t target_offset);

 private:
  void Emit32(uint32_t instruction);

  std::vector<uint8_t> code_;
};

}  // namespace xe::cpu::backend::arm64
