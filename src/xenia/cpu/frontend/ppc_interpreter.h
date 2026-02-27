/**
 * Vera360 — Xenia Edge
 * PPC Software Interpreter — executes PowerPC (Xenon) instructions directly
 *
 * This interpreter runs guest PPC code on the host CPU without JIT compilation.
 * It implements the full Xbox 360 PPC instruction set needed for game execution:
 *   - Integer arithmetic, logical, shift/rotate
 *   - Floating-point scalar (double + single)
 *   - Load/store (byte, half, word, double, byte-reverse, indexed, update)
 *   - Branch (unconditional, conditional, CTR, LR, absolute)
 *   - Compare (signed, unsigned, 32-bit, 64-bit)
 *   - Condition register operations
 *   - System calls (HLE thunk dispatch)
 *   - VMX128 SIMD (select operations)
 *   - Atomic (lwarx/stwcx)
 *   - Trap (tw/td — used for debugging)
 *   - Cache operations (dcbz, icbi — NOPs on host)
 *   - Special purpose register moves (mfspr, mtspr, mfcr, mtcrf)
 */
#pragma once

#include "xenia/cpu/processor.h"
#include <cstdint>
#include <functional>
#include <unordered_map>

namespace xe::cpu::frontend {

/// Result of a single instruction execution
enum class InterpResult : uint8_t {
  kContinue = 0,  // Move to next instruction (PC += 4 already applied)
  kBranch,        // PC was changed by a branch
  kSyscall,       // System call — dispatch to HLE
  kTrap,          // Trap instruction hit
  kHalt,          // Halted (invalid instruction or debug break)
  kReturn,        // blr — function returned
};

/// HLE import thunk callback: called when guest hits a syscall/thunk.
/// Args: thread_state*, ordinal → return value written to r3.
using HleDispatchFn = std::function<void(ThreadState*, uint32_t ordinal)>;

class PPCInterpreter {
 public:
  PPCInterpreter();
  ~PPCInterpreter();

  /// Set the guest memory base pointer
  void SetGuestBase(uint8_t* base) { guest_base_ = base; }

  /// Set the HLE dispatch callback (handles kernel import thunks)
  void SetHleDispatch(HleDispatchFn fn) { hle_dispatch_ = std::move(fn); }

  /// Execute a single PPC instruction at thread->pc
  InterpResult Step(ThreadState* thread);

  /// Run until blr, halt, or max_instructions reached
  /// Returns the number of instructions executed.
  uint64_t Run(ThreadState* thread, uint64_t max_instructions = 0);

  /// Check if an address is an HLE thunk
  bool IsThunkAddress(uint32_t addr) const;

  /// Register an HLE thunk address → ordinal mapping
  void RegisterThunk(uint32_t guest_addr, uint32_t ordinal);

  /// Stats
  uint64_t instructions_executed() const { return instructions_executed_; }

 private:
  // ── Memory access helpers (big-endian guest) ──────────────────────────
  uint8_t  ReadU8(uint32_t addr) const;
  uint16_t ReadU16(uint32_t addr) const;
  uint32_t ReadU32(uint32_t addr) const;
  uint64_t ReadU64(uint32_t addr) const;
  float    ReadF32(uint32_t addr) const;
  double   ReadF64(uint32_t addr) const;

  void WriteU8(uint32_t addr, uint8_t val);
  void WriteU16(uint32_t addr, uint16_t val);
  void WriteU32(uint32_t addr, uint32_t val);
  void WriteU64(uint32_t addr, uint64_t val);
  void WriteF32(uint32_t addr, float val);
  void WriteF64(uint32_t addr, double val);

  // ── Instruction field extractors ──────────────────────────────────────
  static uint32_t OPCD(uint32_t i)  { return (i >> 26) & 0x3F; }
  static uint32_t RD(uint32_t i)    { return (i >> 21) & 0x1F; }
  static uint32_t RS(uint32_t i)    { return (i >> 21) & 0x1F; }
  static uint32_t RA(uint32_t i)    { return (i >> 16) & 0x1F; }
  static uint32_t RB(uint32_t i)    { return (i >> 11) & 0x1F; }
  static uint32_t RC_BIT(uint32_t i){ return i & 1; }
  static int16_t  SIMM(uint32_t i)  { return static_cast<int16_t>(i & 0xFFFF); }
  static uint16_t UIMM(uint32_t i)  { return i & 0xFFFF; }
  static uint32_t XO_31(uint32_t i) { return (i >> 1) & 0x3FF; }
  static uint32_t XO_19(uint32_t i) { return (i >> 1) & 0x3FF; }
  static uint32_t XO_59(uint32_t i) { return (i >> 1) & 0x1F; }
  static uint32_t XO_63(uint32_t i) { return (i >> 1) & 0x3FF; }
  static uint32_t XO_63s(uint32_t i){ return (i >> 1) & 0x1F; }
  static uint32_t BO(uint32_t i)    { return (i >> 21) & 0x1F; }
  static uint32_t BI(uint32_t i)    { return (i >> 16) & 0x1F; }
  static uint32_t SH(uint32_t i)    { return (i >> 11) & 0x1F; }
  static uint32_t MB(uint32_t i)    { return (i >> 6) & 0x1F; }
  static uint32_t ME(uint32_t i)    { return (i >> 1) & 0x1F; }
  static uint32_t OE(uint32_t i)    { return (i >> 10) & 1; }
  static uint32_t LK(uint32_t i)    { return i & 1; }
  static uint32_t AA(uint32_t i)    { return (i >> 1) & 1; }
  static uint32_t CRF(uint32_t i)   { return (i >> 23) & 0x7; }
  static uint32_t L_BIT(uint32_t i) { return (i >> 21) & 1; }
  static uint32_t FRT(uint32_t i)   { return (i >> 21) & 0x1F; }
  static uint32_t FRA(uint32_t i)   { return (i >> 16) & 0x1F; }
  static uint32_t FRB(uint32_t i)   { return (i >> 11) & 0x1F; }
  static uint32_t FRC(uint32_t i)   { return (i >> 6) & 0x1F; }
  static uint32_t SPR(uint32_t i)   { return ((i >> 16) & 0x1F) | (((i >> 11) & 0x1F) << 5); }
  static uint32_t TBR(uint32_t i)   { return ((i >> 16) & 0x1F) | (((i >> 11) & 0x1F) << 5); }

  // ── CR helpers ────────────────────────────────────────────────────────
  void UpdateCR0(ThreadState* t, int64_t result);
  void UpdateCR(ThreadState* t, uint32_t field, int64_t a, int64_t b);
  void UpdateCRU(ThreadState* t, uint32_t field, uint64_t a, uint64_t b);
  bool EvalBranchCondition(ThreadState* t, uint32_t bo, uint32_t bi);

  // ── Rotate/mask helper ────────────────────────────────────────────────
  static uint32_t BuildMask32(uint32_t mb, uint32_t me);
  static uint64_t BuildMask64(uint32_t mb, uint32_t me);

  uint8_t* guest_base_ = nullptr;
  HleDispatchFn hle_dispatch_;
  std::unordered_map<uint32_t, uint32_t> thunk_map_;  // guest_addr → ordinal
  uint64_t instructions_executed_ = 0;
};

}  // namespace xe::cpu::frontend
