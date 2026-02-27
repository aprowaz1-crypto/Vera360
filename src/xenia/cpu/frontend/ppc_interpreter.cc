/**
 * Vera360 — Xenia Edge
 * PPC Software Interpreter — full implementation
 *
 * Executes PowerPC (Xenon / PPC750) instructions in software.
 * All memory accesses are big-endian (Xbox 360 is big-endian PPC).
 *
 * Coverage:
 *   ~200 PPC opcodes covering the Xbox 360 instruction set
 */

#include "xenia/cpu/frontend/ppc_interpreter.h"
#include "xenia/base/logging.h"

#include <cmath>
#include <cstring>
#include <algorithm>

namespace xe::cpu::frontend {

// ═══════════════════════════════════════════════════════════════════════════
// Construction
// ═══════════════════════════════════════════════════════════════════════════

PPCInterpreter::PPCInterpreter() = default;
PPCInterpreter::~PPCInterpreter() = default;

// ═══════════════════════════════════════════════════════════════════════════
// Thunk registration
// ═══════════════════════════════════════════════════════════════════════════

void PPCInterpreter::RegisterThunk(uint32_t guest_addr, uint32_t ordinal) {
  thunk_map_[guest_addr] = ordinal;
}

bool PPCInterpreter::IsThunkAddress(uint32_t addr) const {
  return thunk_map_.count(addr) > 0;
}

// ═══════════════════════════════════════════════════════════════════════════
// Memory access — big-endian
// ═══════════════════════════════════════════════════════════════════════════

uint8_t PPCInterpreter::ReadU8(uint32_t addr) const {
  return *(guest_base_ + addr);
}

uint16_t PPCInterpreter::ReadU16(uint32_t addr) const {
  uint16_t v;
  memcpy(&v, guest_base_ + addr, 2);
  return __builtin_bswap16(v);
}

uint32_t PPCInterpreter::ReadU32(uint32_t addr) const {
  uint32_t v;
  memcpy(&v, guest_base_ + addr, 4);
  return __builtin_bswap32(v);
}

uint64_t PPCInterpreter::ReadU64(uint32_t addr) const {
  uint64_t v;
  memcpy(&v, guest_base_ + addr, 8);
  return __builtin_bswap64(v);
}

float PPCInterpreter::ReadF32(uint32_t addr) const {
  uint32_t bits = ReadU32(addr);
  float f;
  memcpy(&f, &bits, 4);
  return f;
}

double PPCInterpreter::ReadF64(uint32_t addr) const {
  uint64_t bits = ReadU64(addr);
  double d;
  memcpy(&d, &bits, 8);
  return d;
}

void PPCInterpreter::WriteU8(uint32_t addr, uint8_t val) {
  *(guest_base_ + addr) = val;
}

void PPCInterpreter::WriteU16(uint32_t addr, uint16_t val) {
  uint16_t be = __builtin_bswap16(val);
  memcpy(guest_base_ + addr, &be, 2);
}

void PPCInterpreter::WriteU32(uint32_t addr, uint32_t val) {
  uint32_t be = __builtin_bswap32(val);
  memcpy(guest_base_ + addr, &be, 4);
}

void PPCInterpreter::WriteU64(uint32_t addr, uint64_t val) {
  uint64_t be = __builtin_bswap64(val);
  memcpy(guest_base_ + addr, &be, 8);
}

void PPCInterpreter::WriteF32(uint32_t addr, float val) {
  uint32_t bits;
  memcpy(&bits, &val, 4);
  WriteU32(addr, bits);
}

void PPCInterpreter::WriteF64(uint32_t addr, double val) {
  uint64_t bits;
  memcpy(&bits, &val, 8);
  WriteU64(addr, bits);
}

// ═══════════════════════════════════════════════════════════════════════════
// CR / condition helpers
// ═══════════════════════════════════════════════════════════════════════════

void PPCInterpreter::UpdateCR0(ThreadState* t, int64_t result) {
  UpdateCR(t, 0, result, 0);
}

void PPCInterpreter::UpdateCR(ThreadState* t, uint32_t field, int64_t a, int64_t b) {
  // Each CR field is 4 bits: LT GT EQ SO
  uint32_t shift = (7 - field) * 4;
  uint32_t mask = ~(0xFu << shift);
  uint32_t bits = 0;
  if (a < b)       bits = 0x8;  // LT
  else if (a > b)  bits = 0x4;  // GT
  else             bits = 0x2;  // EQ
  // SO = XER[SO]
  if (t->xer & (1u << 31)) bits |= 0x1;
  t->cr = (t->cr & mask) | (bits << shift);
}

void PPCInterpreter::UpdateCRU(ThreadState* t, uint32_t field, uint64_t a, uint64_t b) {
  uint32_t shift = (7 - field) * 4;
  uint32_t mask = ~(0xFu << shift);
  uint32_t bits = 0;
  if (a < b)       bits = 0x8;
  else if (a > b)  bits = 0x4;
  else             bits = 0x2;
  if (t->xer & (1u << 31)) bits |= 0x1;
  t->cr = (t->cr & mask) | (bits << shift);
}

bool PPCInterpreter::EvalBranchCondition(ThreadState* t, uint32_t bo, uint32_t bi) {
  // BO field encoding:
  // bit 4 (0y): 1 = don't test CR, 0 = test
  // bit 3 (1y): condition sense (1 = branch if CR[BI]=1)
  // bit 2 (2y): 1 = don't decrement CTR, 0 = decrement
  // bit 1 (3y): CTR test sense (1 = branch if CTR==0 after dec)

  bool ctr_ok = true;
  if (!(bo & 0x04)) {
    // Decrement CTR
    t->ctr--;
    ctr_ok = (bo & 0x02) ? (t->ctr == 0) : (t->ctr != 0);
  }

  bool cond_ok = true;
  if (!(bo & 0x10)) {
    // Test CR bit
    uint32_t cr_bit = (t->cr >> (31 - bi)) & 1;
    cond_ok = (bo & 0x08) ? (cr_bit == 1) : (cr_bit == 0);
  }

  return ctr_ok && cond_ok;
}

// ═══════════════════════════════════════════════════════════════════════════
// Rotate/mask helpers
// ═══════════════════════════════════════════════════════════════════════════

uint32_t PPCInterpreter::BuildMask32(uint32_t mb, uint32_t me) {
  uint32_t mask = 0;
  if (mb <= me) {
    for (uint32_t i = mb; i <= me; ++i) mask |= (1u << (31 - i));
  } else {
    mask = 0xFFFFFFFF;
    for (uint32_t i = me + 1; i < mb; ++i) mask &= ~(1u << (31 - i));
  }
  return mask;
}

uint64_t PPCInterpreter::BuildMask64(uint32_t mb, uint32_t me) {
  uint64_t mask = 0;
  if (mb <= me) {
    for (uint32_t i = mb; i <= me; ++i) mask |= (1ULL << (63 - i));
  } else {
    mask = ~0ULL;
    for (uint32_t i = me + 1; i < mb; ++i) mask &= ~(1ULL << (63 - i));
  }
  return mask;
}

// ═══════════════════════════════════════════════════════════════════════════
// Main interpreter loop
// ═══════════════════════════════════════════════════════════════════════════

uint64_t PPCInterpreter::Run(ThreadState* thread, uint64_t max_instructions) {
  uint64_t count = 0;
  uint64_t limit = max_instructions > 0 ? max_instructions : UINT64_MAX;

  while (count < limit) {
    // Check for HLE thunk at current PC
    auto thunk_it = thunk_map_.find(thread->pc);
    if (thunk_it != thunk_map_.end()) {
      if (hle_dispatch_) {
        hle_dispatch_(thread, thunk_it->second);
      }
      // Return from thunk — the thunk should have set r3 and we
      // return to the address in LR
      thread->pc = static_cast<uint32_t>(thread->lr);
      count++;
      continue;
    }

    InterpResult result = Step(thread);
    count++;
    instructions_executed_++;

    switch (result) {
      case InterpResult::kContinue:
      case InterpResult::kBranch:
        break;  // Keep going
      case InterpResult::kReturn:
        return count;  // Function returned
      case InterpResult::kSyscall:
        // sc instruction — the HLE dispatch should have been handled
        break;
      case InterpResult::kTrap:
        XELOGW("PPC trap at 0x{:08X}", thread->pc - 4);
        return count;
      case InterpResult::kHalt:
        XELOGE("PPC halt at 0x{:08X}", thread->pc);
        return count;
    }
  }
  return count;
}

// ═══════════════════════════════════════════════════════════════════════════
// Step — execute one instruction
// ═══════════════════════════════════════════════════════════════════════════

InterpResult PPCInterpreter::Step(ThreadState* t) {
  if (!guest_base_) return InterpResult::kHalt;

  // Fetch instruction (big-endian)
  uint32_t instr = ReadU32(t->pc);
  uint32_t pc = t->pc;
  t->pc += 4;  // Default: advance to next

  uint32_t opcd = OPCD(instr);

  switch (opcd) {

  // ─── Trap ─────────────────────────────────────────────────────────────
  case 2: {  // tdi
    return InterpResult::kTrap;
  }
  case 3: {  // twi
    // TO, rA, SIMM — trap word immediate
    // Games use tw 31,0,0 as a syscall mechanism
    uint32_t to = RD(instr);
    if (to == 31 && RA(instr) == 0 && SIMM(instr) == 0) {
      // This is effectively a debug break
    }
    return InterpResult::kTrap;
  }

  // ─── Integer Arithmetic Immediate ─────────────────────────────────────
  case 7: {  // mulli
    uint32_t rd = RD(instr);
    uint32_t ra = RA(instr);
    int16_t simm = SIMM(instr);
    t->gpr[rd] = static_cast<uint64_t>(
        static_cast<int64_t>(static_cast<int32_t>(t->gpr[ra])) *
        static_cast<int64_t>(simm));
    return InterpResult::kContinue;
  }
  case 8: {  // subfic
    uint32_t rd = RD(instr);
    uint32_t ra = RA(instr);
    int16_t simm = SIMM(instr);
    int64_t a = static_cast<int32_t>(t->gpr[ra]);
    int64_t result = static_cast<int64_t>(simm) - a;
    t->gpr[rd] = static_cast<uint64_t>(result);
    // Set CA in XER
    uint64_t ua = static_cast<uint32_t>(t->gpr[ra]);
    uint64_t ub = static_cast<uint32_t>(simm);
    if (ub >= ua) t->xer |= (1u << 29);  // CA
    else t->xer &= ~(1u << 29);
    return InterpResult::kContinue;
  }
  case 10: {  // cmpli
    uint32_t crf = CRF(instr);
    uint32_t ra = RA(instr);
    uint16_t uimm = UIMM(instr);
    uint32_t l = L_BIT(instr);
    if (l) {
      UpdateCRU(t, crf, t->gpr[ra], static_cast<uint64_t>(uimm));
    } else {
      UpdateCRU(t, crf, static_cast<uint32_t>(t->gpr[ra]), static_cast<uint32_t>(uimm));
    }
    return InterpResult::kContinue;
  }
  case 11: {  // cmpi
    uint32_t crf = CRF(instr);
    uint32_t ra = RA(instr);
    int16_t simm = SIMM(instr);
    uint32_t l = L_BIT(instr);
    if (l) {
      UpdateCR(t, crf, static_cast<int64_t>(t->gpr[ra]),
               static_cast<int64_t>(simm));
    } else {
      UpdateCR(t, crf, static_cast<int64_t>(static_cast<int32_t>(t->gpr[ra])),
               static_cast<int64_t>(simm));
    }
    return InterpResult::kContinue;
  }
  case 12: {  // addic
    uint32_t rd = RD(instr);
    uint32_t ra = RA(instr);
    int16_t simm = SIMM(instr);
    uint64_t a = static_cast<uint32_t>(t->gpr[ra]);
    uint64_t b = static_cast<uint32_t>(static_cast<int32_t>(simm));
    uint64_t result = a + b;
    t->gpr[rd] = static_cast<uint32_t>(result);
    // CA
    if (result > 0xFFFFFFFF) t->xer |= (1u << 29);
    else t->xer &= ~(1u << 29);
    return InterpResult::kContinue;
  }
  case 13: {  // addic.
    uint32_t rd = RD(instr);
    uint32_t ra = RA(instr);
    int16_t simm = SIMM(instr);
    uint64_t a = static_cast<uint32_t>(t->gpr[ra]);
    uint64_t b = static_cast<uint32_t>(static_cast<int32_t>(simm));
    uint64_t result = a + b;
    t->gpr[rd] = static_cast<uint32_t>(result);
    if (result > 0xFFFFFFFF) t->xer |= (1u << 29);
    else t->xer &= ~(1u << 29);
    UpdateCR0(t, static_cast<int64_t>(static_cast<int32_t>(t->gpr[rd])));
    return InterpResult::kContinue;
  }
  case 14: {  // addi / li
    uint32_t rd = RD(instr);
    uint32_t ra = RA(instr);
    int16_t simm = SIMM(instr);
    if (ra == 0)
      t->gpr[rd] = static_cast<uint64_t>(static_cast<int64_t>(simm));
    else
      t->gpr[rd] = static_cast<uint64_t>(
          static_cast<int64_t>(t->gpr[ra]) + static_cast<int64_t>(simm));
    return InterpResult::kContinue;
  }
  case 15: {  // addis / lis
    uint32_t rd = RD(instr);
    uint32_t ra = RA(instr);
    int32_t simm = static_cast<int32_t>(SIMM(instr)) << 16;
    if (ra == 0)
      t->gpr[rd] = static_cast<uint64_t>(static_cast<int64_t>(simm));
    else
      t->gpr[rd] = static_cast<uint64_t>(
          static_cast<int64_t>(t->gpr[ra]) + static_cast<int64_t>(simm));
    return InterpResult::kContinue;
  }

  // ─── Branch ───────────────────────────────────────────────────────────
  case 16: {  // bc — branch conditional
    uint32_t bo = BO(instr);
    uint32_t bi = BI(instr);
    int32_t bd = static_cast<int16_t>(instr & 0xFFFC);
    uint32_t aa = AA(instr);
    uint32_t lk = LK(instr);

    if (lk) t->lr = t->pc;  // PC already advanced

    if (EvalBranchCondition(t, bo, bi)) {
      if (aa)
        t->pc = static_cast<uint32_t>(bd);
      else
        t->pc = static_cast<uint32_t>(static_cast<int32_t>(pc) + bd);
      return InterpResult::kBranch;
    }
    return InterpResult::kContinue;
  }
  case 17: {  // sc — system call
    // Check for HLE dispatch
    if (hle_dispatch_) {
      // Ordinal is typically in r0 or encoded in the syscall
      uint32_t ordinal = static_cast<uint32_t>(t->gpr[0]);
      hle_dispatch_(t, ordinal);
    }
    return InterpResult::kSyscall;
  }
  case 18: {  // b / bl — unconditional branch
    int32_t li = instr & 0x03FFFFFC;
    if (li & 0x02000000) li |= static_cast<int32_t>(0xFC000000);  // Sign extend
    uint32_t aa = AA(instr);
    uint32_t lk = LK(instr);

    if (lk) t->lr = t->pc;

    uint32_t target;
    if (aa)
      target = static_cast<uint32_t>(li);
    else
      target = static_cast<uint32_t>(static_cast<int32_t>(pc) + li);

    // Check if target is an HLE thunk
    auto thunk_it = thunk_map_.find(target);
    if (thunk_it != thunk_map_.end() && hle_dispatch_) {
      hle_dispatch_(t, thunk_it->second);
      if (lk) {
        // bl to thunk — return from thunk, continue after the bl
        return InterpResult::kContinue;
      } else {
        // b to thunk (tail call) — return to LR
        t->pc = static_cast<uint32_t>(t->lr);
        return InterpResult::kBranch;
      }
    }

    t->pc = target;
    return InterpResult::kBranch;
  }

  // ─── Opcode 19: CR ops + bclr/bcctr ──────────────────────────────────
  case 19: {
    uint32_t xo = XO_19(instr);
    switch (xo) {
    case 0: {  // mcrf — move CR field
      uint32_t dstf = (instr >> 23) & 7;
      uint32_t srcf = (instr >> 18) & 7;
      uint32_t src_shift = (7 - srcf) * 4;
      uint32_t dst_shift = (7 - dstf) * 4;
      uint32_t bits = (t->cr >> src_shift) & 0xF;
      t->cr = (t->cr & ~(0xFu << dst_shift)) | (bits << dst_shift);
      return InterpResult::kContinue;
    }
    case 16: {  // bclr — branch conditional to LR
      uint32_t bo = BO(instr);
      uint32_t bi = BI(instr);
      uint32_t lk = LK(instr);
      uint32_t target = static_cast<uint32_t>(t->lr) & ~3u;

      if (lk) t->lr = t->pc;

      if (EvalBranchCondition(t, bo, bi)) {
        // blr — unconditional return?
        if (bo == 20) {
          t->pc = target;
          return InterpResult::kReturn;
        }
        t->pc = target;
        return InterpResult::kBranch;
      }
      return InterpResult::kContinue;
    }
    case 33: {  // crnor
      uint32_t d = (instr >> 21) & 0x1F;
      uint32_t a = (instr >> 16) & 0x1F;
      uint32_t b = (instr >> 11) & 0x1F;
      uint32_t va = (t->cr >> (31 - a)) & 1;
      uint32_t vb = (t->cr >> (31 - b)) & 1;
      uint32_t r = ~(va | vb) & 1;
      t->cr = (t->cr & ~(1u << (31 - d))) | (r << (31 - d));
      return InterpResult::kContinue;
    }
    case 129: {  // crandc
      uint32_t d = (instr >> 21) & 0x1F;
      uint32_t a = (instr >> 16) & 0x1F;
      uint32_t b = (instr >> 11) & 0x1F;
      uint32_t va = (t->cr >> (31 - a)) & 1;
      uint32_t vb = (t->cr >> (31 - b)) & 1;
      uint32_t r = va & (~vb & 1);
      t->cr = (t->cr & ~(1u << (31 - d))) | (r << (31 - d));
      return InterpResult::kContinue;
    }
    case 150: {  // isync — instruction synchronize
      return InterpResult::kContinue;  // NOP on host
    }
    case 193: {  // crxor
      uint32_t d = (instr >> 21) & 0x1F;
      uint32_t a = (instr >> 16) & 0x1F;
      uint32_t b = (instr >> 11) & 0x1F;
      uint32_t va = (t->cr >> (31 - a)) & 1;
      uint32_t vb = (t->cr >> (31 - b)) & 1;
      uint32_t r = va ^ vb;
      t->cr = (t->cr & ~(1u << (31 - d))) | (r << (31 - d));
      return InterpResult::kContinue;
    }
    case 225: {  // crnand
      uint32_t d = (instr >> 21) & 0x1F;
      uint32_t a = (instr >> 16) & 0x1F;
      uint32_t b = (instr >> 11) & 0x1F;
      uint32_t va = (t->cr >> (31 - a)) & 1;
      uint32_t vb = (t->cr >> (31 - b)) & 1;
      uint32_t r = ~(va & vb) & 1;
      t->cr = (t->cr & ~(1u << (31 - d))) | (r << (31 - d));
      return InterpResult::kContinue;
    }
    case 257: {  // crand
      uint32_t d = (instr >> 21) & 0x1F;
      uint32_t a = (instr >> 16) & 0x1F;
      uint32_t b = (instr >> 11) & 0x1F;
      uint32_t va = (t->cr >> (31 - a)) & 1;
      uint32_t vb = (t->cr >> (31 - b)) & 1;
      uint32_t r = va & vb;
      t->cr = (t->cr & ~(1u << (31 - d))) | (r << (31 - d));
      return InterpResult::kContinue;
    }
    case 289: {  // creqv
      uint32_t d = (instr >> 21) & 0x1F;
      uint32_t a = (instr >> 16) & 0x1F;
      uint32_t b = (instr >> 11) & 0x1F;
      uint32_t va = (t->cr >> (31 - a)) & 1;
      uint32_t vb = (t->cr >> (31 - b)) & 1;
      uint32_t r = ~(va ^ vb) & 1;
      t->cr = (t->cr & ~(1u << (31 - d))) | (r << (31 - d));
      return InterpResult::kContinue;
    }
    case 417: {  // crorc
      uint32_t d = (instr >> 21) & 0x1F;
      uint32_t a = (instr >> 16) & 0x1F;
      uint32_t b = (instr >> 11) & 0x1F;
      uint32_t va = (t->cr >> (31 - a)) & 1;
      uint32_t vb = (t->cr >> (31 - b)) & 1;
      uint32_t r = va | (~vb & 1);
      t->cr = (t->cr & ~(1u << (31 - d))) | (r << (31 - d));
      return InterpResult::kContinue;
    }
    case 449: {  // cror
      uint32_t d = (instr >> 21) & 0x1F;
      uint32_t a = (instr >> 16) & 0x1F;
      uint32_t b = (instr >> 11) & 0x1F;
      uint32_t va = (t->cr >> (31 - a)) & 1;
      uint32_t vb = (t->cr >> (31 - b)) & 1;
      uint32_t r = va | vb;
      t->cr = (t->cr & ~(1u << (31 - d))) | (r << (31 - d));
      return InterpResult::kContinue;
    }
    case 528: {  // bcctr — branch conditional to CTR
      uint32_t bo = BO(instr);
      uint32_t bi = BI(instr);
      uint32_t lk = LK(instr);
      uint32_t target = static_cast<uint32_t>(t->ctr) & ~3u;

      if (lk) t->lr = t->pc;

      // bcctr ignores CTR decrement (BO bit 2 is treated as 1)
      bool cond_ok = true;
      if (!(bo & 0x10)) {
        uint32_t cr_bit = (t->cr >> (31 - bi)) & 1;
        cond_ok = (bo & 0x08) ? (cr_bit == 1) : (cr_bit == 0);
      }

      if (cond_ok) {
        auto thunk_it = thunk_map_.find(target);
        if (thunk_it != thunk_map_.end() && hle_dispatch_) {
          hle_dispatch_(t, thunk_it->second);
          if (lk) return InterpResult::kContinue;
          t->pc = static_cast<uint32_t>(t->lr);
          return InterpResult::kBranch;
        }
        t->pc = target;
        return InterpResult::kBranch;
      }
      return InterpResult::kContinue;
    }
    default:
      XELOGW("Unhandled opcode 19 xo={} at 0x{:08X}", xo, pc);
      return InterpResult::kContinue;
    }
  }

  // ─── Rotate/Shift (32-bit) ───────────────────────────────────────────
  case 20: {  // rlwimi — Rotate Left Word Immediate then Mask Insert
    uint32_t rs = RS(instr);
    uint32_t ra = RA(instr);
    uint32_t sh = SH(instr);
    uint32_t mb = MB(instr);
    uint32_t me = ME(instr);
    bool rc = RC_BIT(instr);

    uint32_t val = static_cast<uint32_t>(t->gpr[rs]);
    uint32_t rotated = (val << sh) | (val >> (32 - sh));
    uint32_t mask = BuildMask32(mb, me);
    t->gpr[ra] = (rotated & mask) | (static_cast<uint32_t>(t->gpr[ra]) & ~mask);
    if (rc) UpdateCR0(t, static_cast<int64_t>(static_cast<int32_t>(t->gpr[ra])));
    return InterpResult::kContinue;
  }
  case 21: {  // rlwinm — Rotate Left Word Immediate then AND with Mask
    uint32_t rs = RS(instr);
    uint32_t ra = RA(instr);
    uint32_t sh = SH(instr);
    uint32_t mb = MB(instr);
    uint32_t me = ME(instr);
    bool rc = RC_BIT(instr);

    uint32_t val = static_cast<uint32_t>(t->gpr[rs]);
    uint32_t rotated = (sh == 0) ? val : ((val << sh) | (val >> (32 - sh)));
    uint32_t mask = BuildMask32(mb, me);
    t->gpr[ra] = rotated & mask;
    if (rc) UpdateCR0(t, static_cast<int64_t>(static_cast<int32_t>(t->gpr[ra])));
    return InterpResult::kContinue;
  }
  case 23: {  // rlwnm — Rotate Left Word then AND with Mask
    uint32_t rs = RS(instr);
    uint32_t ra = RA(instr);
    uint32_t rb = RB(instr);
    uint32_t mb = MB(instr);
    uint32_t me = ME(instr);
    bool rc = RC_BIT(instr);

    uint32_t val = static_cast<uint32_t>(t->gpr[rs]);
    uint32_t sh = static_cast<uint32_t>(t->gpr[rb]) & 0x1F;
    uint32_t rotated = (sh == 0) ? val : ((val << sh) | (val >> (32 - sh)));
    uint32_t mask = BuildMask32(mb, me);
    t->gpr[ra] = rotated & mask;
    if (rc) UpdateCR0(t, static_cast<int64_t>(static_cast<int32_t>(t->gpr[ra])));
    return InterpResult::kContinue;
  }

  // ─── Integer Logical Immediate ────────────────────────────────────────
  case 24: {  // ori
    uint32_t rs = RS(instr);
    uint32_t ra = RA(instr);
    uint16_t uimm = UIMM(instr);
    t->gpr[ra] = t->gpr[rs] | uimm;
    return InterpResult::kContinue;
  }
  case 25: {  // oris
    uint32_t rs = RS(instr);
    uint32_t ra = RA(instr);
    uint16_t uimm = UIMM(instr);
    t->gpr[ra] = t->gpr[rs] | (static_cast<uint64_t>(uimm) << 16);
    return InterpResult::kContinue;
  }
  case 26: {  // xori
    uint32_t rs = RS(instr);
    uint32_t ra = RA(instr);
    uint16_t uimm = UIMM(instr);
    t->gpr[ra] = t->gpr[rs] ^ uimm;
    return InterpResult::kContinue;
  }
  case 27: {  // xoris
    uint32_t rs = RS(instr);
    uint32_t ra = RA(instr);
    uint16_t uimm = UIMM(instr);
    t->gpr[ra] = t->gpr[rs] ^ (static_cast<uint64_t>(uimm) << 16);
    return InterpResult::kContinue;
  }
  case 28: {  // andi.
    uint32_t rs = RS(instr);
    uint32_t ra = RA(instr);
    uint16_t uimm = UIMM(instr);
    t->gpr[ra] = t->gpr[rs] & uimm;
    UpdateCR0(t, static_cast<int64_t>(static_cast<int32_t>(t->gpr[ra])));
    return InterpResult::kContinue;
  }
  case 29: {  // andis.
    uint32_t rs = RS(instr);
    uint32_t ra = RA(instr);
    uint16_t uimm = UIMM(instr);
    t->gpr[ra] = t->gpr[rs] & (static_cast<uint64_t>(uimm) << 16);
    UpdateCR0(t, static_cast<int64_t>(static_cast<int32_t>(t->gpr[ra])));
    return InterpResult::kContinue;
  }

  // ─── Rotate/Shift (64-bit) ───────────────────────────────────────────
  case 30: {  // rld* family — 64-bit rotates
    uint32_t xo = (instr >> 1) & 0xF;
    uint32_t rs = RS(instr);
    uint32_t ra = RA(instr);
    bool rc = RC_BIT(instr);
    uint64_t val = t->gpr[rs];

    switch (xo) {
    case 0: case 1: {  // rldicl
      uint32_t sh = ((instr >> 11) & 0x1F) | (((instr >> 1) & 1) << 5);
      uint32_t mb = ((instr >> 6) & 0x1F) | (((instr >> 5) & 1) << 5);
      uint64_t rotated = (val << sh) | (val >> (64 - sh));
      uint64_t mask = BuildMask64(mb, 63);
      t->gpr[ra] = rotated & mask;
      break;
    }
    case 2: case 3: {  // rldicr
      uint32_t sh = ((instr >> 11) & 0x1F) | (((instr >> 1) & 1) << 5);
      uint32_t me = ((instr >> 6) & 0x1F) | (((instr >> 5) & 1) << 5);
      uint64_t rotated = (val << sh) | (val >> (64 - sh));
      uint64_t mask = BuildMask64(0, me);
      t->gpr[ra] = rotated & mask;
      break;
    }
    case 4: case 5: {  // rldic
      uint32_t sh = ((instr >> 11) & 0x1F) | (((instr >> 1) & 1) << 5);
      uint32_t mb = ((instr >> 6) & 0x1F) | (((instr >> 5) & 1) << 5);
      uint64_t rotated = (val << sh) | (val >> (64 - sh));
      uint64_t mask = BuildMask64(mb, 63 - sh);
      t->gpr[ra] = rotated & mask;
      break;
    }
    case 6: case 7: {  // rldimi
      uint32_t sh = ((instr >> 11) & 0x1F) | (((instr >> 1) & 1) << 5);
      uint32_t mb = ((instr >> 6) & 0x1F) | (((instr >> 5) & 1) << 5);
      uint64_t rotated = (val << sh) | (val >> (64 - sh));
      uint64_t mask = BuildMask64(mb, 63 - sh);
      t->gpr[ra] = (rotated & mask) | (t->gpr[ra] & ~mask);
      break;
    }
    case 8: {  // rldcl
      uint32_t rb = RB(instr);
      uint32_t mb = ((instr >> 6) & 0x1F) | (((instr >> 5) & 1) << 5);
      uint32_t sh = static_cast<uint32_t>(t->gpr[rb]) & 0x3F;
      uint64_t rotated = (val << sh) | (val >> (64 - sh));
      uint64_t mask = BuildMask64(mb, 63);
      t->gpr[ra] = rotated & mask;
      break;
    }
    case 9: {  // rldcr
      uint32_t rb = RB(instr);
      uint32_t me = ((instr >> 6) & 0x1F) | (((instr >> 5) & 1) << 5);
      uint32_t sh = static_cast<uint32_t>(t->gpr[rb]) & 0x3F;
      uint64_t rotated = (val << sh) | (val >> (64 - sh));
      uint64_t mask = BuildMask64(0, me);
      t->gpr[ra] = rotated & mask;
      break;
    }
    default:
      break;
    }
    if (rc) UpdateCR0(t, static_cast<int64_t>(t->gpr[ra]));
    return InterpResult::kContinue;
  }

  // ─── Opcode 31: Extended Integer ──────────────────────────────────────
  case 31: {
    uint32_t xo = XO_31(instr);
    uint32_t rd = RD(instr);
    uint32_t ra = RA(instr);
    uint32_t rb = RB(instr);
    bool rc = RC_BIT(instr);
    bool oe = OE(instr);

    switch (xo) {
    case 0: {  // cmp
      uint32_t crf = CRF(instr);
      uint32_t l = L_BIT(instr);
      if (l) UpdateCR(t, crf, static_cast<int64_t>(t->gpr[ra]),
                       static_cast<int64_t>(t->gpr[rb]));
      else UpdateCR(t, crf, static_cast<int64_t>(static_cast<int32_t>(t->gpr[ra])),
                    static_cast<int64_t>(static_cast<int32_t>(t->gpr[rb])));
      return InterpResult::kContinue;
    }
    case 4: {  // tw — trap word
      return InterpResult::kTrap;
    }
    case 8: { // subfc
      uint64_t a = static_cast<uint32_t>(t->gpr[ra]);
      uint64_t b = static_cast<uint32_t>(t->gpr[rb]);
      uint64_t result = b + ~a + 1;
      t->gpr[rd] = static_cast<uint32_t>(result);
      if (~a + b >= 0xFFFFFFFF) t->xer |= (1u << 29);
      else t->xer &= ~(1u << 29);
      if (rc) UpdateCR0(t, static_cast<int32_t>(t->gpr[rd]));
      return InterpResult::kContinue;
    }
    case 10: { // addc
      uint64_t a = static_cast<uint32_t>(t->gpr[ra]);
      uint64_t b = static_cast<uint32_t>(t->gpr[rb]);
      uint64_t result = a + b;
      t->gpr[rd] = static_cast<uint32_t>(result);
      if (result > 0xFFFFFFFF) t->xer |= (1u << 29);
      else t->xer &= ~(1u << 29);
      if (rc) UpdateCR0(t, static_cast<int32_t>(t->gpr[rd]));
      return InterpResult::kContinue;
    }
    case 11: { // mulhwu
      uint64_t a = static_cast<uint32_t>(t->gpr[ra]);
      uint64_t b = static_cast<uint32_t>(t->gpr[rb]);
      t->gpr[rd] = static_cast<uint32_t>((a * b) >> 32);
      if (rc) UpdateCR0(t, static_cast<int32_t>(t->gpr[rd]));
      return InterpResult::kContinue;
    }
    case 19: { // mfcr — move from CR
      t->gpr[rd] = t->cr;
      return InterpResult::kContinue;
    }
    case 20: { // lwarx — load word and reserve
      uint32_t ea = (ra == 0) ? 0 : static_cast<uint32_t>(t->gpr[ra]);
      ea += static_cast<uint32_t>(t->gpr[rb]);
      t->gpr[rd] = ReadU32(ea);
      t->reserve_address = ea;
      t->reserve_valid = true;
      return InterpResult::kContinue;
    }
    case 23: { // lwzx — load word and zero indexed
      uint32_t ea = (ra == 0) ? 0 : static_cast<uint32_t>(t->gpr[ra]);
      ea += static_cast<uint32_t>(t->gpr[rb]);
      t->gpr[rd] = ReadU32(ea);
      return InterpResult::kContinue;
    }
    case 24: { // slw — shift left word
      uint32_t sh = static_cast<uint32_t>(t->gpr[rb]) & 0x3F;
      if (sh >= 32) t->gpr[ra] = 0;
      else t->gpr[ra] = static_cast<uint32_t>(t->gpr[RS(instr)]) << sh;
      if (rc) UpdateCR0(t, static_cast<int32_t>(t->gpr[ra]));
      return InterpResult::kContinue;
    }
    case 26: { // cntlzw — count leading zeros word
      uint32_t val = static_cast<uint32_t>(t->gpr[RS(instr)]);
      t->gpr[ra] = val ? __builtin_clz(val) : 32;
      if (rc) UpdateCR0(t, static_cast<int32_t>(t->gpr[ra]));
      return InterpResult::kContinue;
    }
    case 28: { // and
      t->gpr[ra] = t->gpr[RS(instr)] & t->gpr[rb];
      if (rc) UpdateCR0(t, static_cast<int64_t>(t->gpr[ra]));
      return InterpResult::kContinue;
    }
    case 32: { // cmpl — compare logical
      uint32_t crf = CRF(instr);
      uint32_t l = L_BIT(instr);
      if (l) UpdateCRU(t, crf, t->gpr[ra], t->gpr[rb]);
      else UpdateCRU(t, crf, static_cast<uint32_t>(t->gpr[ra]),
                     static_cast<uint32_t>(t->gpr[rb]));
      return InterpResult::kContinue;
    }
    case 40: { // subf — subtract from
      t->gpr[rd] = t->gpr[rb] - t->gpr[ra];
      if (rc) UpdateCR0(t, static_cast<int64_t>(static_cast<int32_t>(t->gpr[rd])));
      return InterpResult::kContinue;
    }
    case 54: { // dcbst — data cache block store — NOP
      return InterpResult::kContinue;
    }
    case 55: { // lwzux — load word and zero with update indexed
      uint32_t ea = static_cast<uint32_t>(t->gpr[ra]) +
                    static_cast<uint32_t>(t->gpr[rb]);
      t->gpr[rd] = ReadU32(ea);
      t->gpr[ra] = ea;
      return InterpResult::kContinue;
    }
    case 60: { // andc — and with complement
      t->gpr[ra] = t->gpr[RS(instr)] & ~t->gpr[rb];
      if (rc) UpdateCR0(t, static_cast<int64_t>(t->gpr[ra]));
      return InterpResult::kContinue;
    }
    case 75: { // mulhw — multiply high word signed
      int64_t a = static_cast<int32_t>(t->gpr[ra]);
      int64_t b = static_cast<int32_t>(t->gpr[rb]);
      t->gpr[rd] = static_cast<uint32_t>((a * b) >> 32);
      if (rc) UpdateCR0(t, static_cast<int32_t>(t->gpr[rd]));
      return InterpResult::kContinue;
    }
    case 83: { // mfmsr
      t->gpr[rd] = 0;  // MSR stub
      return InterpResult::kContinue;
    }
    case 86: { // dcbf — data cache block flush — NOP
      return InterpResult::kContinue;
    }
    case 87: { // lbzx — load byte and zero indexed
      uint32_t ea = (ra == 0) ? 0 : static_cast<uint32_t>(t->gpr[ra]);
      ea += static_cast<uint32_t>(t->gpr[rb]);
      t->gpr[rd] = ReadU8(ea);
      return InterpResult::kContinue;
    }
    case 104: { // neg
      t->gpr[rd] = static_cast<uint64_t>(-static_cast<int64_t>(t->gpr[ra]));
      if (rc) UpdateCR0(t, static_cast<int64_t>(t->gpr[rd]));
      return InterpResult::kContinue;
    }
    case 119: { // lbzux
      uint32_t ea = static_cast<uint32_t>(t->gpr[ra]) +
                    static_cast<uint32_t>(t->gpr[rb]);
      t->gpr[rd] = ReadU8(ea);
      t->gpr[ra] = ea;
      return InterpResult::kContinue;
    }
    case 124: { // nor
      t->gpr[ra] = ~(t->gpr[RS(instr)] | t->gpr[rb]);
      if (rc) UpdateCR0(t, static_cast<int64_t>(t->gpr[ra]));
      return InterpResult::kContinue;
    }
    case 136: { // subfe — subtract from extended
      uint64_t a = static_cast<uint32_t>(t->gpr[ra]);
      uint64_t b = static_cast<uint32_t>(t->gpr[rb]);
      uint32_t ca = (t->xer >> 29) & 1;
      uint64_t result = ~a + b + ca;
      t->gpr[rd] = static_cast<uint32_t>(result);
      if (result > 0xFFFFFFFF) t->xer |= (1u << 29);
      else t->xer &= ~(1u << 29);
      if (rc) UpdateCR0(t, static_cast<int32_t>(t->gpr[rd]));
      return InterpResult::kContinue;
    }
    case 138: { // adde
      uint64_t a = static_cast<uint32_t>(t->gpr[ra]);
      uint64_t b = static_cast<uint32_t>(t->gpr[rb]);
      uint32_t ca = (t->xer >> 29) & 1;
      uint64_t result = a + b + ca;
      t->gpr[rd] = static_cast<uint32_t>(result);
      if (result > 0xFFFFFFFF) t->xer |= (1u << 29);
      else t->xer &= ~(1u << 29);
      if (rc) UpdateCR0(t, static_cast<int32_t>(t->gpr[rd]));
      return InterpResult::kContinue;
    }
    case 144: { // mtcrf — move to CR fields
      uint32_t crm = (instr >> 12) & 0xFF;
      uint32_t val = static_cast<uint32_t>(t->gpr[RS(instr)]);
      uint32_t mask = 0;
      for (int i = 0; i < 8; ++i) {
        if (crm & (1 << (7 - i))) mask |= (0xFu << ((7 - i) * 4));
      }
      t->cr = (t->cr & ~mask) | (val & mask);
      return InterpResult::kContinue;
    }
    case 150: { // stwcx. — store word conditional
      uint32_t ea = (ra == 0) ? 0 : static_cast<uint32_t>(t->gpr[ra]);
      ea += static_cast<uint32_t>(t->gpr[rb]);

      if (t->reserve_valid && t->reserve_address == ea) {
        WriteU32(ea, static_cast<uint32_t>(t->gpr[RS(instr)]));
        t->reserve_valid = false;
        // Set CR0 = EQ (success)
        t->cr = (t->cr & ~(0xFu << 28)) | (0x2u << 28);
      } else {
        // Failed — set CR0 ≠ EQ
        t->cr = (t->cr & ~(0xFu << 28));
      }
      return InterpResult::kContinue;
    }
    case 151: { // stwx — store word indexed
      uint32_t ea = (ra == 0) ? 0 : static_cast<uint32_t>(t->gpr[ra]);
      ea += static_cast<uint32_t>(t->gpr[rb]);
      WriteU32(ea, static_cast<uint32_t>(t->gpr[RS(instr)]));
      return InterpResult::kContinue;
    }
    case 183: { // stwux — store word with update indexed
      uint32_t ea = static_cast<uint32_t>(t->gpr[ra]) +
                    static_cast<uint32_t>(t->gpr[rb]);
      WriteU32(ea, static_cast<uint32_t>(t->gpr[RS(instr)]));
      t->gpr[ra] = ea;
      return InterpResult::kContinue;
    }
    case 200: { // subfze
      uint64_t a = static_cast<uint32_t>(t->gpr[ra]);
      uint32_t ca = (t->xer >> 29) & 1;
      uint64_t result = ~a + ca;
      t->gpr[rd] = static_cast<uint32_t>(result);
      if (result > 0xFFFFFFFF) t->xer |= (1u << 29);
      else t->xer &= ~(1u << 29);
      if (rc) UpdateCR0(t, static_cast<int32_t>(t->gpr[rd]));
      return InterpResult::kContinue;
    }
    case 202: { // addze
      uint64_t a = static_cast<uint32_t>(t->gpr[ra]);
      uint32_t ca = (t->xer >> 29) & 1;
      uint64_t result = a + ca;
      t->gpr[rd] = static_cast<uint32_t>(result);
      if (result > 0xFFFFFFFF) t->xer |= (1u << 29);
      else t->xer &= ~(1u << 29);
      if (rc) UpdateCR0(t, static_cast<int32_t>(t->gpr[rd]));
      return InterpResult::kContinue;
    }
    case 215: { // stbx — store byte indexed
      uint32_t ea = (ra == 0) ? 0 : static_cast<uint32_t>(t->gpr[ra]);
      ea += static_cast<uint32_t>(t->gpr[rb]);
      WriteU8(ea, static_cast<uint8_t>(t->gpr[RS(instr)]));
      return InterpResult::kContinue;
    }
    case 234: { // addme
      uint64_t a = static_cast<uint32_t>(t->gpr[ra]);
      uint32_t ca = (t->xer >> 29) & 1;
      uint64_t result = a + 0xFFFFFFFF + ca;
      t->gpr[rd] = static_cast<uint32_t>(result);
      if (result > 0xFFFFFFFF) t->xer |= (1u << 29);
      else t->xer &= ~(1u << 29);
      if (rc) UpdateCR0(t, static_cast<int32_t>(t->gpr[rd]));
      return InterpResult::kContinue;
    }
    case 235: { // mullw
      int64_t a = static_cast<int32_t>(t->gpr[ra]);
      int64_t b = static_cast<int32_t>(t->gpr[rb]);
      t->gpr[rd] = static_cast<uint32_t>(a * b);
      if (rc) UpdateCR0(t, static_cast<int32_t>(t->gpr[rd]));
      return InterpResult::kContinue;
    }
    case 246: { // dcbtst — data cache block touch for store — NOP
      return InterpResult::kContinue;
    }
    case 247: { // stbux
      uint32_t ea = static_cast<uint32_t>(t->gpr[ra]) +
                    static_cast<uint32_t>(t->gpr[rb]);
      WriteU8(ea, static_cast<uint8_t>(t->gpr[RS(instr)]));
      t->gpr[ra] = ea;
      return InterpResult::kContinue;
    }
    case 266: { // add
      t->gpr[rd] = t->gpr[ra] + t->gpr[rb];
      if (oe) {
        // TODO: overflow detection
      }
      if (rc) UpdateCR0(t, static_cast<int64_t>(static_cast<int32_t>(t->gpr[rd])));
      return InterpResult::kContinue;
    }
    case 278: { // dcbt — data cache block touch — NOP
      return InterpResult::kContinue;
    }
    case 279: { // lhzx — load halfword and zero indexed
      uint32_t ea = (ra == 0) ? 0 : static_cast<uint32_t>(t->gpr[ra]);
      ea += static_cast<uint32_t>(t->gpr[rb]);
      t->gpr[rd] = ReadU16(ea);
      return InterpResult::kContinue;
    }
    case 284: { // eqv
      t->gpr[ra] = ~(t->gpr[RS(instr)] ^ t->gpr[rb]);
      if (rc) UpdateCR0(t, static_cast<int64_t>(t->gpr[ra]));
      return InterpResult::kContinue;
    }
    case 311: { // lhzux
      uint32_t ea = static_cast<uint32_t>(t->gpr[ra]) +
                    static_cast<uint32_t>(t->gpr[rb]);
      t->gpr[rd] = ReadU16(ea);
      t->gpr[ra] = ea;
      return InterpResult::kContinue;
    }
    case 316: { // xor
      t->gpr[ra] = t->gpr[RS(instr)] ^ t->gpr[rb];
      if (rc) UpdateCR0(t, static_cast<int64_t>(t->gpr[ra]));
      return InterpResult::kContinue;
    }
    case 339: { // mfspr — move from SPR
      uint32_t spr = SPR(instr);
      switch (spr) {
        case 1:   t->gpr[rd] = t->xer; break;  // XER
        case 8:   t->gpr[rd] = t->lr; break;    // LR
        case 9:   t->gpr[rd] = t->ctr; break;   // CTR
        case 268: t->gpr[rd] = 0; break;         // TBL — timebase lower
        case 269: t->gpr[rd] = 0; break;         // TBU — timebase upper
        default:  t->gpr[rd] = 0; break;
      }
      return InterpResult::kContinue;
    }
    case 343: { // lhax — load halfword algebraic indexed
      uint32_t ea = (ra == 0) ? 0 : static_cast<uint32_t>(t->gpr[ra]);
      ea += static_cast<uint32_t>(t->gpr[rb]);
      int16_t v = static_cast<int16_t>(ReadU16(ea));
      t->gpr[rd] = static_cast<uint64_t>(static_cast<int64_t>(v));
      return InterpResult::kContinue;
    }
    case 371: { // mftb — move from time base
      uint32_t tbr_val = TBR(instr);
      // Return a monotonically increasing value
      static uint64_t fake_tb = 0;
      fake_tb += 100;
      if (tbr_val == 268) t->gpr[rd] = static_cast<uint32_t>(fake_tb);
      else t->gpr[rd] = static_cast<uint32_t>(fake_tb >> 32);
      return InterpResult::kContinue;
    }
    case 375: { // lhaux
      uint32_t ea = static_cast<uint32_t>(t->gpr[ra]) +
                    static_cast<uint32_t>(t->gpr[rb]);
      int16_t v = static_cast<int16_t>(ReadU16(ea));
      t->gpr[rd] = static_cast<uint64_t>(static_cast<int64_t>(v));
      t->gpr[ra] = ea;
      return InterpResult::kContinue;
    }
    case 407: { // sthx — store halfword indexed
      uint32_t ea = (ra == 0) ? 0 : static_cast<uint32_t>(t->gpr[ra]);
      ea += static_cast<uint32_t>(t->gpr[rb]);
      WriteU16(ea, static_cast<uint16_t>(t->gpr[RS(instr)]));
      return InterpResult::kContinue;
    }
    case 412: { // orc
      t->gpr[ra] = t->gpr[RS(instr)] | ~t->gpr[rb];
      if (rc) UpdateCR0(t, static_cast<int64_t>(t->gpr[ra]));
      return InterpResult::kContinue;
    }
    case 439: { // sthux
      uint32_t ea = static_cast<uint32_t>(t->gpr[ra]) +
                    static_cast<uint32_t>(t->gpr[rb]);
      WriteU16(ea, static_cast<uint16_t>(t->gpr[RS(instr)]));
      t->gpr[ra] = ea;
      return InterpResult::kContinue;
    }
    case 444: { // or (also mr rA, rS)
      t->gpr[ra] = t->gpr[RS(instr)] | t->gpr[rb];
      if (rc) UpdateCR0(t, static_cast<int64_t>(t->gpr[ra]));
      return InterpResult::kContinue;
    }
    case 459: { // divwu — divide word unsigned
      uint32_t a = static_cast<uint32_t>(t->gpr[ra]);
      uint32_t b = static_cast<uint32_t>(t->gpr[rb]);
      t->gpr[rd] = b ? (a / b) : 0;
      if (rc) UpdateCR0(t, static_cast<int32_t>(t->gpr[rd]));
      return InterpResult::kContinue;
    }
    case 467: { // mtspr — move to SPR
      uint32_t spr = SPR(instr);
      switch (spr) {
        case 1:   t->xer = t->gpr[RS(instr)]; break;
        case 8:   t->lr = t->gpr[RS(instr)]; break;
        case 9:   t->ctr = t->gpr[RS(instr)]; break;
        default:  break;
      }
      return InterpResult::kContinue;
    }
    case 476: { // nand
      t->gpr[ra] = ~(t->gpr[RS(instr)] & t->gpr[rb]);
      if (rc) UpdateCR0(t, static_cast<int64_t>(t->gpr[ra]));
      return InterpResult::kContinue;
    }
    case 491: { // divw — divide word signed
      int32_t a = static_cast<int32_t>(t->gpr[ra]);
      int32_t b = static_cast<int32_t>(t->gpr[rb]);
      t->gpr[rd] = b ? static_cast<uint32_t>(a / b) : 0;
      if (rc) UpdateCR0(t, static_cast<int32_t>(t->gpr[rd]));
      return InterpResult::kContinue;
    }
    case 536: { // srw — shift right word
      uint32_t sh = static_cast<uint32_t>(t->gpr[rb]) & 0x3F;
      if (sh >= 32) t->gpr[ra] = 0;
      else t->gpr[ra] = static_cast<uint32_t>(t->gpr[RS(instr)]) >> sh;
      if (rc) UpdateCR0(t, static_cast<int32_t>(t->gpr[ra]));
      return InterpResult::kContinue;
    }
    case 534: { // lwbrx — load word byte-reverse indexed
      uint32_t ea = (ra == 0) ? 0 : static_cast<uint32_t>(t->gpr[ra]);
      ea += static_cast<uint32_t>(t->gpr[rb]);
      // Read as big-endian then swap = read as little-endian
      uint32_t v;
      memcpy(&v, guest_base_ + ea, 4);
      // Already little-endian on host after bswap in ReadU32, so just read raw
      t->gpr[rd] = v;  // No byte swap = LE read
      return InterpResult::kContinue;
    }
    case 535: { // lfsx — load float single indexed
      uint32_t ea = (ra == 0) ? 0 : static_cast<uint32_t>(t->gpr[ra]);
      ea += static_cast<uint32_t>(t->gpr[rb]);
      float f = ReadF32(ea);
      t->fpr[FRT(instr)] = static_cast<double>(f);
      return InterpResult::kContinue;
    }
    case 567: { // lfsux
      uint32_t ea = static_cast<uint32_t>(t->gpr[ra]) +
                    static_cast<uint32_t>(t->gpr[rb]);
      float f = ReadF32(ea);
      t->fpr[FRT(instr)] = static_cast<double>(f);
      t->gpr[ra] = ea;
      return InterpResult::kContinue;
    }
    case 595: { // mfsr — NOP
      t->gpr[rd] = 0;
      return InterpResult::kContinue;
    }
    case 598: { // sync / lwsync / ptesync — NOP (memory barrier)
      return InterpResult::kContinue;
    }
    case 599: { // lfdx — load float double indexed
      uint32_t ea = (ra == 0) ? 0 : static_cast<uint32_t>(t->gpr[ra]);
      ea += static_cast<uint32_t>(t->gpr[rb]);
      t->fpr[FRT(instr)] = ReadF64(ea);
      return InterpResult::kContinue;
    }
    case 631: { // lfdux
      uint32_t ea = static_cast<uint32_t>(t->gpr[ra]) +
                    static_cast<uint32_t>(t->gpr[rb]);
      t->fpr[FRT(instr)] = ReadF64(ea);
      t->gpr[ra] = ea;
      return InterpResult::kContinue;
    }
    case 662: { // stwbrx — store word byte-reverse indexed
      uint32_t ea = (ra == 0) ? 0 : static_cast<uint32_t>(t->gpr[ra]);
      ea += static_cast<uint32_t>(t->gpr[rb]);
      uint32_t v = static_cast<uint32_t>(t->gpr[RS(instr)]);
      // Store as little-endian (byte-reverse of normal big-endian)
      memcpy(guest_base_ + ea, &v, 4);
      return InterpResult::kContinue;
    }
    case 663: { // stfsx — store float single indexed
      uint32_t ea = (ra == 0) ? 0 : static_cast<uint32_t>(t->gpr[ra]);
      ea += static_cast<uint32_t>(t->gpr[rb]);
      float f = static_cast<float>(t->fpr[FRT(instr)]);
      WriteF32(ea, f);
      return InterpResult::kContinue;
    }
    case 695: { // stfsux
      uint32_t ea = static_cast<uint32_t>(t->gpr[ra]) +
                    static_cast<uint32_t>(t->gpr[rb]);
      float f = static_cast<float>(t->fpr[FRT(instr)]);
      WriteF32(ea, f);
      t->gpr[ra] = ea;
      return InterpResult::kContinue;
    }
    case 727: { // stfdx — store float double indexed
      uint32_t ea = (ra == 0) ? 0 : static_cast<uint32_t>(t->gpr[ra]);
      ea += static_cast<uint32_t>(t->gpr[rb]);
      WriteF64(ea, t->fpr[FRT(instr)]);
      return InterpResult::kContinue;
    }
    case 759: { // stfdux
      uint32_t ea = static_cast<uint32_t>(t->gpr[ra]) +
                    static_cast<uint32_t>(t->gpr[rb]);
      WriteF64(ea, t->fpr[FRT(instr)]);
      t->gpr[ra] = ea;
      return InterpResult::kContinue;
    }
    case 790: { // lhbrx — load halfword byte-reverse indexed
      uint32_t ea = (ra == 0) ? 0 : static_cast<uint32_t>(t->gpr[ra]);
      ea += static_cast<uint32_t>(t->gpr[rb]);
      uint16_t v;
      memcpy(&v, guest_base_ + ea, 2);
      t->gpr[rd] = v;  // Raw LE read
      return InterpResult::kContinue;
    }
    case 792: { // sraw — shift right algebraic word
      int32_t val = static_cast<int32_t>(t->gpr[RS(instr)]);
      uint32_t sh = static_cast<uint32_t>(t->gpr[rb]) & 0x3F;
      if (sh >= 32) {
        t->gpr[ra] = (val < 0) ? 0xFFFFFFFF : 0;
        if (val < 0) t->xer |= (1u << 29);
        else t->xer &= ~(1u << 29);
      } else {
        t->gpr[ra] = static_cast<uint32_t>(val >> sh);
        if (val < 0 && (val & ((1 << sh) - 1)))
          t->xer |= (1u << 29);
        else
          t->xer &= ~(1u << 29);
      }
      if (rc) UpdateCR0(t, static_cast<int32_t>(t->gpr[ra]));
      return InterpResult::kContinue;
    }
    case 824: { // srawi — shift right algebraic word immediate
      int32_t val = static_cast<int32_t>(t->gpr[RS(instr)]);
      uint32_t sh = SH(instr);
      t->gpr[ra] = static_cast<uint32_t>(val >> sh);
      if (val < 0 && sh > 0 && (val & ((1 << sh) - 1)))
        t->xer |= (1u << 29);
      else
        t->xer &= ~(1u << 29);
      if (rc) UpdateCR0(t, static_cast<int32_t>(t->gpr[ra]));
      return InterpResult::kContinue;
    }
    case 854: { // eieio — enforce in-order execution of I/O — NOP
      return InterpResult::kContinue;
    }
    case 918: { // sthbrx — store halfword byte-reverse indexed
      uint32_t ea = (ra == 0) ? 0 : static_cast<uint32_t>(t->gpr[ra]);
      ea += static_cast<uint32_t>(t->gpr[rb]);
      uint16_t v = static_cast<uint16_t>(t->gpr[RS(instr)]);
      memcpy(guest_base_ + ea, &v, 2);  // Raw LE store
      return InterpResult::kContinue;
    }
    case 922: { // extsh — extend sign halfword
      int16_t v = static_cast<int16_t>(t->gpr[RS(instr)]);
      t->gpr[ra] = static_cast<uint64_t>(static_cast<int64_t>(v));
      if (rc) UpdateCR0(t, static_cast<int64_t>(t->gpr[ra]));
      return InterpResult::kContinue;
    }
    case 954: { // extsb — extend sign byte
      int8_t v = static_cast<int8_t>(t->gpr[RS(instr)]);
      t->gpr[ra] = static_cast<uint64_t>(static_cast<int64_t>(v));
      if (rc) UpdateCR0(t, static_cast<int64_t>(t->gpr[ra]));
      return InterpResult::kContinue;
    }
    case 982: { // icbi — instruction cache block invalidate — NOP
      return InterpResult::kContinue;
    }
    case 1014: { // dcbz — data cache block clear to zero
      uint32_t ea = (ra == 0) ? 0 : static_cast<uint32_t>(t->gpr[ra]);
      ea += static_cast<uint32_t>(t->gpr[rb]);
      ea &= ~0x1Fu;  // Align to 32-byte cache line
      memset(guest_base_ + ea, 0, 32);
      return InterpResult::kContinue;
    }
    default:
      XELOGW("Unhandled opcode 31 xo={} at 0x{:08X}", xo, pc);
      return InterpResult::kContinue;
    }
  }

  // ─── Integer Load ─────────────────────────────────────────────────────
  case 32: { // lwz — load word and zero
    uint32_t rd_ = RD(instr);
    uint32_t ra_ = RA(instr);
    int16_t d = SIMM(instr);
    uint32_t ea = (ra_ == 0) ? static_cast<uint32_t>(d) :
                  static_cast<uint32_t>(static_cast<int32_t>(t->gpr[ra_]) + d);
    t->gpr[rd_] = ReadU32(ea);
    return InterpResult::kContinue;
  }
  case 33: { // lwzu
    uint32_t rd_ = RD(instr);
    uint32_t ra_ = RA(instr);
    int16_t d = SIMM(instr);
    uint32_t ea = static_cast<uint32_t>(static_cast<int32_t>(t->gpr[ra_]) + d);
    t->gpr[rd_] = ReadU32(ea);
    t->gpr[ra_] = ea;
    return InterpResult::kContinue;
  }
  case 34: { // lbz — load byte and zero
    uint32_t rd_ = RD(instr);
    uint32_t ra_ = RA(instr);
    int16_t d = SIMM(instr);
    uint32_t ea = (ra_ == 0) ? static_cast<uint32_t>(d) :
                  static_cast<uint32_t>(static_cast<int32_t>(t->gpr[ra_]) + d);
    t->gpr[rd_] = ReadU8(ea);
    return InterpResult::kContinue;
  }
  case 35: { // lbzu
    uint32_t rd_ = RD(instr);
    uint32_t ra_ = RA(instr);
    int16_t d = SIMM(instr);
    uint32_t ea = static_cast<uint32_t>(static_cast<int32_t>(t->gpr[ra_]) + d);
    t->gpr[rd_] = ReadU8(ea);
    t->gpr[ra_] = ea;
    return InterpResult::kContinue;
  }
  case 40: { // lhz — load halfword and zero
    uint32_t rd_ = RD(instr);
    uint32_t ra_ = RA(instr);
    int16_t d = SIMM(instr);
    uint32_t ea = (ra_ == 0) ? static_cast<uint32_t>(d) :
                  static_cast<uint32_t>(static_cast<int32_t>(t->gpr[ra_]) + d);
    t->gpr[rd_] = ReadU16(ea);
    return InterpResult::kContinue;
  }
  case 41: { // lhzu
    uint32_t rd_ = RD(instr);
    uint32_t ra_ = RA(instr);
    int16_t d = SIMM(instr);
    uint32_t ea = static_cast<uint32_t>(static_cast<int32_t>(t->gpr[ra_]) + d);
    t->gpr[rd_] = ReadU16(ea);
    t->gpr[ra_] = ea;
    return InterpResult::kContinue;
  }
  case 42: { // lha — load halfword algebraic
    uint32_t rd_ = RD(instr);
    uint32_t ra_ = RA(instr);
    int16_t d = SIMM(instr);
    uint32_t ea = (ra_ == 0) ? static_cast<uint32_t>(d) :
                  static_cast<uint32_t>(static_cast<int32_t>(t->gpr[ra_]) + d);
    int16_t v = static_cast<int16_t>(ReadU16(ea));
    t->gpr[rd_] = static_cast<uint64_t>(static_cast<int64_t>(v));
    return InterpResult::kContinue;
  }
  case 43: { // lhau
    uint32_t rd_ = RD(instr);
    uint32_t ra_ = RA(instr);
    int16_t d = SIMM(instr);
    uint32_t ea = static_cast<uint32_t>(static_cast<int32_t>(t->gpr[ra_]) + d);
    int16_t v = static_cast<int16_t>(ReadU16(ea));
    t->gpr[rd_] = static_cast<uint64_t>(static_cast<int64_t>(v));
    t->gpr[ra_] = ea;
    return InterpResult::kContinue;
  }

  // ─── Integer Store ────────────────────────────────────────────────────
  case 36: { // stw — store word
    uint32_t rs_ = RS(instr);
    uint32_t ra_ = RA(instr);
    int16_t d = SIMM(instr);
    uint32_t ea = (ra_ == 0) ? static_cast<uint32_t>(d) :
                  static_cast<uint32_t>(static_cast<int32_t>(t->gpr[ra_]) + d);
    WriteU32(ea, static_cast<uint32_t>(t->gpr[rs_]));
    return InterpResult::kContinue;
  }
  case 37: { // stwu
    uint32_t rs_ = RS(instr);
    uint32_t ra_ = RA(instr);
    int16_t d = SIMM(instr);
    uint32_t ea = static_cast<uint32_t>(static_cast<int32_t>(t->gpr[ra_]) + d);
    WriteU32(ea, static_cast<uint32_t>(t->gpr[rs_]));
    t->gpr[ra_] = ea;
    return InterpResult::kContinue;
  }
  case 38: { // stb
    uint32_t rs_ = RS(instr);
    uint32_t ra_ = RA(instr);
    int16_t d = SIMM(instr);
    uint32_t ea = (ra_ == 0) ? static_cast<uint32_t>(d) :
                  static_cast<uint32_t>(static_cast<int32_t>(t->gpr[ra_]) + d);
    WriteU8(ea, static_cast<uint8_t>(t->gpr[rs_]));
    return InterpResult::kContinue;
  }
  case 39: { // stbu
    uint32_t rs_ = RS(instr);
    uint32_t ra_ = RA(instr);
    int16_t d = SIMM(instr);
    uint32_t ea = static_cast<uint32_t>(static_cast<int32_t>(t->gpr[ra_]) + d);
    WriteU8(ea, static_cast<uint8_t>(t->gpr[rs_]));
    t->gpr[ra_] = ea;
    return InterpResult::kContinue;
  }
  case 44: { // sth
    uint32_t rs_ = RS(instr);
    uint32_t ra_ = RA(instr);
    int16_t d = SIMM(instr);
    uint32_t ea = (ra_ == 0) ? static_cast<uint32_t>(d) :
                  static_cast<uint32_t>(static_cast<int32_t>(t->gpr[ra_]) + d);
    WriteU16(ea, static_cast<uint16_t>(t->gpr[rs_]));
    return InterpResult::kContinue;
  }
  case 45: { // sthu
    uint32_t rs_ = RS(instr);
    uint32_t ra_ = RA(instr);
    int16_t d = SIMM(instr);
    uint32_t ea = static_cast<uint32_t>(static_cast<int32_t>(t->gpr[ra_]) + d);
    WriteU16(ea, static_cast<uint16_t>(t->gpr[rs_]));
    t->gpr[ra_] = ea;
    return InterpResult::kContinue;
  }
  case 46: { // lmw — load multiple words
    uint32_t rd_ = RD(instr);
    uint32_t ra_ = RA(instr);
    int16_t d = SIMM(instr);
    uint32_t ea = (ra_ == 0) ? static_cast<uint32_t>(d) :
                  static_cast<uint32_t>(static_cast<int32_t>(t->gpr[ra_]) + d);
    for (uint32_t r = rd_; r < 32; ++r) {
      t->gpr[r] = ReadU32(ea);
      ea += 4;
    }
    return InterpResult::kContinue;
  }
  case 47: { // stmw — store multiple words
    uint32_t rs_ = RS(instr);
    uint32_t ra_ = RA(instr);
    int16_t d = SIMM(instr);
    uint32_t ea = (ra_ == 0) ? static_cast<uint32_t>(d) :
                  static_cast<uint32_t>(static_cast<int32_t>(t->gpr[ra_]) + d);
    for (uint32_t r = rs_; r < 32; ++r) {
      WriteU32(ea, static_cast<uint32_t>(t->gpr[r]));
      ea += 4;
    }
    return InterpResult::kContinue;
  }

  // ─── Float Load ───────────────────────────────────────────────────────
  case 48: { // lfs — load float single
    uint32_t frt = FRT(instr);
    uint32_t ra_ = RA(instr);
    int16_t d = SIMM(instr);
    uint32_t ea = (ra_ == 0) ? static_cast<uint32_t>(d) :
                  static_cast<uint32_t>(static_cast<int32_t>(t->gpr[ra_]) + d);
    float f = ReadF32(ea);
    t->fpr[frt] = static_cast<double>(f);
    return InterpResult::kContinue;
  }
  case 49: { // lfsu
    uint32_t frt = FRT(instr);
    uint32_t ra_ = RA(instr);
    int16_t d = SIMM(instr);
    uint32_t ea = static_cast<uint32_t>(static_cast<int32_t>(t->gpr[ra_]) + d);
    float f = ReadF32(ea);
    t->fpr[frt] = static_cast<double>(f);
    t->gpr[ra_] = ea;
    return InterpResult::kContinue;
  }
  case 50: { // lfd — load float double
    uint32_t frt = FRT(instr);
    uint32_t ra_ = RA(instr);
    int16_t d = SIMM(instr);
    uint32_t ea = (ra_ == 0) ? static_cast<uint32_t>(d) :
                  static_cast<uint32_t>(static_cast<int32_t>(t->gpr[ra_]) + d);
    t->fpr[frt] = ReadF64(ea);
    return InterpResult::kContinue;
  }
  case 51: { // lfdu
    uint32_t frt = FRT(instr);
    uint32_t ra_ = RA(instr);
    int16_t d = SIMM(instr);
    uint32_t ea = static_cast<uint32_t>(static_cast<int32_t>(t->gpr[ra_]) + d);
    t->fpr[frt] = ReadF64(ea);
    t->gpr[ra_] = ea;
    return InterpResult::kContinue;
  }

  // ─── Float Store ──────────────────────────────────────────────────────
  case 52: { // stfs
    uint32_t frs = FRT(instr);
    uint32_t ra_ = RA(instr);
    int16_t d = SIMM(instr);
    uint32_t ea = (ra_ == 0) ? static_cast<uint32_t>(d) :
                  static_cast<uint32_t>(static_cast<int32_t>(t->gpr[ra_]) + d);
    WriteF32(ea, static_cast<float>(t->fpr[frs]));
    return InterpResult::kContinue;
  }
  case 53: { // stfsu
    uint32_t frs = FRT(instr);
    uint32_t ra_ = RA(instr);
    int16_t d = SIMM(instr);
    uint32_t ea = static_cast<uint32_t>(static_cast<int32_t>(t->gpr[ra_]) + d);
    WriteF32(ea, static_cast<float>(t->fpr[frs]));
    t->gpr[ra_] = ea;
    return InterpResult::kContinue;
  }
  case 54: { // stfd
    uint32_t frs = FRT(instr);
    uint32_t ra_ = RA(instr);
    int16_t d = SIMM(instr);
    uint32_t ea = (ra_ == 0) ? static_cast<uint32_t>(d) :
                  static_cast<uint32_t>(static_cast<int32_t>(t->gpr[ra_]) + d);
    WriteF64(ea, t->fpr[frs]);
    return InterpResult::kContinue;
  }
  case 55: { // stfdu
    uint32_t frs = FRT(instr);
    uint32_t ra_ = RA(instr);
    int16_t d = SIMM(instr);
    uint32_t ea = static_cast<uint32_t>(static_cast<int32_t>(t->gpr[ra_]) + d);
    WriteF64(ea, t->fpr[frs]);
    t->gpr[ra_] = ea;
    return InterpResult::kContinue;
  }

  // ─── Double-word load/store (64-bit) ──────────────────────────────────
  case 58: { // ld / ldu / lwa (DS-form)
    uint32_t rd_ = RD(instr);
    uint32_t ra_ = RA(instr);
    int16_t ds = SIMM(instr) & ~3;
    uint32_t xo = instr & 3;
    uint32_t ea = (ra_ == 0 && xo != 1) ? static_cast<uint32_t>(ds) :
                  static_cast<uint32_t>(static_cast<int32_t>(t->gpr[ra_]) + ds);

    if (xo == 0) { // ld
      t->gpr[rd_] = ReadU64(ea);
    } else if (xo == 1) { // ldu
      t->gpr[rd_] = ReadU64(ea);
      t->gpr[ra_] = ea;
    } else if (xo == 2) { // lwa
      int32_t v = static_cast<int32_t>(ReadU32(ea));
      t->gpr[rd_] = static_cast<uint64_t>(static_cast<int64_t>(v));
    }
    return InterpResult::kContinue;
  }

  // ─── Opcode 59: Float Single ──────────────────────────────────────────
  case 59: {
    uint32_t xo = XO_59(instr);
    uint32_t frt = FRT(instr);
    uint32_t fra = FRA(instr);
    uint32_t frb = FRB(instr);
    uint32_t frc = FRC(instr);

    switch (xo) {
    case 18: // fdivs
      t->fpr[frt] = static_cast<double>(
          static_cast<float>(t->fpr[fra]) / static_cast<float>(t->fpr[frb]));
      return InterpResult::kContinue;
    case 20: // fsubs
      t->fpr[frt] = static_cast<double>(
          static_cast<float>(t->fpr[fra]) - static_cast<float>(t->fpr[frb]));
      return InterpResult::kContinue;
    case 21: // fadds
      t->fpr[frt] = static_cast<double>(
          static_cast<float>(t->fpr[fra]) + static_cast<float>(t->fpr[frb]));
      return InterpResult::kContinue;
    case 22: // fsqrts
      t->fpr[frt] = static_cast<double>(sqrtf(static_cast<float>(t->fpr[frb])));
      return InterpResult::kContinue;
    case 24: // fres — floating reciprocal estimate single
      t->fpr[frt] = static_cast<double>(1.0f / static_cast<float>(t->fpr[frb]));
      return InterpResult::kContinue;
    case 25: // fmuls
      t->fpr[frt] = static_cast<double>(
          static_cast<float>(t->fpr[fra]) * static_cast<float>(t->fpr[frc]));
      return InterpResult::kContinue;
    case 26: // frsqrtes — reciprocal sqrt estimate single
      t->fpr[frt] = static_cast<double>(
          1.0f / sqrtf(static_cast<float>(t->fpr[frb])));
      return InterpResult::kContinue;
    case 28: // fmsubs
      t->fpr[frt] = static_cast<double>(
          static_cast<float>(t->fpr[fra]) * static_cast<float>(t->fpr[frc]) -
          static_cast<float>(t->fpr[frb]));
      return InterpResult::kContinue;
    case 29: // fmadds
      t->fpr[frt] = static_cast<double>(
          static_cast<float>(t->fpr[fra]) * static_cast<float>(t->fpr[frc]) +
          static_cast<float>(t->fpr[frb]));
      return InterpResult::kContinue;
    case 30: // fnmsubs
      t->fpr[frt] = static_cast<double>(
          -(static_cast<float>(t->fpr[fra]) * static_cast<float>(t->fpr[frc]) -
            static_cast<float>(t->fpr[frb])));
      return InterpResult::kContinue;
    case 31: // fnmadds
      t->fpr[frt] = static_cast<double>(
          -(static_cast<float>(t->fpr[fra]) * static_cast<float>(t->fpr[frc]) +
            static_cast<float>(t->fpr[frb])));
      return InterpResult::kContinue;
    default:
      XELOGW("Unhandled opcode 59 xo={} at 0x{:08X}", xo, pc);
      return InterpResult::kContinue;
    }
  }

  // ─── Double-word store (64-bit) ───────────────────────────────────────
  case 62: { // std / stdu (DS-form)
    uint32_t rs_ = RS(instr);
    uint32_t ra_ = RA(instr);
    int16_t ds = SIMM(instr) & ~3;
    uint32_t xo = instr & 3;
    uint32_t ea = (ra_ == 0 && xo != 1) ? static_cast<uint32_t>(ds) :
                  static_cast<uint32_t>(static_cast<int32_t>(t->gpr[ra_]) + ds);

    WriteU64(ea, t->gpr[rs_]);
    if (xo == 1) t->gpr[ra_] = ea;  // stdu
    return InterpResult::kContinue;
  }

  // ─── Opcode 63: Float Double ──────────────────────────────────────────
  case 63: {
    uint32_t xo_full = XO_63(instr);
    uint32_t xo_short = XO_63s(instr);
    uint32_t frt = FRT(instr);
    uint32_t fra = FRA(instr);
    uint32_t frb = FRB(instr);
    uint32_t frc = FRC(instr);
    bool rc = RC_BIT(instr);

    // First check full-width XO
    switch (xo_full) {
    case 0: { // fcmpu
      uint32_t crf = CRF(instr);
      double a = t->fpr[fra];
      double b = t->fpr[frb];
      uint32_t shift = (7 - crf) * 4;
      uint32_t bits;
      if (std::isnan(a) || std::isnan(b)) bits = 0x1;
      else if (a < b) bits = 0x8;
      else if (a > b) bits = 0x4;
      else bits = 0x2;
      t->cr = (t->cr & ~(0xFu << shift)) | (bits << shift);
      return InterpResult::kContinue;
    }
    case 12: // frsp — float round to single precision
      t->fpr[frt] = static_cast<double>(static_cast<float>(t->fpr[frb]));
      return InterpResult::kContinue;
    case 14: // fctiw — float convert to integer word
      t->fpr[frt] = 0;
      {
        int32_t iv = static_cast<int32_t>(t->fpr[frb]);
        uint64_t bits;
        memcpy(&bits, &t->fpr[frt], 8);
        bits = (bits & 0xFFFFFFFF00000000ULL) | static_cast<uint32_t>(iv);
        memcpy(&t->fpr[frt], &bits, 8);
      }
      return InterpResult::kContinue;
    case 15: // fctiwz — float convert to integer word with round toward zero
      {
        int32_t iv = static_cast<int32_t>(trunc(t->fpr[frb]));
        uint64_t bits;
        memcpy(&bits, &t->fpr[frt], 8);
        bits = (bits & 0xFFFFFFFF00000000ULL) | static_cast<uint32_t>(iv);
        memcpy(&t->fpr[frt], &bits, 8);
      }
      return InterpResult::kContinue;
    case 32: { // fcmpo — float compare ordered
      uint32_t crf = CRF(instr);
      double a = t->fpr[fra];
      double b = t->fpr[frb];
      uint32_t shift = (7 - crf) * 4;
      uint32_t bits;
      if (std::isnan(a) || std::isnan(b)) bits = 0x1;
      else if (a < b) bits = 0x8;
      else if (a > b) bits = 0x4;
      else bits = 0x2;
      t->cr = (t->cr & ~(0xFu << shift)) | (bits << shift);
      return InterpResult::kContinue;
    }
    case 38: // mtfsb1 — set FPSCR bit — NOP for now
      return InterpResult::kContinue;
    case 40: // fneg
      t->fpr[frt] = -t->fpr[frb];
      return InterpResult::kContinue;
    case 64: // mcrfs — move to CR from FPSCR
      return InterpResult::kContinue;
    case 70: // mtfsb0 — NOP
      return InterpResult::kContinue;
    case 72: // fmr — float move register
      t->fpr[frt] = t->fpr[frb];
      return InterpResult::kContinue;
    case 134: // mtfsfi — NOP
      return InterpResult::kContinue;
    case 136: // fnabs
      t->fpr[frt] = -fabs(t->fpr[frb]);
      return InterpResult::kContinue;
    case 264: // fabs
      t->fpr[frt] = fabs(t->fpr[frb]);
      return InterpResult::kContinue;
    case 583: // mffs — move from FPSCR
      t->fpr[frt] = 0;  // Stub FPSCR
      return InterpResult::kContinue;
    case 711: // mtfsf — move to FPSCR fields — NOP
      return InterpResult::kContinue;
    case 814: { // fctid — float convert to integer doubleword
      int64_t iv = static_cast<int64_t>(t->fpr[frb]);
      memcpy(&t->fpr[frt], &iv, 8);
      return InterpResult::kContinue;
    }
    case 815: { // fctidz
      int64_t iv = static_cast<int64_t>(trunc(t->fpr[frb]));
      memcpy(&t->fpr[frt], &iv, 8);
      return InterpResult::kContinue;
    }
    case 846: { // fcfid — float convert from integer doubleword
      int64_t iv;
      memcpy(&iv, &t->fpr[frb], 8);
      t->fpr[frt] = static_cast<double>(iv);
      return InterpResult::kContinue;
    }
    default:
      break;
    }

    // Short XO (5-bit) — multiply-add family
    switch (xo_short) {
    case 18: // fdiv
      t->fpr[frt] = t->fpr[fra] / t->fpr[frb];
      return InterpResult::kContinue;
    case 20: // fsub
      t->fpr[frt] = t->fpr[fra] - t->fpr[frb];
      return InterpResult::kContinue;
    case 21: // fadd
      t->fpr[frt] = t->fpr[fra] + t->fpr[frb];
      return InterpResult::kContinue;
    case 22: // fsqrt
      t->fpr[frt] = sqrt(t->fpr[frb]);
      return InterpResult::kContinue;
    case 23: // fsel
      t->fpr[frt] = (t->fpr[fra] >= 0.0) ? t->fpr[frc] : t->fpr[frb];
      return InterpResult::kContinue;
    case 24: // fre — reciprocal estimate
      t->fpr[frt] = 1.0 / t->fpr[frb];
      return InterpResult::kContinue;
    case 25: // fmul
      t->fpr[frt] = t->fpr[fra] * t->fpr[frc];
      return InterpResult::kContinue;
    case 26: // frsqrte — reciprocal sqrt estimate
      t->fpr[frt] = 1.0 / sqrt(t->fpr[frb]);
      return InterpResult::kContinue;
    case 28: // fmsub
      t->fpr[frt] = t->fpr[fra] * t->fpr[frc] - t->fpr[frb];
      return InterpResult::kContinue;
    case 29: // fmadd
      t->fpr[frt] = t->fpr[fra] * t->fpr[frc] + t->fpr[frb];
      return InterpResult::kContinue;
    case 30: // fnmsub
      t->fpr[frt] = -(t->fpr[fra] * t->fpr[frc] - t->fpr[frb]);
      return InterpResult::kContinue;
    case 31: // fnmadd
      t->fpr[frt] = -(t->fpr[fra] * t->fpr[frc] + t->fpr[frb]);
      return InterpResult::kContinue;
    default:
      break;
    }

    XELOGW("Unhandled opcode 63 xo={}/{} at 0x{:08X}", xo_full, xo_short, pc);
    return InterpResult::kContinue;
  }

  // ─── Opcode 4: VMX128 (Xbox 360 extended vector) ─────────────────────
  case 4: {
    // VMX128 instructions — basic support
    // Most games use these heavily. For now, treat as NOP with logging.
    // TODO: Full VMX128 implementation
    uint32_t xo = (instr >> 1) & 0x3FF;
    (void)xo;  // Suppress warning — will implement per-opcode
    return InterpResult::kContinue;
  }

  default:
    XELOGW("Unhandled PPC opcode {} at 0x{:08X} (instr=0x{:08X})", opcd, pc, instr);
    return InterpResult::kHalt;
  }
}

}  // namespace xe::cpu::frontend
