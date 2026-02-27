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

  /// Encode commonly used instruction formats
  uint32_t EncodeAddSub(bool is64, bool sub, bool setflags,
                         Reg rd, Reg rn, Reg rm, Shift sh, uint8_t amount);
  uint32_t EncodeAddSubImm(bool is64, bool sub, bool setflags,
                            Reg rd, Reg rn, uint32_t imm12, bool shift12 = false);
  uint32_t EncodeLogical(bool is64, uint8_t opc, Reg rd, Reg rn, Reg rm);
  uint32_t EncodeLoadStore(bool is64, bool is_load, Reg rt, Reg rn, int32_t offset);

  std::vector<uint8_t> code_;
};

}  // namespace xe::cpu::backend::arm64
