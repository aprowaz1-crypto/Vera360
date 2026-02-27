/**
 * Vera360 — Xenia Edge
 * ARM64 JIT Backend — Recompiles PPC guest code to native AArch64
 */
#pragma once

#include "xenia/cpu/backend/arm64/arm64_emitter.h"
#include <cstdint>
#include <unordered_map>
#include <memory>

namespace xe::cpu::backend::arm64 {

/// Register allocation map: PPC register → ARM64 register
struct RegisterAllocation {
  // PPC GPR (r0-r31) → ARM64 X registers
  // We use X19-X28 (callee-saved) for hot PPC GPRs
  // X0-X7 are scratch / argument passing
  // X8 = guest memory base pointer
  // X9 = PPC context pointer (thread state)
  // X10-X15 = scratch for instruction lowering
  // X16-X17 = intra-procedure-call scratch (linker)
  // X18 = platform register (reserved on Android)
  // X29 = frame pointer
  // X30 = link register

  static constexpr Reg kGuestMemBase = Reg::X8;
  static constexpr Reg kContextPtr   = Reg::X9;
  static constexpr Reg kScratch0     = Reg::X10;
  static constexpr Reg kScratch1     = Reg::X11;
  static constexpr Reg kScratch2     = Reg::X12;
  static constexpr Reg kScratch3     = Reg::X13;

  // PPC GPR mapping: r3-r12 are hot (function args + temporaries)
  // Map to callee-saved X19-X28 for persistence across calls
  static constexpr Reg kPpcGpr[10] = {
    Reg::X19, // PPC r3
    Reg::X20, // PPC r4
    Reg::X21, // PPC r5
    Reg::X22, // PPC r6
    Reg::X23, // PPC r7
    Reg::X24, // PPC r8
    Reg::X25, // PPC r9
    Reg::X26, // PPC r10
    Reg::X27, // PPC r11
    Reg::X28, // PPC r12
  };

  // PPC FPR/VMX → ARM64 NEON V registers
  // V0-V7 = scratch
  // V8-V15 = callee-saved (map to hot PPC FPR)
  // V16-V31 = additional PPC VMX128 vectors
};

/// Compiled code block (PPC function → ARM64)
struct CodeBlock {
  uint32_t guest_address = 0;
  uint32_t guest_size = 0;
  void* host_code = nullptr;
  size_t host_code_size = 0;
};

/**
 * ARM64 JIT Backend
 * Translates PPC instructions to ARM64 machine code.
 */
class ARM64Backend {
 public:
  ARM64Backend();
  ~ARM64Backend();

  bool Initialize();
  void Shutdown();

  /// Compile a PPC function starting at guest_address
  CodeBlock* CompileFunction(uint32_t guest_address);

  /// Look up already-compiled code for a guest address
  CodeBlock* LookupCode(uint32_t guest_address);

  /// Execute native code for a guest address
  void Execute(uint32_t guest_address, void* context);

  /// Invalidate compiled code (e.g., self-modifying code)
  void InvalidateCode(uint32_t guest_address, uint32_t size);

  /// Get compilation statistics
  uint64_t GetTotalCompiled() const { return total_compiled_; }
  uint64_t GetTotalCodeSize() const { return total_code_size_; }

 private:
  /// Emit prologue: save callee-saved registers, set up context
  void EmitPrologue();

  /// Emit epilogue: restore registers, return
  void EmitEpilogue();

  /// Emit a single PPC instruction
  bool EmitInstruction(uint32_t guest_addr, uint32_t ppc_instr);

  ARM64Emitter emitter_;
  std::unordered_map<uint32_t, std::unique_ptr<CodeBlock>> code_cache_;

  uint64_t total_compiled_ = 0;
  uint64_t total_code_size_ = 0;
};

}  // namespace xe::cpu::backend::arm64
