/**
 * Vera360 — Xenia Edge
 * GPU Command Processor implementation
 *
 * Parses Xenos PM4 command packets from the ring buffer, records draw calls
 * with full register state snapshots, and handles GPU MMIO access.
 */

#include "xenia/gpu/gpu_command_processor.h"
#include "xenia/base/memory/memory.h"
#include "xenia/base/logging.h"

#include <cstring>

namespace xe::gpu {

GpuCommandProcessor::GpuCommandProcessor() = default;
GpuCommandProcessor::~GpuCommandProcessor() { Shutdown(); }

bool GpuCommandProcessor::Initialize(vulkan::VulkanDevice* device) {
  vk_device_ = device;
  draw_calls_.reserve(256);
  XELOGI("GPU Command Processor initialized");
  return true;
}

void GpuCommandProcessor::Shutdown() {
  vk_device_ = nullptr;
  draw_calls_.clear();
}

void GpuCommandProcessor::SetRingBuffer(uint32_t base_address, uint32_t size_dwords) {
  ring_base_ = base_address;
  ring_size_ = size_dwords;
  ring_read_ptr_ = 0;
  ring_write_ptr_ = 0;
  XELOGI("Ring buffer set: base=0x{:08X}, size={} DW", base_address, size_dwords);
}

void GpuCommandProcessor::WriteRegister(uint32_t index, uint32_t value) {
  regs_.Set(index, value);
}

uint32_t GpuCommandProcessor::ReadRegister(uint32_t index) const {
  return regs_.Get(index);
}

void GpuCommandProcessor::SetInterruptCallback(std::function<void(uint32_t)> callback) {
  interrupt_callback_ = std::move(callback);
}

// ── MMIO handlers — intercept guest writes to GPU register space ────────────

bool GpuCommandProcessor::HandleMmioWrite(uint32_t guest_addr, uint32_t value) {
  if (guest_addr < GpuMmio::kGpuRegBase) return false;
  uint32_t reg_offset = (guest_addr - GpuMmio::kGpuRegBase) >> 2;

  regs_.Set(reg_offset, value);

  // Check for special GPU registers
  if (reg_offset == GpuMmio::kRbWritePtr) {
    ring_write_ptr_ = value;
  } else if (reg_offset == GpuMmio::kRbBaseAddr) {
    ring_base_ = value << 8;  // Base is in 256-byte units
    XELOGI("GPU ring buffer base set: 0x{:08X}", ring_base_);
  } else if (reg_offset == GpuMmio::kRbCntl) {
    uint32_t log2_size = value & 0x3F;
    ring_size_ = 1u << log2_size;  // Size in DWORDs
    XELOGI("GPU ring buffer size: {} DW", ring_size_);
  }

  return true;
}

uint32_t GpuCommandProcessor::HandleMmioRead(uint32_t guest_addr) {
  if (guest_addr < GpuMmio::kGpuRegBase) return 0;
  uint32_t reg_offset = (guest_addr - GpuMmio::kGpuRegBase) >> 2;

  if (reg_offset == GpuMmio::kRbRptrAddr) {
    return ring_read_ptr_;
  }
  return regs_.Get(reg_offset);
}

void GpuCommandProcessor::ProcessPendingCommands() {
  if (ring_write_ptr_ == ring_read_ptr_) return;
  if (ring_size_ == 0) return;
  ProcessRingBuffer(ring_read_ptr_, ring_write_ptr_);
  ring_read_ptr_ = ring_write_ptr_;
}

void GpuCommandProcessor::ProcessRingBuffer(uint32_t read_ptr, uint32_t write_ptr) {
  uint8_t* guest_base = xe::memory::GetGuestBase();
  if (!guest_base || ring_size_ == 0) return;

  uint32_t ptr = read_ptr;
  uint32_t safe_limit = ring_size_ * 2;  // prevent infinite loops on corrupted data
  uint32_t iterations = 0;

  while (ptr != write_ptr && iterations < safe_limit) {
    iterations++;
    uint32_t header;
    memcpy(&header, guest_base + ring_base_ + (ptr * 4), sizeof(uint32_t));
    header = __builtin_bswap32(header);

    uint32_t type = (header >> 30) & 0x3;

    switch (static_cast<PM4Type>(type)) {
      case PM4Type::kType0: {
        uint32_t reg_index = header & 0x7FFF;
        uint32_t count = ((header >> 16) & 0x3FFF) + 1;

        for (uint32_t i = 0; i < count && i < 128; ++i) {
          ptr = (ptr + 1) % ring_size_;
          uint32_t val;
          memcpy(&val, guest_base + ring_base_ + (ptr * 4), sizeof(uint32_t));
          regs_.Set(reg_index + i, __builtin_bswap32(val));
        }
        break;
      }

      case PM4Type::kType2:
        break;

      case PM4Type::kType3: {
        uint32_t opcode = (header >> 8) & 0xFF;
        uint32_t count = ((header >> 16) & 0x3FFF) + 1;

        uint32_t data[512];
        uint32_t safe_count = (count < 512) ? count : 512;
        for (uint32_t i = 0; i < safe_count; ++i) {
          ptr = (ptr + 1) % ring_size_;
          memcpy(&data[i], guest_base + ring_base_ + (ptr * 4), sizeof(uint32_t));
          data[i] = __builtin_bswap32(data[i]);
        }
        // Skip remaining if count > 512
        for (uint32_t i = safe_count; i < count; ++i) {
          ptr = (ptr + 1) % ring_size_;
        }

        switch (opcode) {
          case PM4Opcode::NOP:
          case PM4Opcode::SET_BIN_MASK:
          case PM4Opcode::SET_BIN_SELECT:
          case PM4Opcode::CONTEXT_UPDATE:
          case PM4Opcode::INVALIDATE_STATE:
          case PM4Opcode::WAIT_FOR_IDLE:
            break;

          case PM4Opcode::SET_CONSTANT:
          case PM4Opcode::SET_CONSTANT_2:
            HandleSetConstant(data, safe_count);
            break;

          case PM4Opcode::LOAD_ALU_CONSTANT:
            // data[0] = source address, data[1] = start offset, data[2] = count
            if (safe_count >= 3 && guest_base) {
              uint32_t src = data[0] & 0x1FFFFFFC;
              uint32_t dst_off = data[1];
              uint32_t cnt = data[2] + 1;
              for (uint32_t i = 0; i < cnt; ++i) {
                uint32_t v;
                memcpy(&v, guest_base + src + i * 4, 4);
                regs_.Set(dst_off + i, __builtin_bswap32(v));
              }
            }
            break;

          case PM4Opcode::DRAW_INDX:
          case PM4Opcode::DRAW_INDX_2:
            HandleDraw(opcode, data, safe_count);
            break;

          case PM4Opcode::EVENT_WRITE:
          case PM4Opcode::EVENT_WRITE_SHD:
            HandleEventWrite(data, safe_count);
            break;

          case PM4Opcode::INTERRUPT:
            if (interrupt_callback_) {
              interrupt_callback_(data[0]);
            }
            break;

          case PM4Opcode::MEM_WRITE:
            HandleMemWrite(data, safe_count);
            break;

          case PM4Opcode::REG_TO_MEM:
            HandleRegToMem(data, safe_count);
            break;

          case PM4Opcode::WAIT_REG_MEM:
            HandleWaitRegMem(data, safe_count);
            break;

          case PM4Opcode::INDIRECT_BUFFER:
            HandleIndirectBuffer(data, safe_count);
            break;

          case PM4Opcode::REG_RMW: {
            if (safe_count >= 3) {
              uint32_t reg = data[0] & 0x7FFF;
              uint32_t old_val = regs_.Get(reg);
              uint32_t new_val = (old_val & data[1]) | data[2];
              regs_.Set(reg, new_val);
            }
            break;
          }

          case PM4Opcode::IM_LOAD:
          case PM4Opcode::IM_LOAD_IMMEDIATE:
            // Shader microcode upload — store address in register for later
            if (safe_count >= 2) {
              uint32_t shader_type = data[0] & 0x3;
              uint32_t addr = data[1] & 0x1FFFFFFC;
              // Store for shader translation: type 0 = vertex, 1 = pixel
              if (shader_type == 0) {
                regs_.Set(0x5F00, addr);  // Custom: VS microcode address
              } else {
                regs_.Set(0x5F01, addr);  // Custom: PS microcode address
              }
            }
            break;

          default:
            break;
        }
        break;
      }

      default:
        break;
    }

    ptr = (ptr + 1) % ring_size_;
  }
}

void GpuCommandProcessor::HandleSetConstant(const uint32_t* data, uint32_t count) {
  if (count < 2) return;
  uint32_t offset = data[0];
  // SET_CONSTANT uses type field in bits 16-17 of offset word
  uint32_t const_offset = offset & 0xFFFF;
  for (uint32_t i = 1; i < count; ++i) {
    regs_.Set(const_offset + i - 1, data[i]);
  }
}

void GpuCommandProcessor::HandleDraw(uint32_t opcode, const uint32_t* data, uint32_t count) {
  uint32_t prim_type = regs_.Get(reg::VGT_PRIMITIVE_TYPE);
  uint32_t num_indices = regs_.Get(reg::VGT_NUM_INDICES);
  uint32_t index_type = regs_.Get(reg::VGT_INDEX_TYPE);

  if (num_indices == 0) return;  // Skip empty draws

  // Snapshot current register state into a DrawCall
  DrawCall dc{};
  dc.prim_type = prim_type;
  dc.num_indices = num_indices;
  dc.index_type = index_type;
  dc.vgt_draw_initiator = regs_.Get(reg::VGT_DRAW_INITIATOR);

  // DRAW_INDX_2 has inline index data — record base address from packet
  if (opcode == PM4Opcode::DRAW_INDX_2 && count >= 1) {
    dc.vgt_draw_initiator = data[0];
    dc.num_indices = (data[0] >> 16) & 0xFFFF;
    dc.prim_type = data[0] & 0x3F;
  }

  // Vertex fetch info (from fetch constant 0 = registers 0x4800+)
  // SQ_TEX_RESOURCE_0 layout: base address in first 6 DWORDs of fetch const
  uint32_t vf0 = regs_.Get(0x4800);  // Vertex fetch constant 0, word 0
  uint32_t vf1 = regs_.Get(0x4801);  // word 1 (size)
  dc.vertex_base_addr = (vf0 & 0x1FFFFFFC);
  dc.vertex_stride = (vf1 >> 16) & 0xFF;  // Simplified

  // Index buffer from DRAW_INDX data[1] if present
  if (opcode == PM4Opcode::DRAW_INDX && count >= 2) {
    dc.index_base_addr = data[1] & 0x1FFFFFFC;
    dc.index_size = (data[0] >> 11) & 0x1;  // 0=16bit, 1=32bit
  }

  // Render state snapshot
  dc.rb_colorcontrol = regs_.Get(reg::RB_COLOR_MASK);
  dc.rb_blendcontrol = regs_.Get(reg::RB_BLENDCONTROL_0);
  dc.rb_depthcontrol = regs_.Get(reg::RB_DEPTHCONTROL);
  dc.pa_su_sc_mode = regs_.Get(reg::PA_SU_SC_MODE_CNTL);
  dc.sq_program_cntl = regs_.Get(reg::SQ_PROGRAM_CNTL);
  dc.rb_surface_info = regs_.Get(reg::RB_SURFACE_INFO);
  dc.rb_color_info = regs_.Get(reg::RB_COLOR_INFO);
  dc.rb_depth_info = regs_.Get(reg::RB_DEPTH_INFO);

  draw_calls_.push_back(dc);
  total_draw_calls_++;

  XELOGD("Draw #{}: prim={} indices={} vb=0x{:08X} stride={}",
         total_draw_calls_, prim_type, num_indices,
         dc.vertex_base_addr, dc.vertex_stride);
}

void GpuCommandProcessor::HandleEventWrite(const uint32_t* data, uint32_t count) {
  if (count < 1) return;
  uint32_t event_type = data[0] & 0xFF;

  // EVENT_WRITE_SHD writes a value to guest memory at a given address
  if (count >= 3) {
    uint32_t addr = data[1] & 0x1FFFFFFC;
    uint32_t value = data[2];
    uint8_t* guest_base = xe::memory::GetGuestBase();
    if (guest_base && addr > 0) {
      uint32_t be_val = __builtin_bswap32(value);
      memcpy(guest_base + addr, &be_val, 4);
    }
  }

  XELOGD("GPU event: type={}", event_type);
}

void GpuCommandProcessor::HandleMemWrite(const uint32_t* data, uint32_t count) {
  if (count < 3) return;
  uint32_t addr = data[0] & 0x1FFFFFFC;
  // MEM_WRITE writes two DWORDs to guest memory
  uint8_t* guest_base = xe::memory::GetGuestBase();
  if (!guest_base) return;
  uint32_t v0 = __builtin_bswap32(data[1]);
  uint32_t v1 = __builtin_bswap32(data[2]);
  memcpy(guest_base + addr, &v0, 4);
  memcpy(guest_base + addr + 4, &v1, 4);
}

void GpuCommandProcessor::HandleRegToMem(const uint32_t* data, uint32_t count) {
  if (count < 2) return;
  uint32_t reg = data[0] & 0x7FFF;
  uint32_t addr = data[1] & 0x1FFFFFFC;
  uint8_t* guest_base = xe::memory::GetGuestBase();
  if (!guest_base) return;
  uint32_t val = __builtin_bswap32(regs_.Get(reg));
  memcpy(guest_base + addr, &val, 4);
}

void GpuCommandProcessor::HandleWaitRegMem(const uint32_t* data, uint32_t count) {
  // WAIT_REG_MEM: poll register or memory until condition met
  // For now, just skip — in a real impl this would stall the CP
  if (count >= 4) {
    uint32_t engine = data[0] & 0x1;  // 0=register, 1=memory
    uint32_t reg_or_addr = data[1];
    uint32_t ref_val = data[2];
    uint32_t mask = data[3];
    (void)engine; (void)reg_or_addr; (void)ref_val; (void)mask;
  }
}

void GpuCommandProcessor::HandleIndirectBuffer(const uint32_t* data, uint32_t count) {
  if (count < 2) return;
  uint32_t ib_base = data[0] & 0x1FFFFFFC;
  uint32_t ib_size = data[1] & 0xFFFFF;  // In DWORDs

  // Save and restore ring state to process the indirect buffer
  uint32_t saved_base = ring_base_;
  uint32_t saved_size = ring_size_;
  ring_base_ = ib_base;
  ring_size_ = ib_size;
  ProcessRingBuffer(0, ib_size);
  ring_base_ = saved_base;
  ring_size_ = saved_size;
}

}  // namespace xe::gpu
