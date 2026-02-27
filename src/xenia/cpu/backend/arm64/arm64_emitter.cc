/**
 * Vera360 — Xenia Edge
 * ARM64 Machine Code Emitter — implementation
 *
 * Encodes AArch64 instructions according to the ARMv8-A ISA.
 */

#include "xenia/cpu/backend/arm64/arm64_emitter.h"
#include "xenia/base/memory/memory.h"
#include "xenia/base/logging.h"

#include <cstring>

#if defined(__aarch64__)
// For instruction cache flush
extern "C" void __clear_cache(void* begin, void* end);
#endif

namespace xe::cpu::backend::arm64 {

ARM64Emitter::ARM64Emitter() {
  code_.reserve(64 * 1024);  // 64KB initial code buffer
}

ARM64Emitter::~ARM64Emitter() = default;

void ARM64Emitter::Reset() {
  code_.clear();
}

const uint8_t* ARM64Emitter::GetCode() const {
  return code_.data();
}

size_t ARM64Emitter::GetCodeSize() const {
  return code_.size();
}

void* ARM64Emitter::FinalizeToExecutable() {
  size_t size = code_.size();
  if (size == 0) return nullptr;

  void* exec = xe::memory::AllocateExecutable(size);
  if (!exec) return nullptr;

  memcpy(exec, code_.data(), size);

  // Flush instruction cache — critical on ARM64!
#if defined(__aarch64__)
  __clear_cache(exec, static_cast<uint8_t*>(exec) + size);
#endif

  return exec;
}

void ARM64Emitter::Emit32(uint32_t instruction) {
  code_.push_back(instruction & 0xFF);
  code_.push_back((instruction >> 8) & 0xFF);
  code_.push_back((instruction >> 16) & 0xFF);
  code_.push_back((instruction >> 24) & 0xFF);
}

// ── Encoding helpers ────────────────────────────────────────────────────────

static constexpr uint32_t Rd(Reg r)  { return static_cast<uint32_t>(r) & 0x1F; }
static constexpr uint32_t Rn(Reg r)  { return (static_cast<uint32_t>(r) & 0x1F) << 5; }
static constexpr uint32_t Rm(Reg r)  { return (static_cast<uint32_t>(r) & 0x1F) << 16; }
static constexpr uint32_t Vd(VReg r) { return static_cast<uint32_t>(r) & 0x1F; }
static constexpr uint32_t Vn(VReg r) { return (static_cast<uint32_t>(r) & 0x1F) << 5; }
static constexpr uint32_t Vm(VReg r) { return (static_cast<uint32_t>(r) & 0x1F) << 16; }

// ── Data Processing (Immediate) ─────────────────────────────────────────────

void ARM64Emitter::MOV(Reg rd, Reg rn) {
  // ORR Xd, XZR, Xn
  Emit32(0xAA0003E0 | Rd(rd) | Rm(rn));
}

void ARM64Emitter::MOV_imm(Reg rd, uint64_t imm) {
  // Emit MOVZ + up to 3x MOVK to build 64-bit immediate
  MOVZ(rd, static_cast<uint16_t>(imm & 0xFFFF), 0);
  if (imm > 0xFFFF) {
    MOVK(rd, static_cast<uint16_t>((imm >> 16) & 0xFFFF), 16);
  }
  if (imm > 0xFFFFFFFF) {
    MOVK(rd, static_cast<uint16_t>((imm >> 32) & 0xFFFF), 32);
  }
  if (imm > 0xFFFFFFFFFFFF) {
    MOVK(rd, static_cast<uint16_t>((imm >> 48) & 0xFFFF), 48);
  }
}

void ARM64Emitter::MOVZ(Reg rd, uint16_t imm, uint8_t shift) {
  uint32_t hw = shift / 16;
  Emit32(0xD2800000 | (hw << 21) | (static_cast<uint32_t>(imm) << 5) | Rd(rd));
}

void ARM64Emitter::MOVK(Reg rd, uint16_t imm, uint8_t shift) {
  uint32_t hw = shift / 16;
  Emit32(0xF2800000 | (hw << 21) | (static_cast<uint32_t>(imm) << 5) | Rd(rd));
}

void ARM64Emitter::ADD_imm(Reg rd, Reg rn, uint32_t imm12) {
  Emit32(0x91000000 | ((imm12 & 0xFFF) << 10) | Rn(rn) | Rd(rd));
}

void ARM64Emitter::SUB_imm(Reg rd, Reg rn, uint32_t imm12) {
  Emit32(0xD1000000 | ((imm12 & 0xFFF) << 10) | Rn(rn) | Rd(rd));
}

void ARM64Emitter::ADDS_imm(Reg rd, Reg rn, uint32_t imm12) {
  Emit32(0xB1000000 | ((imm12 & 0xFFF) << 10) | Rn(rn) | Rd(rd));
}

void ARM64Emitter::SUBS_imm(Reg rd, Reg rn, uint32_t imm12) {
  Emit32(0xF1000000 | ((imm12 & 0xFFF) << 10) | Rn(rn) | Rd(rd));
}

// ── Data Processing (Register) ──────────────────────────────────────────────

void ARM64Emitter::ADD(Reg rd, Reg rn, Reg rm, Shift sh, uint8_t amount) {
  Emit32(0x8B000000 | (static_cast<uint32_t>(sh) << 22) |
         (static_cast<uint32_t>(amount & 0x3F) << 10) |
         Rm(rm) | Rn(rn) | Rd(rd));
}

void ARM64Emitter::SUB(Reg rd, Reg rn, Reg rm, Shift sh, uint8_t amount) {
  Emit32(0xCB000000 | (static_cast<uint32_t>(sh) << 22) |
         (static_cast<uint32_t>(amount & 0x3F) << 10) |
         Rm(rm) | Rn(rn) | Rd(rd));
}

void ARM64Emitter::ADDS(Reg rd, Reg rn, Reg rm) {
  Emit32(0xAB000000 | Rm(rm) | Rn(rn) | Rd(rd));
}

void ARM64Emitter::SUBS(Reg rd, Reg rn, Reg rm) {
  Emit32(0xEB000000 | Rm(rm) | Rn(rn) | Rd(rd));
}

void ARM64Emitter::AND(Reg rd, Reg rn, Reg rm) {
  Emit32(0x8A000000 | Rm(rm) | Rn(rn) | Rd(rd));
}

void ARM64Emitter::ORR(Reg rd, Reg rn, Reg rm) {
  Emit32(0xAA000000 | Rm(rm) | Rn(rn) | Rd(rd));
}

void ARM64Emitter::EOR(Reg rd, Reg rn, Reg rm) {
  Emit32(0xCA000000 | Rm(rm) | Rn(rn) | Rd(rd));
}

void ARM64Emitter::ORN(Reg rd, Reg rn, Reg rm) {
  Emit32(0xAA200000 | Rm(rm) | Rn(rn) | Rd(rd));
}

void ARM64Emitter::LSL_reg(Reg rd, Reg rn, Reg rm) {
  Emit32(0x9AC02000 | Rm(rm) | Rn(rn) | Rd(rd));
}

void ARM64Emitter::LSR_reg(Reg rd, Reg rn, Reg rm) {
  Emit32(0x9AC02400 | Rm(rm) | Rn(rn) | Rd(rd));
}

void ARM64Emitter::ASR_reg(Reg rd, Reg rn, Reg rm) {
  Emit32(0x9AC02800 | Rm(rm) | Rn(rn) | Rd(rd));
}

void ARM64Emitter::ROR_reg(Reg rd, Reg rn, Reg rm) {
  Emit32(0x9AC02C00 | Rm(rm) | Rn(rn) | Rd(rd));
}

void ARM64Emitter::MUL(Reg rd, Reg rn, Reg rm) {
  // MADD Xd, Xn, Xm, XZR
  Emit32(0x9B007C00 | Rm(rm) | Rn(rn) | Rd(rd));
}

void ARM64Emitter::SMULL(Reg rd, Reg rn, Reg rm) {
  Emit32(0x9B207C00 | Rm(rm) | Rn(rn) | Rd(rd));
}

void ARM64Emitter::UMULL(Reg rd, Reg rn, Reg rm) {
  Emit32(0x9BA07C00 | Rm(rm) | Rn(rn) | Rd(rd));
}

void ARM64Emitter::SDIV(Reg rd, Reg rn, Reg rm) {
  Emit32(0x9AC00C00 | Rm(rm) | Rn(rn) | Rd(rd));
}

void ARM64Emitter::UDIV(Reg rd, Reg rn, Reg rm) {
  Emit32(0x9AC00800 | Rm(rm) | Rn(rn) | Rd(rd));
}

void ARM64Emitter::CLZ(Reg rd, Reg rn) {
  Emit32(0xDAC01000 | Rn(rn) | Rd(rd));
}

void ARM64Emitter::RBIT(Reg rd, Reg rn) {
  Emit32(0xDAC00000 | Rn(rn) | Rd(rd));
}

void ARM64Emitter::REV(Reg rd, Reg rn) {
  // REV X (64-bit byte reverse)
  Emit32(0xDAC00C00 | Rn(rn) | Rd(rd));
}

void ARM64Emitter::REV16(Reg rd, Reg rn) {
  Emit32(0xDAC00400 | Rn(rn) | Rd(rd));
}

void ARM64Emitter::REV32(Reg rd, Reg rn) {
  Emit32(0xDAC00800 | Rn(rn) | Rd(rd));
}

// ── Comparison ──────────────────────────────────────────────────────────────

void ARM64Emitter::CMP(Reg rn, Reg rm) {
  SUBS(Reg::XZR, rn, rm);
}

void ARM64Emitter::CMP_imm(Reg rn, uint32_t imm12) {
  SUBS_imm(Reg::XZR, rn, imm12);
}

void ARM64Emitter::CMN(Reg rn, Reg rm) {
  ADDS(Reg::XZR, rn, rm);
}

void ARM64Emitter::TST(Reg rn, Reg rm) {
  // ANDS XZR, Xn, Xm
  Emit32(0xEA000000 | Rm(rm) | Rn(rn) | Rd(Reg::XZR));
}

// ── Conditional select ──────────────────────────────────────────────────────

void ARM64Emitter::CSEL(Reg rd, Reg rn, Reg rm, Cond cc) {
  Emit32(0x9A800000 | (static_cast<uint32_t>(cc) << 12) | Rm(rm) | Rn(rn) | Rd(rd));
}

void ARM64Emitter::CSINC(Reg rd, Reg rn, Reg rm, Cond cc) {
  Emit32(0x9A800400 | (static_cast<uint32_t>(cc) << 12) | Rm(rm) | Rn(rn) | Rd(rd));
}

void ARM64Emitter::CSET(Reg rd, Cond cc) {
  // CSINC Xd, XZR, XZR, invert(cc)
  Cond inv = static_cast<Cond>(static_cast<uint8_t>(cc) ^ 1);
  CSINC(rd, Reg::XZR, Reg::XZR, inv);
}

// ── Branches ────────────────────────────────────────────────────────────────

void ARM64Emitter::B(int32_t offset_bytes) {
  int32_t imm26 = offset_bytes >> 2;
  Emit32(0x14000000 | (imm26 & 0x03FFFFFF));
}

void ARM64Emitter::B(Cond cc, int32_t offset_bytes) {
  int32_t imm19 = offset_bytes >> 2;
  Emit32(0x54000000 | ((imm19 & 0x7FFFF) << 5) | static_cast<uint32_t>(cc));
}

void ARM64Emitter::BL(int32_t offset_bytes) {
  int32_t imm26 = offset_bytes >> 2;
  Emit32(0x94000000 | (imm26 & 0x03FFFFFF));
}

void ARM64Emitter::BR(Reg rn) {
  Emit32(0xD61F0000 | Rn(rn));
}

void ARM64Emitter::BLR(Reg rn) {
  Emit32(0xD63F0000 | Rn(rn));
}

void ARM64Emitter::RET(Reg rn) {
  Emit32(0xD65F0000 | Rn(rn));
}

void ARM64Emitter::CBZ(Reg rt, int32_t offset_bytes) {
  int32_t imm19 = offset_bytes >> 2;
  Emit32(0xB4000000 | ((imm19 & 0x7FFFF) << 5) | Rd(rt));
}

void ARM64Emitter::CBNZ(Reg rt, int32_t offset_bytes) {
  int32_t imm19 = offset_bytes >> 2;
  Emit32(0xB5000000 | ((imm19 & 0x7FFFF) << 5) | Rd(rt));
}

// ── Memory Access ───────────────────────────────────────────────────────────

void ARM64Emitter::LDR(Reg rt, Reg rn, int32_t offset) {
  uint32_t imm12 = (offset >> 3) & 0xFFF;  // Scale by 8 for 64-bit
  Emit32(0xF9400000 | (imm12 << 10) | Rn(rn) | Rd(rt));
}

void ARM64Emitter::LDRW(Reg rt, Reg rn, int32_t offset) {
  uint32_t imm12 = (offset >> 2) & 0xFFF;  // Scale by 4 for 32-bit
  Emit32(0xB9400000 | (imm12 << 10) | Rn(rn) | Rd(rt));
}

void ARM64Emitter::LDRH(Reg rt, Reg rn, int32_t offset) {
  uint32_t imm12 = (offset >> 1) & 0xFFF;
  Emit32(0x79400000 | (imm12 << 10) | Rn(rn) | Rd(rt));
}

void ARM64Emitter::LDRB(Reg rt, Reg rn, int32_t offset) {
  uint32_t imm12 = offset & 0xFFF;
  Emit32(0x39400000 | (imm12 << 10) | Rn(rn) | Rd(rt));
}

void ARM64Emitter::STR(Reg rt, Reg rn, int32_t offset) {
  uint32_t imm12 = (offset >> 3) & 0xFFF;
  Emit32(0xF9000000 | (imm12 << 10) | Rn(rn) | Rd(rt));
}

void ARM64Emitter::STRW(Reg rt, Reg rn, int32_t offset) {
  uint32_t imm12 = (offset >> 2) & 0xFFF;
  Emit32(0xB9000000 | (imm12 << 10) | Rn(rn) | Rd(rt));
}

void ARM64Emitter::STRH(Reg rt, Reg rn, int32_t offset) {
  uint32_t imm12 = (offset >> 1) & 0xFFF;
  Emit32(0x79000000 | (imm12 << 10) | Rn(rn) | Rd(rt));
}

void ARM64Emitter::STRB(Reg rt, Reg rn, int32_t offset) {
  uint32_t imm12 = offset & 0xFFF;
  Emit32(0x39000000 | (imm12 << 10) | Rn(rn) | Rd(rt));
}

void ARM64Emitter::LDP(Reg rt1, Reg rt2, Reg rn, int32_t offset) {
  int32_t imm7 = (offset >> 3) & 0x7F;
  Emit32(0xA9400000 | (imm7 << 15) | (static_cast<uint32_t>(rt2) << 10) | Rn(rn) | Rd(rt1));
}

void ARM64Emitter::STP(Reg rt1, Reg rt2, Reg rn, int32_t offset) {
  int32_t imm7 = (offset >> 3) & 0x7F;
  Emit32(0xA9000000 | (imm7 << 15) | (static_cast<uint32_t>(rt2) << 10) | Rn(rn) | Rd(rt1));
}

void ARM64Emitter::LDR_pre(Reg rt, Reg rn, int32_t offset) {
  uint32_t imm9 = offset & 0x1FF;
  Emit32(0xF8400C00 | (imm9 << 12) | Rn(rn) | Rd(rt));
}

void ARM64Emitter::LDR_post(Reg rt, Reg rn, int32_t offset) {
  uint32_t imm9 = offset & 0x1FF;
  Emit32(0xF8400400 | (imm9 << 12) | Rn(rn) | Rd(rt));
}

void ARM64Emitter::STR_pre(Reg rt, Reg rn, int32_t offset) {
  uint32_t imm9 = offset & 0x1FF;
  Emit32(0xF8000C00 | (imm9 << 12) | Rn(rn) | Rd(rt));
}

void ARM64Emitter::STR_post(Reg rt, Reg rn, int32_t offset) {
  uint32_t imm9 = offset & 0x1FF;
  Emit32(0xF8000400 | (imm9 << 12) | Rn(rn) | Rd(rt));
}

// ── NEON ────────────────────────────────────────────────────────────────────

void ARM64Emitter::FMOV_vtog(Reg rd, VReg vn) {
  Emit32(0x9E660000 | Vn(vn) | Rd(rd));
}

void ARM64Emitter::FMOV_gtov(VReg vd, Reg rn) {
  Emit32(0x9E670000 | Rn(rn) | Vd(vd));
}

void ARM64Emitter::LDR_v128(VReg vt, Reg rn, int32_t offset) {
  uint32_t imm12 = (offset >> 4) & 0xFFF;
  Emit32(0x3DC00000 | (imm12 << 10) | Rn(rn) | Vd(vt));
}

void ARM64Emitter::STR_v128(VReg vt, Reg rn, int32_t offset) {
  uint32_t imm12 = (offset >> 4) & 0xFFF;
  Emit32(0x3D800000 | (imm12 << 10) | Rn(rn) | Vd(vt));
}

void ARM64Emitter::FADD_4s(VReg vd, VReg vn, VReg vm) {
  Emit32(0x4E20D400 | Vm(vm) | Vn(vn) | Vd(vd));
}

void ARM64Emitter::FSUB_4s(VReg vd, VReg vn, VReg vm) {
  Emit32(0x4EA0D400 | Vm(vm) | Vn(vn) | Vd(vd));
}

void ARM64Emitter::FMUL_4s(VReg vd, VReg vn, VReg vm) {
  Emit32(0x6E20DC00 | Vm(vm) | Vn(vn) | Vd(vd));
}

void ARM64Emitter::FDIV_4s(VReg vd, VReg vn, VReg vm) {
  Emit32(0x6E20FC00 | Vm(vm) | Vn(vn) | Vd(vd));
}

void ARM64Emitter::FMLA_4s(VReg vd, VReg vn, VReg vm) {
  Emit32(0x4E20CC00 | Vm(vm) | Vn(vn) | Vd(vd));
}

void ARM64Emitter::FABS_4s(VReg vd, VReg vn) {
  Emit32(0x4EA0F800 | Vn(vn) | Vd(vd));
}

void ARM64Emitter::FNEG_4s(VReg vd, VReg vn) {
  Emit32(0x6EA0F800 | Vn(vn) | Vd(vd));
}

void ARM64Emitter::FSQRT_4s(VReg vd, VReg vn) {
  Emit32(0x6EA1F800 | Vn(vn) | Vd(vd));
}

void ARM64Emitter::FMIN_4s(VReg vd, VReg vn, VReg vm) {
  Emit32(0x4EA0F400 | Vm(vm) | Vn(vn) | Vd(vd));
}

void ARM64Emitter::FMAX_4s(VReg vd, VReg vn, VReg vm) {
  Emit32(0x4E20F400 | Vm(vm) | Vn(vn) | Vd(vd));
}

void ARM64Emitter::AND_v(VReg vd, VReg vn, VReg vm) {
  Emit32(0x4E201C00 | Vm(vm) | Vn(vn) | Vd(vd));
}

void ARM64Emitter::ORR_v(VReg vd, VReg vn, VReg vm) {
  Emit32(0x4EA01C00 | Vm(vm) | Vn(vn) | Vd(vd));
}

void ARM64Emitter::EOR_v(VReg vd, VReg vn, VReg vm) {
  Emit32(0x6E201C00 | Vm(vm) | Vn(vn) | Vd(vd));
}

void ARM64Emitter::DUP_4s(VReg vd, VReg vn, uint8_t index) {
  uint32_t imm5 = ((index & 3) << 3) | 0x04;  // size=10 (32-bit), index in upper bits
  Emit32(0x5E000400 | (imm5 << 16) | Vn(vn) | Vd(vd));
}

void ARM64Emitter::INS_4s(VReg vd, uint8_t dst_idx, VReg vn, uint8_t src_idx) {
  uint32_t imm5 = ((dst_idx & 3) << 3) | 0x04;
  uint32_t imm4 = (src_idx & 3) << 1;
  Emit32(0x6E000400 | (imm5 << 16) | (imm4 << 11) | Vn(vn) | Vd(vd));
}

void ARM64Emitter::FCVTZS_4s(VReg vd, VReg vn) {
  Emit32(0x4EA1B800 | Vn(vn) | Vd(vd));
}

void ARM64Emitter::SCVTF_4s(VReg vd, VReg vn) {
  Emit32(0x4E21D800 | Vn(vn) | Vd(vd));
}

// ── System ──────────────────────────────────────────────────────────────────

void ARM64Emitter::NOP() {
  Emit32(0xD503201F);
}

void ARM64Emitter::BRK(uint16_t imm) {
  Emit32(0xD4200000 | (static_cast<uint32_t>(imm) << 5));
}

void ARM64Emitter::DMB_ISH() {
  Emit32(0xD5033BBF);  // DMB ISH
}

void ARM64Emitter::DSB_ISH() {
  Emit32(0xD5033B9F);  // DSB ISH
}

void ARM64Emitter::ISB() {
  Emit32(0xD5033FDF);
}

void ARM64Emitter::SVC(uint16_t imm) {
  Emit32(0xD4000001 | (static_cast<uint32_t>(imm) << 5));
}

void ARM64Emitter::MRS(Reg rt, uint32_t sysreg) {
  Emit32(0xD5300000 | sysreg | Rd(rt));
}

void ARM64Emitter::MSR(uint32_t sysreg, Reg rt) {
  Emit32(0xD5100000 | sysreg | Rd(rt));
}

// ── Label patching ──────────────────────────────────────────────────────────

void ARM64Emitter::PatchBranch(size_t branch_offset, size_t target_offset) {
  int32_t delta = static_cast<int32_t>(target_offset - branch_offset);
  int32_t imm26 = delta >> 2;
  
  uint32_t* instr = reinterpret_cast<uint32_t*>(code_.data() + branch_offset);
  *instr = (*instr & 0xFC000000) | (imm26 & 0x03FFFFFF);
}

void ARM64Emitter::PatchCondBranch(size_t branch_offset, size_t target_offset) {
  int32_t delta = static_cast<int32_t>(target_offset - branch_offset);
  int32_t imm19 = delta >> 2;
  
  uint32_t* instr = reinterpret_cast<uint32_t*>(code_.data() + branch_offset);
  *instr = (*instr & 0xFF00001F) | ((imm19 & 0x7FFFF) << 5);
}

// ── Extended integer ────────────────────────────────────────────────────────

void ARM64Emitter::MADD(Reg rd, Reg rn, Reg rm, Reg ra) {
  Emit32(0x9B000000 | Rm(rm) | (static_cast<uint32_t>(ra) << 10) | Rn(rn) | Rd(rd));
}
void ARM64Emitter::MSUB(Reg rd, Reg rn, Reg rm, Reg ra) {
  Emit32(0x9B008000 | Rm(rm) | (static_cast<uint32_t>(ra) << 10) | Rn(rn) | Rd(rd));
}
void ARM64Emitter::SMADDL(Reg rd, Reg rn, Reg rm, Reg ra) {
  Emit32(0x9B200000 | Rm(rm) | (static_cast<uint32_t>(ra) << 10) | Rn(rn) | Rd(rd));
}
void ARM64Emitter::UMADDL(Reg rd, Reg rn, Reg rm, Reg ra) {
  Emit32(0x9BA00000 | Rm(rm) | (static_cast<uint32_t>(ra) << 10) | Rn(rn) | Rd(rd));
}
void ARM64Emitter::SMULH(Reg rd, Reg rn, Reg rm) {
  Emit32(0x9B407C00 | Rm(rm) | Rn(rn) | Rd(rd));
}
void ARM64Emitter::UMULH(Reg rd, Reg rn, Reg rm) {
  Emit32(0x9BC07C00 | Rm(rm) | Rn(rn) | Rd(rd));
}
void ARM64Emitter::ADC(Reg rd, Reg rn, Reg rm) {
  Emit32(0x9A000000 | Rm(rm) | Rn(rn) | Rd(rd));
}
void ARM64Emitter::ADCS(Reg rd, Reg rn, Reg rm) {
  Emit32(0xBA000000 | Rm(rm) | Rn(rn) | Rd(rd));
}
void ARM64Emitter::SBC(Reg rd, Reg rn, Reg rm) {
  Emit32(0xDA000000 | Rm(rm) | Rn(rn) | Rd(rd));
}
void ARM64Emitter::SBCS(Reg rd, Reg rn, Reg rm) {
  Emit32(0xFA000000 | Rm(rm) | Rn(rn) | Rd(rd));
}
void ARM64Emitter::SXTW(Reg rd, Reg rn) {
  // SBFM Xd, Xn, #0, #31
  Emit32(0x93407C00 | Rn(rn) | Rd(rd));
}
void ARM64Emitter::SXTH(Reg rd, Reg rn) {
  // SBFM Xd, Xn, #0, #15
  Emit32(0x93403C00 | Rn(rn) | Rd(rd));
}
void ARM64Emitter::SXTB(Reg rd, Reg rn) {
  // SBFM Xd, Xn, #0, #7
  Emit32(0x93401C00 | Rn(rn) | Rd(rd));
}
void ARM64Emitter::UXTW(Reg rd, Reg rn) {
  // UBFM Wd, Wn, #0, #31  (MOV Wd, Wn essentially)
  Emit32(0x2A0003E0 | Rm(rn) | Rd(rd));  // ORR Wd, WZR, Wn
}
void ARM64Emitter::UXTH(Reg rd, Reg rn) {
  // UBFM Xd, Xn, #0, #15
  Emit32(0xD3403C00 | Rn(rn) | Rd(rd));
}
void ARM64Emitter::UXTB(Reg rd, Reg rn) {
  // UBFM Xd, Xn, #0, #7
  Emit32(0xD3401C00 | Rn(rn) | Rd(rd));
}
void ARM64Emitter::BIC(Reg rd, Reg rn, Reg rm) {
  Emit32(0x8A200000 | Rm(rm) | Rn(rn) | Rd(rd));
}
void ARM64Emitter::BICS(Reg rd, Reg rn, Reg rm) {
  Emit32(0xEA200000 | Rm(rm) | Rn(rn) | Rd(rd));
}
void ARM64Emitter::MVN(Reg rd, Reg rm) {
  ORN(rd, Reg::XZR, rm);
}
void ARM64Emitter::EON(Reg rd, Reg rn, Reg rm) {
  Emit32(0xCA200000 | Rm(rm) | Rn(rn) | Rd(rd));
}
void ARM64Emitter::ANDS(Reg rd, Reg rn, Reg rm) {
  Emit32(0xEA000000 | Rm(rm) | Rn(rn) | Rd(rd));
}
void ARM64Emitter::UBFM(Reg rd, Reg rn, uint8_t immr, uint8_t imms) {
  Emit32(0xD3400000 | ((immr & 0x3F) << 16) | ((imms & 0x3F) << 10) | Rn(rn) | Rd(rd));
}
void ARM64Emitter::SBFM(Reg rd, Reg rn, uint8_t immr, uint8_t imms) {
  Emit32(0x93400000 | ((immr & 0x3F) << 16) | ((imms & 0x3F) << 10) | Rn(rn) | Rd(rd));
}
void ARM64Emitter::EXTR(Reg rd, Reg rn, Reg rm, uint8_t lsb) {
  Emit32(0x93C00000 | Rm(rm) | ((lsb & 0x3F) << 10) | Rn(rn) | Rd(rd));
}
void ARM64Emitter::CCMP(Reg rn, Reg rm, uint8_t nzcv, Cond cc) {
  Emit32(0xFA400000 | (static_cast<uint32_t>(cc) << 12) | Rm(rm) | Rn(rn) | (nzcv & 0xF));
}
void ARM64Emitter::CSINV(Reg rd, Reg rn, Reg rm, Cond cc) {
  Emit32(0xDA800000 | (static_cast<uint32_t>(cc) << 12) | Rm(rm) | Rn(rn) | Rd(rd));
}
void ARM64Emitter::CSNEG(Reg rd, Reg rn, Reg rm, Cond cc) {
  Emit32(0xDA800400 | (static_cast<uint32_t>(cc) << 12) | Rm(rm) | Rn(rn) | Rd(rd));
}

// ── Load/store register offset ──────────────────────────────────────────────

void ARM64Emitter::LDR_reg(Reg rt, Reg rn, Reg rm) {
  Emit32(0xF8606800 | Rm(rm) | Rn(rn) | Rd(rt));
}
void ARM64Emitter::LDRW_reg(Reg rt, Reg rn, Reg rm) {
  Emit32(0xB8606800 | Rm(rm) | Rn(rn) | Rd(rt));
}
void ARM64Emitter::LDRH_reg(Reg rt, Reg rn, Reg rm) {
  Emit32(0x78606800 | Rm(rm) | Rn(rn) | Rd(rt));
}
void ARM64Emitter::LDRB_reg(Reg rt, Reg rn, Reg rm) {
  Emit32(0x38606800 | Rm(rm) | Rn(rn) | Rd(rt));
}
void ARM64Emitter::STR_reg(Reg rt, Reg rn, Reg rm) {
  Emit32(0xF8206800 | Rm(rm) | Rn(rn) | Rd(rt));
}
void ARM64Emitter::STRW_reg(Reg rt, Reg rn, Reg rm) {
  Emit32(0xB8206800 | Rm(rm) | Rn(rn) | Rd(rt));
}
void ARM64Emitter::STRH_reg(Reg rt, Reg rn, Reg rm) {
  Emit32(0x78206800 | Rm(rm) | Rn(rn) | Rd(rt));
}
void ARM64Emitter::STRB_reg(Reg rt, Reg rn, Reg rm) {
  Emit32(0x38206800 | Rm(rm) | Rn(rn) | Rd(rt));
}
void ARM64Emitter::LDRSW(Reg rt, Reg rn, int32_t offset) {
  uint32_t imm12 = (offset >> 2) & 0xFFF;
  Emit32(0xB9800000 | (imm12 << 10) | Rn(rn) | Rd(rt));
}
void ARM64Emitter::LDRSH(Reg rt, Reg rn, int32_t offset) {
  uint32_t imm12 = (offset >> 1) & 0xFFF;
  Emit32(0x79800000 | (imm12 << 10) | Rn(rn) | Rd(rt));
}
void ARM64Emitter::LDRSB(Reg rt, Reg rn, int32_t offset) {
  uint32_t imm12 = offset & 0xFFF;
  Emit32(0x39800000 | (imm12 << 10) | Rn(rn) | Rd(rt));
}
void ARM64Emitter::LDAXR(Reg rt, Reg rn) {
  Emit32(0xC85FFC00 | Rn(rn) | Rd(rt));
}
void ARM64Emitter::STLXR(Reg rs, Reg rt, Reg rn) {
  Emit32(0xC800FC00 | Rm(rs) | Rn(rn) | Rd(rt));
}
void ARM64Emitter::LDAXRW(Reg rt, Reg rn) {
  Emit32(0x885FFC00 | Rn(rn) | Rd(rt));
}
void ARM64Emitter::STLXRW(Reg rs, Reg rt, Reg rn) {
  Emit32(0x8800FC00 | Rm(rs) | Rn(rn) | Rd(rt));
}
void ARM64Emitter::CLREX() {
  Emit32(0xD503305F);
}

// ── Scalar FP ───────────────────────────────────────────────────────────────

void ARM64Emitter::FADD_d(VReg vd, VReg vn, VReg vm) {
  Emit32(0x1E602800 | Vm(vm) | Vn(vn) | Vd(vd));
}
void ARM64Emitter::FSUB_d(VReg vd, VReg vn, VReg vm) {
  Emit32(0x1E603800 | Vm(vm) | Vn(vn) | Vd(vd));
}
void ARM64Emitter::FMUL_d(VReg vd, VReg vn, VReg vm) {
  Emit32(0x1E600800 | Vm(vm) | Vn(vn) | Vd(vd));
}
void ARM64Emitter::FDIV_d(VReg vd, VReg vn, VReg vm) {
  Emit32(0x1E601800 | Vm(vm) | Vn(vn) | Vd(vd));
}
void ARM64Emitter::FMADD_d(VReg vd, VReg vn, VReg vm, VReg va) {
  Emit32(0x1F400000 | Vm(vm) | (static_cast<uint32_t>(va) << 10) | Vn(vn) | Vd(vd));
}
void ARM64Emitter::FMSUB_d(VReg vd, VReg vn, VReg vm, VReg va) {
  Emit32(0x1F408000 | Vm(vm) | (static_cast<uint32_t>(va) << 10) | Vn(vn) | Vd(vd));
}
void ARM64Emitter::FNMADD_d(VReg vd, VReg vn, VReg vm, VReg va) {
  Emit32(0x1F600000 | Vm(vm) | (static_cast<uint32_t>(va) << 10) | Vn(vn) | Vd(vd));
}
void ARM64Emitter::FNMSUB_d(VReg vd, VReg vn, VReg vm, VReg va) {
  Emit32(0x1F608000 | Vm(vm) | (static_cast<uint32_t>(va) << 10) | Vn(vn) | Vd(vd));
}
void ARM64Emitter::FABS_d(VReg vd, VReg vn) {
  Emit32(0x1E60C000 | Vn(vn) | Vd(vd));
}
void ARM64Emitter::FNEG_d(VReg vd, VReg vn) {
  Emit32(0x1E614000 | Vn(vn) | Vd(vd));
}
void ARM64Emitter::FSQRT_d(VReg vd, VReg vn) {
  Emit32(0x1E61C000 | Vn(vn) | Vd(vd));
}
void ARM64Emitter::FMOV_d(VReg vd, VReg vn) {
  Emit32(0x1E604000 | Vn(vn) | Vd(vd));
}
void ARM64Emitter::FCMP_d(VReg vn, VReg vm) {
  Emit32(0x1E602000 | Vm(vm) | Vn(vn));
}
void ARM64Emitter::FCMP_dz(VReg vn) {
  Emit32(0x1E602008 | Vn(vn));
}
void ARM64Emitter::FADD_s(VReg vd, VReg vn, VReg vm) {
  Emit32(0x1E202800 | Vm(vm) | Vn(vn) | Vd(vd));
}
void ARM64Emitter::FSUB_s(VReg vd, VReg vn, VReg vm) {
  Emit32(0x1E203800 | Vm(vm) | Vn(vn) | Vd(vd));
}
void ARM64Emitter::FMUL_s(VReg vd, VReg vn, VReg vm) {
  Emit32(0x1E200800 | Vm(vm) | Vn(vn) | Vd(vd));
}
void ARM64Emitter::FDIV_s(VReg vd, VReg vn, VReg vm) {
  Emit32(0x1E201800 | Vm(vm) | Vn(vn) | Vd(vd));
}
void ARM64Emitter::FMADD_s(VReg vd, VReg vn, VReg vm, VReg va) {
  Emit32(0x1F000000 | Vm(vm) | (static_cast<uint32_t>(va) << 10) | Vn(vn) | Vd(vd));
}
void ARM64Emitter::FMSUB_s(VReg vd, VReg vn, VReg vm, VReg va) {
  Emit32(0x1F008000 | Vm(vm) | (static_cast<uint32_t>(va) << 10) | Vn(vn) | Vd(vd));
}
void ARM64Emitter::FABS_s(VReg vd, VReg vn) {
  Emit32(0x1E20C000 | Vn(vn) | Vd(vd));
}
void ARM64Emitter::FNEG_s(VReg vd, VReg vn) {
  Emit32(0x1E214000 | Vn(vn) | Vd(vd));
}
void ARM64Emitter::FSQRT_s(VReg vd, VReg vn) {
  Emit32(0x1E21C000 | Vn(vn) | Vd(vd));
}
void ARM64Emitter::FMOV_s(VReg vd, VReg vn) {
  Emit32(0x1E204000 | Vn(vn) | Vd(vd));
}
void ARM64Emitter::FCVT_sd(VReg vd, VReg vn) {
  // FCVT Dd, Sn
  Emit32(0x1E22C000 | Vn(vn) | Vd(vd));
}
void ARM64Emitter::FCVT_ds(VReg vd, VReg vn) {
  // FCVT Sd, Dn
  Emit32(0x1E624000 | Vn(vn) | Vd(vd));
}
void ARM64Emitter::FCVTZS_xd(Reg rd, VReg vn) {
  Emit32(0x9E780000 | Vn(vn) | Rd(rd));
}
void ARM64Emitter::FCVTZS_wd(Reg rd, VReg vn) {
  Emit32(0x1E780000 | Vn(vn) | Rd(rd));
}
void ARM64Emitter::SCVTF_dx(VReg vd, Reg rn) {
  Emit32(0x9E620000 | Rn(rn) | Vd(vd));
}
void ARM64Emitter::SCVTF_dw(VReg vd, Reg rn) {
  Emit32(0x1E620000 | Rn(rn) | Vd(vd));
}
void ARM64Emitter::UCVTF_dx(VReg vd, Reg rn) {
  Emit32(0x9E630000 | Rn(rn) | Vd(vd));
}
void ARM64Emitter::FMOV_dtog(Reg rd, VReg vn) {
  Emit32(0x9E660000 | Vn(vn) | Rd(rd));
}
void ARM64Emitter::FMOV_gtod(VReg vd, Reg rn) {
  Emit32(0x9E670000 | Rn(rn) | Vd(vd));
}
void ARM64Emitter::FMOV_stog(Reg rd, VReg vn) {
  Emit32(0x1E260000 | Vn(vn) | Rd(rd));
}
void ARM64Emitter::FMOV_gtos(VReg vd, Reg rn) {
  Emit32(0x1E270000 | Rn(rn) | Vd(vd));
}
void ARM64Emitter::FRECPE_d(VReg vd, VReg vn) {
  Emit32(0x5EE1D800 | Vn(vn) | Vd(vd));
}
void ARM64Emitter::FRSQRTE_d(VReg vd, VReg vn) {
  Emit32(0x7EE1D800 | Vn(vn) | Vd(vd));
}
void ARM64Emitter::FCSEL_d(VReg vd, VReg vn, VReg vm, Cond cc) {
  Emit32(0x1E600C00 | (static_cast<uint32_t>(cc) << 12) | Vm(vm) | Vn(vn) | Vd(vd));
}
void ARM64Emitter::LDR_d(VReg vt, Reg rn, int32_t offset) {
  uint32_t imm12 = (offset >> 3) & 0xFFF;
  Emit32(0xFD400000 | (imm12 << 10) | Rn(rn) | Vd(vt));
}
void ARM64Emitter::STR_d(VReg vt, Reg rn, int32_t offset) {
  uint32_t imm12 = (offset >> 3) & 0xFFF;
  Emit32(0xFD000000 | (imm12 << 10) | Rn(rn) | Vd(vt));
}
void ARM64Emitter::LDR_s(VReg vt, Reg rn, int32_t offset) {
  uint32_t imm12 = (offset >> 2) & 0xFFF;
  Emit32(0xBD400000 | (imm12 << 10) | Rn(rn) | Vd(vt));
}
void ARM64Emitter::STR_s(VReg vt, Reg rn, int32_t offset) {
  uint32_t imm12 = (offset >> 2) & 0xFFF;
  Emit32(0xBD000000 | (imm12 << 10) | Rn(rn) | Vd(vt));
}

// ── Additional NEON ─────────────────────────────────────────────────────────

void ARM64Emitter::MOV_v(VReg vd, VReg vn) {
  ORR_v(vd, vn, vn);
}
void ARM64Emitter::MOVI_v(VReg vd, uint8_t imm8) {
  // MOVI Vd.4S, #imm8
  uint32_t a = (imm8 >> 7) & 1;
  uint32_t bcd = (imm8 >> 4) & 7;
  uint32_t efgh = imm8 & 0xF;
  Emit32(0x4F000400 | (a << 18) | (bcd << 16) | (efgh << 5) | Vd(vd));
}
void ARM64Emitter::NOT_v(VReg vd, VReg vn) {
  Emit32(0x6E205800 | Vn(vn) | Vd(vd));
}
void ARM64Emitter::BSL_v(VReg vd, VReg vn, VReg vm) {
  Emit32(0x6E601C00 | Vm(vm) | Vn(vn) | Vd(vd));
}
void ARM64Emitter::BIF_v(VReg vd, VReg vn, VReg vm) {
  Emit32(0x6EE01C00 | Vm(vm) | Vn(vn) | Vd(vd));
}
void ARM64Emitter::BIT_v(VReg vd, VReg vn, VReg vm) {
  Emit32(0x6EA01C00 | Vm(vm) | Vn(vn) | Vd(vd));
}
void ARM64Emitter::FRINTM_4s(VReg vd, VReg vn) {
  Emit32(0x4E219800 | Vn(vn) | Vd(vd));
}
void ARM64Emitter::FRINTP_4s(VReg vd, VReg vn) {
  Emit32(0x4EA18800 | Vn(vn) | Vd(vd));
}
void ARM64Emitter::FRINTZ_4s(VReg vd, VReg vn) {
  Emit32(0x4EA19800 | Vn(vn) | Vd(vd));
}
void ARM64Emitter::FRINTN_4s(VReg vd, VReg vn) {
  Emit32(0x4E218800 | Vn(vn) | Vd(vd));
}
void ARM64Emitter::FRECPE_4s(VReg vd, VReg vn) {
  Emit32(0x4EA1D800 | Vn(vn) | Vd(vd));
}
void ARM64Emitter::FRSQRTE_4s(VReg vd, VReg vn) {
  Emit32(0x6EA1D800 | Vn(vn) | Vd(vd));
}
void ARM64Emitter::ADDV_4s(VReg vd, VReg vn) {
  Emit32(0x4EB1B800 | Vn(vn) | Vd(vd));
}
void ARM64Emitter::FCMEQ_4s(VReg vd, VReg vn, VReg vm) {
  Emit32(0x4E20E400 | Vm(vm) | Vn(vn) | Vd(vd));
}
void ARM64Emitter::FCMGT_4s(VReg vd, VReg vn, VReg vm) {
  Emit32(0x6EA0E400 | Vm(vm) | Vn(vn) | Vd(vd));
}
void ARM64Emitter::FCMGE_4s(VReg vd, VReg vn, VReg vm) {
  Emit32(0x6E20E400 | Vm(vm) | Vn(vn) | Vd(vd));
}
void ARM64Emitter::ADD_4s(VReg vd, VReg vn, VReg vm) {
  Emit32(0x4EA08400 | Vm(vm) | Vn(vn) | Vd(vd));
}
void ARM64Emitter::SUB_4s(VReg vd, VReg vn, VReg vm) {
  Emit32(0x6EA08400 | Vm(vm) | Vn(vn) | Vd(vd));
}
void ARM64Emitter::MUL_4s(VReg vd, VReg vn, VReg vm) {
  Emit32(0x4EA09C00 | Vm(vm) | Vn(vn) | Vd(vd));
}
void ARM64Emitter::SHL_4s(VReg vd, VReg vn, uint8_t shift) {
  uint8_t immh_imml = 0x20 | (shift & 0x1F);
  Emit32(0x4F005400 | (immh_imml << 16) | Vn(vn) | Vd(vd));
}
void ARM64Emitter::SSHR_4s(VReg vd, VReg vn, uint8_t shift) {
  uint8_t immh_imml = 0x40 - (shift & 0x1F);
  Emit32(0x4F000400 | (immh_imml << 16) | Vn(vn) | Vd(vd));
}
void ARM64Emitter::USHR_4s(VReg vd, VReg vn, uint8_t shift) {
  uint8_t immh_imml = 0x40 - (shift & 0x1F);
  Emit32(0x6F000400 | (immh_imml << 16) | Vn(vn) | Vd(vd));
}
void ARM64Emitter::CMPEQ_4s(VReg vd, VReg vn, VReg vm) {
  Emit32(0x6EA08C00 | Vm(vm) | Vn(vn) | Vd(vd));
}
void ARM64Emitter::CMGT_4s(VReg vd, VReg vn, VReg vm) {
  Emit32(0x4EA03400 | Vm(vm) | Vn(vn) | Vd(vd));
}
void ARM64Emitter::TBL_v(VReg vd, VReg vn, VReg vm) {
  Emit32(0x4E000000 | Vm(vm) | Vn(vn) | Vd(vd));
}
void ARM64Emitter::ZIP1_4s(VReg vd, VReg vn, VReg vm) {
  Emit32(0x4E803800 | Vm(vm) | Vn(vn) | Vd(vd));
}
void ARM64Emitter::ZIP2_4s(VReg vd, VReg vn, VReg vm) {
  Emit32(0x4E807800 | Vm(vm) | Vn(vn) | Vd(vd));
}
void ARM64Emitter::UZP1_4s(VReg vd, VReg vn, VReg vm) {
  Emit32(0x4E801800 | Vm(vm) | Vn(vn) | Vd(vd));
}
void ARM64Emitter::UZP2_4s(VReg vd, VReg vn, VReg vm) {
  Emit32(0x4E805800 | Vm(vm) | Vn(vn) | Vd(vd));
}
void ARM64Emitter::REV64_4s(VReg vd, VReg vn) {
  Emit32(0x4EA00800 | Vn(vn) | Vd(vd));
}
void ARM64Emitter::EXT_v(VReg vd, VReg vn, VReg vm, uint8_t idx) {
  Emit32(0x6E000000 | ((idx & 0xF) << 11) | Vm(vm) | Vn(vn) | Vd(vd));
}
void ARM64Emitter::SMIN_4s(VReg vd, VReg vn, VReg vm) {
  Emit32(0x4EA06C00 | Vm(vm) | Vn(vn) | Vd(vd));
}
void ARM64Emitter::SMAX_4s(VReg vd, VReg vn, VReg vm) {
  Emit32(0x4EA06400 | Vm(vm) | Vn(vn) | Vd(vd));
}
void ARM64Emitter::ABS_4s(VReg vd, VReg vn) {
  Emit32(0x4EA0B800 | Vn(vn) | Vd(vd));
}
void ARM64Emitter::NEG_4s(VReg vd, VReg vn) {
  Emit32(0x6EA0B800 | Vn(vn) | Vd(vd));
}

}  // namespace xe::cpu::backend::arm64
