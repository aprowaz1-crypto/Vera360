/**
 * Vera360 — Xenia Edge
 * Xenos GPU Register Definitions
 *
 * The Xbox 360 GPU (Xenos/C1) has ~0x6000 registers mapped to MMIO.
 * This file defines the critical registers for GPU command processing.
 */
#pragma once

#include <cstdint>

namespace xe::gpu {

/// Xenos register file: 0x6000 32-bit registers
constexpr uint32_t kXenosRegisterCount = 0x6000;

/// Key register offsets
namespace reg {
  constexpr uint32_t COHER_STATUS_HOST      = 0x07FC;
  constexpr uint32_t COHER_BASE_HOST        = 0x07F8;
  constexpr uint32_t COHER_SIZE_HOST        = 0x07F4;
  
  constexpr uint32_t WAIT_UNTIL             = 0x0005;
  
  // Primitive setup
  constexpr uint32_t VGT_DRAW_INITIATOR     = 0x21FC;
  constexpr uint32_t VGT_EVENT_INITIATOR    = 0x21F8;
  constexpr uint32_t VGT_NUM_INDICES        = 0x2228;
  constexpr uint32_t VGT_PRIMITIVE_TYPE     = 0x2256;
  constexpr uint32_t VGT_INDEX_TYPE         = 0x2257;
  
  // Render state
  constexpr uint32_t PA_SC_WINDOW_OFFSET    = 0x2080;
  constexpr uint32_t PA_SC_WINDOW_SCISSOR_TL= 0x2081;
  constexpr uint32_t PA_SC_WINDOW_SCISSOR_BR= 0x2082;
  constexpr uint32_t PA_CL_VTE_CNTL        = 0x2206;
  constexpr uint32_t PA_SU_SC_MODE_CNTL    = 0x2205;
  
  // Render backend
  constexpr uint32_t RB_MODECONTROL        = 0x2210;
  constexpr uint32_t RB_SURFACE_INFO       = 0x2211;
  constexpr uint32_t RB_COLOR_INFO         = 0x2212;
  constexpr uint32_t RB_DEPTH_INFO         = 0x2213;
  constexpr uint32_t RB_COLOR_MASK         = 0x2214;
  constexpr uint32_t RB_BLENDCONTROL_0     = 0x2215;
  constexpr uint32_t RB_DEPTHCONTROL       = 0x2200;
  constexpr uint32_t RB_STENCILREFMASK     = 0x2201;
  
  // Shader constants
  constexpr uint32_t SQ_VS_CONST           = 0x4000;  // 256 float4 vertex shader constants
  constexpr uint32_t SQ_PS_CONST           = 0x4400;  // 256 float4 pixel shader constants
  constexpr uint32_t SQ_BOOL_CONST         = 0x4900;
  constexpr uint32_t SQ_LOOP_CONST         = 0x4908;
  
  // Texture fetch
  constexpr uint32_t SQ_TEX_RESOURCE_0     = 0x4800;
  constexpr uint32_t SQ_TEX_SAMPLER_0      = 0x4880;
  
  // Shader programs
  constexpr uint32_t SQ_PROGRAM_CNTL       = 0x2180;
  constexpr uint32_t SQ_CONTEXT_MISC       = 0x2181;

  // Vertex fetch constants (VF0-VF95) — 6 DWORDs each
  constexpr uint32_t SQ_VTX_CONSTANT_0     = 0x4800;  // Overlaps with TEX

  // Custom registers used by our command processor
  constexpr uint32_t VS_MICROCODE_ADDR     = 0x5F00;
  constexpr uint32_t PS_MICROCODE_ADDR     = 0x5F01;

  // Viewport
  constexpr uint32_t PA_CL_VPORT_XSCALE   = 0x2110;
  constexpr uint32_t PA_CL_VPORT_XOFFSET  = 0x2111;
  constexpr uint32_t PA_CL_VPORT_YSCALE   = 0x2112;
  constexpr uint32_t PA_CL_VPORT_YOFFSET  = 0x2113;
  constexpr uint32_t PA_CL_VPORT_ZSCALE   = 0x2114;
  constexpr uint32_t PA_CL_VPORT_ZOFFSET  = 0x2115;
}

/// Xenos primitive types
enum class PrimitiveType : uint32_t {
  kNone          = 0x00,
  kPointList     = 0x01,
  kLineList      = 0x02,
  kLineStrip     = 0x03,
  kTriangleList  = 0x04,
  kTriangleFan   = 0x05,
  kTriangleStrip = 0x06,
  kRectangleList = 0x08,
  kQuadList      = 0x0D,
};

/// Global register state
struct XenosRegisters {
  uint32_t values[kXenosRegisterCount] = {};
  
  uint32_t Get(uint32_t index) const {
    return (index < kXenosRegisterCount) ? values[index] : 0;
  }
  
  void Set(uint32_t index, uint32_t value) {
    if (index < kXenosRegisterCount) values[index] = value;
  }
};

}  // namespace xe::gpu
