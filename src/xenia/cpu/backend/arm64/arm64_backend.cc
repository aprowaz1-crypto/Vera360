/**
 * Vera360 — Xenia Edge
 * ARM64 JIT Backend implementation
 */

#include "xenia/cpu/backend/arm64/arm64_backend.h"
#include "xenia/cpu/backend/arm64/arm64_sequences.h"
#include "xenia/base/memory/memory.h"
#include "xenia/base/logging.h"

#include <cstring>

namespace xe::cpu::backend::arm64 {

ARM64Backend::ARM64Backend() = default;
ARM64Backend::~ARM64Backend() { Shutdown(); }

bool ARM64Backend::Initialize() {
  XELOGI("ARM64 JIT backend initialized");
  XELOGI("  Register mapping: X8=guestmem, X9=ctx, X19-X28=PPC GPR");
  return true;
}

void ARM64Backend::Shutdown() {
  // Free all compiled code blocks
  for (auto& [addr, block] : code_cache_) {
    if (block->host_code) {
      xe::memory::FreeExecutable(block->host_code, block->host_code_size);
    }
  }
  code_cache_.clear();
  XELOGI("ARM64 JIT backend shut down ({} functions, {} bytes total)",
         total_compiled_, total_code_size_);
}

CodeBlock* ARM64Backend::CompileFunction(uint32_t guest_address) {
  // Check cache first
  auto it = code_cache_.find(guest_address);
  if (it != code_cache_.end()) {
    return it->second.get();
  }

  emitter_.Reset();

  // Emit function prologue
  EmitPrologue();

  // Read PPC instructions from guest memory and translate
  uint8_t* guest_base = xe::memory::GetGuestBase();
  if (!guest_base) {
    XELOGE("Guest memory not initialized");
    return nullptr;
  }

  uint32_t pc = guest_address;
  uint32_t function_size = 0;
  bool done = false;

  while (!done && function_size < 0x10000) {  // Max 64KB per function
    uint32_t ppc_instr;
    memcpy(&ppc_instr, guest_base + pc, sizeof(uint32_t));
    
    // PPC is big-endian, swap on ARM64 (little-endian)
    ppc_instr = __builtin_bswap32(ppc_instr);

    if (!EmitInstruction(pc, ppc_instr)) {
      XELOGW("Failed to emit PPC instruction at 0x{:08X}: 0x{:08X}", pc, ppc_instr);
      // Emit a break as fallback
      emitter_.BRK(0xBAD);
    }

    // Check for blr (function return)
    uint32_t opcode = (ppc_instr >> 26) & 0x3F;
    if (opcode == 19) {  // Extended opcode group
      uint32_t xo = (ppc_instr >> 1) & 0x3FF;
      if (xo == 16 || xo == 528) {  // bclr / bcctr
        done = true;
      }
    }

    pc += 4;
    function_size += 4;
  }

  // Emit epilogue
  EmitEpilogue();

  // Finalize to executable
  void* code = emitter_.FinalizeToExecutable();
  if (!code) {
    XELOGE("Failed to finalize code for 0x{:08X}", guest_address);
    return nullptr;
  }

  auto block = std::make_unique<CodeBlock>();
  block->guest_address = guest_address;
  block->guest_size = function_size;
  block->host_code = code;
  block->host_code_size = emitter_.GetCodeSize();

  CodeBlock* result = block.get();
  code_cache_[guest_address] = std::move(block);

  total_compiled_++;
  total_code_size_ += result->host_code_size;

  XELOGD("Compiled PPC 0x{:08X} ({} bytes) → ARM64 ({} bytes)",
         guest_address, function_size, result->host_code_size);

  return result;
}

CodeBlock* ARM64Backend::LookupCode(uint32_t guest_address) {
  auto it = code_cache_.find(guest_address);
  return (it != code_cache_.end()) ? it->second.get() : nullptr;
}

void ARM64Backend::Execute(uint32_t guest_address, void* context) {
  CodeBlock* block = LookupCode(guest_address);
  if (!block) {
    block = CompileFunction(guest_address);
  }
  if (!block || !block->host_code) {
    XELOGE("No code available for 0x{:08X}", guest_address);
    return;
  }

  // Call the compiled function
  // Prototype: void compiled_func(void* context, uint8_t* guest_base)
  using JitFunc = void(*)(void*, uint8_t*);
  auto func = reinterpret_cast<JitFunc>(block->host_code);
  func(context, xe::memory::GetGuestBase());
}

void ARM64Backend::InvalidateCode(uint32_t guest_address, uint32_t size) {
  // Remove any compiled blocks that overlap [guest_address, guest_address+size)
  auto it = code_cache_.begin();
  while (it != code_cache_.end()) {
    auto& block = it->second;
    uint32_t block_end = block->guest_address + block->guest_size;
    uint32_t inv_end = guest_address + size;
    
    if (block->guest_address < inv_end && block_end > guest_address) {
      if (block->host_code) {
        xe::memory::FreeExecutable(block->host_code, block->host_code_size);
      }
      it = code_cache_.erase(it);
    } else {
      ++it;
    }
  }
}

void ARM64Backend::EmitPrologue() {
  // Save callee-saved registers and set up frame
  // STP X29, X30, [SP, #-16]!
  emitter_.STP(Reg::X29, Reg::X30, Reg::SP, -16);
  emitter_.MOV(Reg::X29, Reg::SP);
  
  // Save PPC register mapping (X19-X28)
  emitter_.STP(Reg::X19, Reg::X20, Reg::SP, -16);
  emitter_.STP(Reg::X21, Reg::X22, Reg::SP, -16);
  emitter_.STP(Reg::X23, Reg::X24, Reg::SP, -16);
  emitter_.STP(Reg::X25, Reg::X26, Reg::SP, -16);
  emitter_.STP(Reg::X27, Reg::X28, Reg::SP, -16);

  // X0 = context pointer → X9, X1 = guest memory base → X8
  emitter_.MOV(RegisterAllocation::kContextPtr, Reg::X0);
  emitter_.MOV(RegisterAllocation::kGuestMemBase, Reg::X1);
}

void ARM64Backend::EmitEpilogue() {
  // Restore callee-saved registers
  emitter_.LDP(Reg::X27, Reg::X28, Reg::SP, 0);
  emitter_.ADD_imm(Reg::SP, Reg::SP, 16);
  emitter_.LDP(Reg::X25, Reg::X26, Reg::SP, 0);
  emitter_.ADD_imm(Reg::SP, Reg::SP, 16);
  emitter_.LDP(Reg::X23, Reg::X24, Reg::SP, 0);
  emitter_.ADD_imm(Reg::SP, Reg::SP, 16);
  emitter_.LDP(Reg::X21, Reg::X22, Reg::SP, 0);
  emitter_.ADD_imm(Reg::SP, Reg::SP, 16);
  emitter_.LDP(Reg::X19, Reg::X20, Reg::SP, 0);
  emitter_.ADD_imm(Reg::SP, Reg::SP, 16);

  // Restore frame and return
  emitter_.LDP(Reg::X29, Reg::X30, Reg::SP, 0);
  emitter_.ADD_imm(Reg::SP, Reg::SP, 16);
  emitter_.RET();
}

bool ARM64Backend::EmitInstruction(uint32_t guest_addr, uint32_t ppc_instr) {
  return ARM64Sequences::Emit(emitter_, guest_addr, ppc_instr);
}

}  // namespace xe::cpu::backend::arm64
