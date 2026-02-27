/**
 * Vera360 — Xenia Edge
 * GPU Command Processor implementation
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
  XELOGI("GPU Command Processor initialized");
  return true;
}

void GpuCommandProcessor::Shutdown() {
  vk_device_ = nullptr;
}

void GpuCommandProcessor::SetRingBuffer(uint32_t base_address, uint32_t size_dwords) {
  ring_base_ = base_address;
  ring_size_ = size_dwords;
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

void GpuCommandProcessor::ProcessRingBuffer(uint32_t read_ptr, uint32_t write_ptr) {
  uint8_t* guest_base = xe::memory::GetGuestBase();
  if (!guest_base) return;

  uint32_t ptr = read_ptr;
  
  while (ptr != write_ptr) {
    uint32_t header;
    memcpy(&header, guest_base + ring_base_ + (ptr * 4), sizeof(uint32_t));
    header = __builtin_bswap32(header);  // Big-endian

    uint32_t type = (header >> 30) & 0x3;

    switch (static_cast<PM4Type>(type)) {
      case PM4Type::kType0: {
        uint32_t reg_index = header & 0x7FFF;
        uint32_t count = ((header >> 16) & 0x3FFF) + 1;
        
        uint32_t data[64];
        for (uint32_t i = 0; i < count && i < 64; ++i) {
          ptr = (ptr + 1) % ring_size_;
          memcpy(&data[i], guest_base + ring_base_ + (ptr * 4), sizeof(uint32_t));
          data[i] = __builtin_bswap32(data[i]);
        }
        
        for (uint32_t i = 0; i < count && i < 64; ++i) {
          regs_.Set(reg_index + i, data[i]);
        }
        break;
      }

      case PM4Type::kType2:
        // NOP — skip
        break;

      case PM4Type::kType3: {
        uint32_t opcode = (header >> 8) & 0xFF;
        uint32_t count = ((header >> 16) & 0x3FFF) + 1;
        
        uint32_t data[256];
        for (uint32_t i = 0; i < count && i < 256; ++i) {
          ptr = (ptr + 1) % ring_size_;
          memcpy(&data[i], guest_base + ring_base_ + (ptr * 4), sizeof(uint32_t));
          data[i] = __builtin_bswap32(data[i]);
        }

        switch (opcode) {
          case PM4Opcode::NOP:
            break;

          case PM4Opcode::SET_CONSTANT:
            HandleSetConstant(data, count);
            break;

          case PM4Opcode::DRAW_INDX:
          case PM4Opcode::DRAW_INDX_2:
            HandleDraw(opcode, data, count);
            break;

          case PM4Opcode::EVENT_WRITE:
          case PM4Opcode::EVENT_WRITE_SHD:
            HandleEventWrite(data, count);
            break;

          case PM4Opcode::INTERRUPT:
            if (interrupt_callback_) {
              interrupt_callback_(data[0]);
            }
            break;

          default:
            XELOGD("Unhandled PM4 opcode: 0x{:02X}", opcode);
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
  for (uint32_t i = 1; i < count; ++i) {
    regs_.Set(offset + i - 1, data[i]);
  }
}

void GpuCommandProcessor::HandleDraw(uint32_t opcode, const uint32_t* data, uint32_t count) {
  // TODO: Translate Xenos draw calls to Vulkan draw commands
  uint32_t prim_type = regs_.Get(reg::VGT_PRIMITIVE_TYPE);
  uint32_t num_indices = regs_.Get(reg::VGT_NUM_INDICES);
  
  XELOGD("Draw: prim={}, indices={}", prim_type, num_indices);
  
  // This is where we'd:
  // 1. Bind current shader programs
  // 2. Set up Vulkan pipeline state
  // 3. Bind vertex/index buffers
  // 4. vkCmdDraw or vkCmdDrawIndexed
}

void GpuCommandProcessor::HandleEventWrite(const uint32_t* data, uint32_t count) {
  if (count < 1) return;
  uint32_t event_type = data[0] & 0xFF;
  XELOGD("GPU event: type={}", event_type);
}

}  // namespace xe::gpu
