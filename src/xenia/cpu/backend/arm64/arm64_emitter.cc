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

}  // namespace xe::cpu::backend::arm64
