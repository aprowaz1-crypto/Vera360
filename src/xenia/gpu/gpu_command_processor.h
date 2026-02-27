/**
 * Vera360 — Xenia Edge
 * GPU Command Processor — processes Xenos GPU command ring buffer
 */
#pragma once

#include "xenia/gpu/xenos_registers.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>
#include <vulkan/vulkan.h>

namespace xe::gpu {

namespace vulkan {
class VulkanInstance;
class VulkanDevice;
class VulkanSwapChain;
}

/// Command types in the ring buffer
enum class PM4Type : uint32_t {
  kType0 = 0,  // Register write
  kType1 = 1,  // Reserved
  kType2 = 2,  // NOP
  kType3 = 3,  // Packet (draw, state, etc.)
};

/// PM4 Type-3 opcodes
namespace PM4Opcode {
  constexpr uint32_t ME_INIT             = 0x48;
  constexpr uint32_t NOP                 = 0x10;
  constexpr uint32_t INTERRUPT           = 0x40;
  constexpr uint32_t INDIRECT_BUFFER     = 0x3F;
  constexpr uint32_t WAIT_REG_MEM        = 0x3C;
  constexpr uint32_t REG_RMW            = 0x21;
  constexpr uint32_t COND_WRITE          = 0x45;
  constexpr uint32_t EVENT_WRITE         = 0x46;
  constexpr uint32_t EVENT_WRITE_SHD     = 0x58;
  constexpr uint32_t SET_CONSTANT        = 0x2D;
  constexpr uint32_t SET_CONSTANT_2      = 0x55;
  constexpr uint32_t LOAD_ALU_CONSTANT   = 0x2F;
  constexpr uint32_t IM_LOAD             = 0x20;
  constexpr uint32_t IM_LOAD_IMMEDIATE   = 0x23; // Actually per Xenos docs
  constexpr uint32_t SET_SHADER_CONSTANTS= 0x56;
  constexpr uint32_t DRAW_INDX           = 0x22;
  constexpr uint32_t DRAW_INDX_2         = 0x36;
  constexpr uint32_t VIZ_QUERY           = 0x23;
  constexpr uint32_t MEM_WRITE           = 0x3D;
  constexpr uint32_t REG_TO_MEM          = 0x3E;
  constexpr uint32_t INVALIDATE_STATE    = 0x3B;
  constexpr uint32_t SET_BIN_MASK        = 0x50;
  constexpr uint32_t SET_BIN_SELECT      = 0x51;
  constexpr uint32_t CONTEXT_UPDATE      = 0x5E;
  constexpr uint32_t WAIT_FOR_IDLE       = 0x26;
}

/// GPU MMIO regions — guest writes to these ranges trigger GPU processing
namespace GpuMmio {
  constexpr uint32_t kGpuRegBase     = 0x7C800000; // GPU registers start
  constexpr uint32_t kRbWritePtr     = 0x0714;      // Ring buffer write pointer
  constexpr uint32_t kRbRptrAddr     = 0x070C;      // Ring buffer read pointer address
  constexpr uint32_t kRbCntl         = 0x0704;      // Ring buffer control
  constexpr uint32_t kRbBaseAddr     = 0x0700;      // Ring buffer base address
  constexpr uint32_t kScratchAddr    = 0x0578;      // Scratch register base
  constexpr uint32_t kScratchUmsk    = 0x057C;      // Scratch register mask
}

/// Recorded draw call for the frame
struct DrawCall {
  uint32_t prim_type;
  uint32_t num_indices;
  uint32_t index_type;        // 0=auto, 1=16bit, 2=32bit
  uint32_t vgt_draw_initiator;
  // Vertex fetch info from registers
  uint32_t vertex_base_addr;
  uint32_t vertex_stride;
  uint32_t index_base_addr;
  uint32_t index_size;
  // Render state snapshot
  uint32_t rb_colorcontrol;
  uint32_t rb_blendcontrol;
  uint32_t rb_depthcontrol;
  uint32_t pa_su_sc_mode;
  uint32_t sq_program_cntl;
  // Surface info
  uint32_t rb_surface_info;
  uint32_t rb_color_info;
  uint32_t rb_depth_info;
};

/**
 * GPU Command Processor — reads and executes Xenos PM4 command packets
 */
class GpuCommandProcessor {
 public:
  GpuCommandProcessor();
  ~GpuCommandProcessor();

  bool Initialize(vulkan::VulkanDevice* device);
  void Shutdown();

  /// Set the ring buffer address and size in guest memory
  void SetRingBuffer(uint32_t base_address, uint32_t size_dwords);

  /// Process commands from the ring buffer
  void ProcessRingBuffer(uint32_t read_ptr, uint32_t write_ptr);

  /// Write a register value (MMIO write from CPU)
  void WriteRegister(uint32_t index, uint32_t value);

  /// Read a register value
  uint32_t ReadRegister(uint32_t index) const;

  /// Handle GPU MMIO access from guest PPC code
  /// Returns true if the address was in GPU MMIO range
  bool HandleMmioWrite(uint32_t guest_addr, uint32_t value);
  uint32_t HandleMmioRead(uint32_t guest_addr);

  /// Set callback for GPU interrupts to CPU
  void SetInterruptCallback(std::function<void(uint32_t)> callback);

  const XenosRegisters& GetRegisters() const { return regs_; }

  /// Check if ring buffer write pointer was updated (needs processing)
  bool HasPendingCommands() const { return ring_write_ptr_ != ring_read_ptr_; }

  /// Process all pending ring buffer commands
  void ProcessPendingCommands();

  /// Get the draw calls recorded this frame, then clear
  const std::vector<DrawCall>& GetDrawCalls() const { return draw_calls_; }
  void ClearDrawCalls() { draw_calls_.clear(); }

  uint32_t draw_call_count() const { return total_draw_calls_; }

 private:
  void ExecutePacketType0(uint32_t header, const uint32_t* data);
  void ExecutePacketType3(uint32_t header, const uint32_t* data, uint32_t count);
  
  void HandleDraw(uint32_t opcode, const uint32_t* data, uint32_t count);
  void HandleSetConstant(const uint32_t* data, uint32_t count);
  void HandleEventWrite(const uint32_t* data, uint32_t count);
  void HandleMemWrite(const uint32_t* data, uint32_t count);
  void HandleRegToMem(const uint32_t* data, uint32_t count);
  void HandleWaitRegMem(const uint32_t* data, uint32_t count);
  void HandleIndirectBuffer(const uint32_t* data, uint32_t count);

  XenosRegisters regs_;
  vulkan::VulkanDevice* vk_device_ = nullptr;
  
  uint32_t ring_base_ = 0;
  uint32_t ring_size_ = 0;
  uint32_t ring_read_ptr_ = 0;
  uint32_t ring_write_ptr_ = 0;
  
  std::function<void(uint32_t)> interrupt_callback_;
  std::vector<DrawCall> draw_calls_;
  uint32_t total_draw_calls_ = 0;
};

}  // namespace xe::gpu
