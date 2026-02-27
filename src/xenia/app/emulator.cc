/**
 * Vera360 — Xenia Edge
 * Emulator implementation — top-level orchestrator
 *
 * Owns and initialises every subsystem, loads XEX executables,
 * and runs the main emulation loop with Vulkan rendering + PPC interpretation.
 */

#include "xenia/app/emulator.h"
#include "xenia/base/logging.h"
#include "xenia/base/memory/memory.h"
#include "xenia/base/platform_android.h"
#include "xenia/base/clock.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/frontend/ppc_interpreter.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/xmodule.h"
#include "xenia/kernel/xthread.h"
#include "xenia/kernel/xex2_loader.h"
#include "xenia/gpu/gpu_command_processor.h"
#include "xenia/gpu/vulkan/vulkan_instance.h"
#include "xenia/gpu/vulkan/vulkan_device.h"
#include "xenia/gpu/vulkan/vulkan_swap_chain.h"

// Forward-declare subsystem init/shutdown:
namespace xe::kernel::xboxkrnl {
  void RegisterAllExports();
  bool Dispatch(uint32_t ordinal, uint32_t* args);
}
namespace xe::kernel::xam {
  void RegisterAllExports();
  bool Dispatch(uint32_t ordinal, uint32_t* args);
}
namespace xe::hid {
  bool Initialize();
  void Shutdown();
}

#include <android/native_window.h>
#include <fstream>
#include <cstring>
#include <algorithm>

namespace xe {

Emulator::Emulator() = default;
Emulator::~Emulator() { Shutdown(); }

bool Emulator::Initialize(ANativeWindow* window, const std::string& storage_root) {
  storage_root_ = storage_root;
  native_window_ = window;
  XELOGI("=== Vera360 / Xenia Edge ===");
  XELOGI("Initialising emulator... storage={}", storage_root);

  if (!InitMemory()) return false;
  if (!InitGraphics(window)) return false;
  if (!InitCpu()) return false;
  if (!InitKernel()) return false;
  if (!InitApu()) return false;
  if (!InitHid()) return false;

  // Wire GPU MMIO intercept: PPC writes to GPU register range get forwarded
  if (processor_ && gpu_command_processor_) {
    auto* interp = processor_->GetInterpreter();
    if (interp) {
      auto* gpu = gpu_command_processor_.get();
      interp->SetMmioHandlers(
        [gpu](uint32_t addr, uint32_t value) -> bool {
          return gpu->HandleMmioWrite(addr, value);
        },
        [gpu](uint32_t addr) -> uint32_t {
          return gpu->HandleMmioRead(addr);
        }
      );
      XELOGI("GPU MMIO intercept wired to PPC interpreter");
    }
  }

  running_ = true;
  XELOGI("Emulator initialised OK");
  return true;
}

void Emulator::Shutdown() {
  if (!running_ && !processor_ && !kernel_state_) return;
  running_ = false;

  XELOGI("Shutting down emulator...");
  xe::hid::Shutdown();

  // Clean up Vulkan rendering resources
  if (vulkan_device_) {
    VkDevice dev = vulkan_device_->GetHandle();
    if (dev) vkDeviceWaitIdle(dev);
    if (vk_cmd_pool_) { vkDestroyCommandPool(dev, vk_cmd_pool_, nullptr); vk_cmd_pool_ = VK_NULL_HANDLE; }
    if (passthrough_vs_) { vkDestroyShaderModule(dev, passthrough_vs_, nullptr); passthrough_vs_ = VK_NULL_HANDLE; }
    if (passthrough_ps_) { vkDestroyShaderModule(dev, passthrough_ps_, nullptr); passthrough_ps_ = VK_NULL_HANDLE; }
    if (vk_pipeline_layout_) { vkDestroyPipelineLayout(dev, vk_pipeline_layout_, nullptr); vk_pipeline_layout_ = VK_NULL_HANDLE; }
    if (vk_draw_pipeline_) { vkDestroyPipeline(dev, vk_draw_pipeline_, nullptr); vk_draw_pipeline_ = VK_NULL_HANDLE; }
    if (vk_clear_pipeline_) { vkDestroyPipeline(dev, vk_clear_pipeline_, nullptr); vk_clear_pipeline_ = VK_NULL_HANDLE; }
    if (vk_desc_set_layout_) { vkDestroyDescriptorSetLayout(dev, vk_desc_set_layout_, nullptr); vk_desc_set_layout_ = VK_NULL_HANDLE; }
    if (vk_staging_vb_) { vkDestroyBuffer(dev, vk_staging_vb_, nullptr); vk_staging_vb_ = VK_NULL_HANDLE; }
    if (vk_staging_vb_mem_) { vkFreeMemory(dev, vk_staging_vb_mem_, nullptr); vk_staging_vb_mem_ = VK_NULL_HANDLE; }
    if (vk_staging_ib_) { vkDestroyBuffer(dev, vk_staging_ib_, nullptr); vk_staging_ib_ = VK_NULL_HANDLE; }
    if (vk_staging_ib_mem_) { vkFreeMemory(dev, vk_staging_ib_mem_, nullptr); vk_staging_ib_mem_ = VK_NULL_HANDLE; }
  }

  gpu_command_processor_.reset();
  vulkan_swap_chain_.reset();
  vulkan_device_.reset();
  vulkan_instance_.reset();

  processor_.reset();

  if (kernel_state_) {
    kernel::KernelState::SetShared(nullptr);
    delete kernel_state_;
    kernel_state_ = nullptr;
  }

  xe::memory::Shutdown();
  XELOGI("Emulator shut down");
}

bool Emulator::InitMemory() {
  if (!xe::memory::Initialize()) {
    XELOGE("Failed to initialise guest memory");
    return false;
  }
  XELOGI("Guest memory mapped at {}", static_cast<void*>(xe::memory::GetGuestBase()));
  return true;
}

bool Emulator::InitGraphics(ANativeWindow* window) {
  if (!window) {
    XELOGW("No ANativeWindow supplied — headless mode");
    return true;
  }
  surface_width_ = ANativeWindow_getWidth(window);
  surface_height_ = ANativeWindow_getHeight(window);
  XELOGI("Graphics init: {}x{}", surface_width_, surface_height_);

  // Vulkan Instance
  vulkan_instance_ = std::make_unique<gpu::vulkan::VulkanInstance>();
  if (!vulkan_instance_->Initialize()) {
    XELOGW("VulkanInstance init failed — continuing without GPU");
    vulkan_instance_.reset();
    return true;
  }

  // Vulkan Device
  vulkan_device_ = std::make_unique<gpu::vulkan::VulkanDevice>();
  if (!vulkan_device_->Initialize(*vulkan_instance_)) {
    XELOGW("VulkanDevice init failed — continuing without GPU");
    vulkan_device_.reset();
    return true;
  }

  // Vulkan Swap Chain
  vulkan_swap_chain_ = std::make_unique<gpu::vulkan::VulkanSwapChain>();
  if (!vulkan_swap_chain_->Initialize(*vulkan_instance_, *vulkan_device_, window)) {
    XELOGW("VulkanSwapChain init failed — continuing without GPU");
    vulkan_swap_chain_.reset();
    return true;
  }

  // GPU command processor
  gpu_command_processor_ = std::make_unique<gpu::GpuCommandProcessor>();
  if (vulkan_device_) {
    gpu_command_processor_->Initialize(vulkan_device_.get());
  }

  // Create Vulkan rendering resources
  if (vulkan_device_ && vulkan_swap_chain_) {
    InitGpuRenderer();
  }

  XELOGI("Vulkan graphics pipeline initialised");
  return true;
}

bool Emulator::InitCpu() {
  XELOGI("CPU subsystem init (PPC interpreter + ARM64 JIT)");

  processor_ = std::make_unique<cpu::Processor>();
  uint8_t* guest_base = xe::memory::GetGuestBase();

  if (!processor_->Initialize(guest_base, cpu::ExecMode::kInterpreter)) {
    XELOGE("Failed to initialise CPU processor");
    return false;
  }

  return true;
}

bool Emulator::InitKernel() {
  XELOGI("Kernel subsystem init");
  kernel_state_ = new kernel::KernelState();
  kernel::KernelState::SetShared(kernel_state_);

  xe::kernel::xboxkrnl::RegisterAllExports();
  xe::kernel::xam::RegisterAllExports();

  // Wire kernel dispatch into the CPU processor
  if (processor_) {
    processor_->SetKernelDispatch(
      [this](cpu::ThreadState* ts, uint32_t ordinal) {
        // Build args array from r3-r10 (PPC calling convention)
        uint32_t args[8];
        for (int i = 0; i < 8; ++i) {
          args[i] = static_cast<uint32_t>(ts->gpr[3 + i]);
        }

        bool handled = false;
        if (ordinal & 0x10000) {
          // XAM export (ordinal high bit set by thunk patching)
          handled = xe::kernel::xam::Dispatch(ordinal & 0xFFFF, args);
        } else {
          // xboxkrnl export
          handled = xe::kernel::xboxkrnl::Dispatch(ordinal, args);
        }

        if (!handled) {
          XELOGW("Unhandled kernel call: ordinal={} (0x{:04X})", ordinal, ordinal);
        }

        // Return value goes in r3
        ts->gpr[3] = args[0];
      });
  }

  XELOGI("Kernel state initialized, handle base=0x{:08X}",
         kernel_state_->AllocateHandle());
  return true;
}

bool Emulator::InitApu() {
  XELOGI("APU subsystem init");
  return true;
}

bool Emulator::InitHid() {
  return xe::hid::Initialize();
}

bool Emulator::InitGpuRenderer() {
  VkDevice device = vulkan_device_->GetHandle();

  // Create command pool + buffer for frame rendering
  VkCommandPoolCreateInfo pool_ci{};
  pool_ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pool_ci.queueFamilyIndex = vulkan_device_->GetGraphicsFamily();
  pool_ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  if (vkCreateCommandPool(device, &pool_ci, nullptr, &vk_cmd_pool_) != VK_SUCCESS) {
    XELOGW("Failed to create frame command pool");
    return false;
  }

  VkCommandBufferAllocateInfo alloc_ci{};
  alloc_ci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  alloc_ci.commandPool = vk_cmd_pool_;
  alloc_ci.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  alloc_ci.commandBufferCount = 1;
  vkAllocateCommandBuffers(device, &alloc_ci, &vk_cmd_buffer_);

  // ── Create staging vertex buffer (host-visible, 4MB) ──────────────────
  auto createBuffer = [&](VkDeviceSize size, VkBufferUsageFlags usage,
                          VkBuffer& buf, VkDeviceMemory& mem) -> bool {
    VkBufferCreateInfo buf_ci{};
    buf_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buf_ci.size = size;
    buf_ci.usage = usage;
    buf_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &buf_ci, nullptr, &buf) != VK_SUCCESS)
      return false;
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device, buf, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = vulkan_device_->FindMemoryType(
        req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(device, &ai, nullptr, &mem) != VK_SUCCESS)
      return false;
    vkBindBufferMemory(device, buf, mem, 0);
    return true;
  };

  createBuffer(kStagingBufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
               vk_staging_vb_, vk_staging_vb_mem_);
  createBuffer(kStagingBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
               vk_staging_ib_, vk_staging_ib_mem_);

  // ── Create pipeline layout (push constants only, no descriptors) ──────
  VkPushConstantRange push{};
  push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  push.offset = 0;
  push.size = 16;  // viewport scale/offset

  VkPipelineLayoutCreateInfo layout_ci{};
  layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layout_ci.pushConstantRangeCount = 1;
  layout_ci.pPushConstantRanges = &push;
  vkCreatePipelineLayout(device, &layout_ci, nullptr, &vk_pipeline_layout_);

  // ── Compile built-in passthrough shaders (inline GLSL→SPIR-V) ────────
  // Minimal vertex shader: read position as vec4 from vertex buffer,
  // apply viewport transform via push constants, output to gl_Position.
  // Minimal fragment shader: output solid white.
  //
  // For now we use pre-compiled SPIR-V blobs (hand-assembled minimal shaders).
  // Vertex: input vec4 position at location 0 → gl_Position
  static const uint32_t vs_spirv[] = {
    0x07230203, 0x00010000, 0x00080001, 0x0000001A, 0x00000000, // header
    0x00020011, 0x00000001,  // OpCapability Shader
    0x0006000B, 0x00000001, 0x4C534C47, 0x6474732E, 0x0030352E, // OpExtInstImport "GLSL.std.450"
    0x0003000E, 0x00000000, 0x00000001, // OpMemoryModel Logical GLSL450
    0x0007000F, 0x00000000, 0x00000002, 0x6E69616D, 0x00000000, 0x00000003, 0x00000004, // OpEntryPoint Vertex %main "main" %in_pos %gl_pos
    // Types
    0x00020013, 0x00000005, // OpTypeVoid
    0x00030021, 0x00000006, 0x00000005, // OpTypeFunction %void
    0x00030016, 0x00000007, 0x00000020, // OpTypeFloat 32
    0x00040017, 0x00000008, 0x00000007, 0x00000004, // OpTypeVector %float 4
    // Input: in_pos at location 0
    0x00040020, 0x00000009, 0x00000001, 0x00000008, // OpTypePointer Input %vec4
    0x0004003B, 0x00000009, 0x00000003, 0x00000001, // OpVariable %in_pos Input
    // Output: gl_Position (BuiltIn)
    0x00040020, 0x0000000A, 0x00000003, 0x00000008, // OpTypePointer Output %vec4
    0x0004003B, 0x0000000A, 0x00000004, 0x00000003, // OpVariable %gl_pos Output
    // Decorations
    0x00040047, 0x00000003, 0x0000001E, 0x00000000, // OpDecorate %in_pos Location 0
    0x00040047, 0x00000004, 0x0000000B, 0x00000000, // OpDecorate %gl_pos BuiltIn Position
    // main()
    0x00050036, 0x00000005, 0x00000002, 0x00000000, 0x00000006, // OpFunction
    0x000200F8, 0x0000000B, // OpLabel
    0x0004003D, 0x00000008, 0x0000000C, 0x00000003, // OpLoad %vec4 %in_pos
    0x0003003E, 0x00000004, 0x0000000C, // OpStore %gl_pos %loaded
    0x000100FD, // OpReturn
    0x00010038, // OpFunctionEnd
  };

  // Fragment: output solid white (1,1,1,1)
  static const uint32_t ps_spirv[] = {
    0x07230203, 0x00010000, 0x00080001, 0x00000015, 0x00000000,
    0x00020011, 0x00000001,
    0x0006000B, 0x00000001, 0x4C534C47, 0x6474732E, 0x0030352E,
    0x0003000E, 0x00000000, 0x00000001,
    0x0006000F, 0x00000004, 0x00000002, 0x6E69616D, 0x00000000, 0x00000003, // OpEntryPoint Fragment %main "main" %out_color
    0x00030010, 0x00000002, 0x00000007, // OpExecutionMode %main OriginUpperLeft
    // Types
    0x00020013, 0x00000004, // void
    0x00030021, 0x00000005, 0x00000004, // func type
    0x00030016, 0x00000006, 0x00000020, // float32
    0x00040017, 0x00000007, 0x00000006, 0x00000004, // vec4
    // Output
    0x00040020, 0x00000008, 0x00000003, 0x00000007, // ptr output vec4
    0x0004003B, 0x00000008, 0x00000003, 0x00000003, // %out_color output
    // Constants: 1.0f
    0x0004002B, 0x00000006, 0x00000009, 0x3F800000, // %c1 = 1.0
    0x0007002C, 0x00000007, 0x0000000A, 0x00000009, 0x00000009, 0x00000009, 0x00000009, // %white = vec4(1,1,1,1)
    // Decoration
    0x00040047, 0x00000003, 0x0000001E, 0x00000000, // Location 0
    // main()
    0x00050036, 0x00000004, 0x00000002, 0x00000000, 0x00000005,
    0x000200F8, 0x0000000B,
    0x0003003E, 0x00000003, 0x0000000A,
    0x000100FD,
    0x00010038,
  };

  auto createShaderModule = [&](const uint32_t* code, size_t size) -> VkShaderModule {
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = size;
    ci.pCode = code;
    VkShaderModule mod;
    if (vkCreateShaderModule(device, &ci, nullptr, &mod) != VK_SUCCESS)
      return VK_NULL_HANDLE;
    return mod;
  };

  passthrough_vs_ = createShaderModule(vs_spirv, sizeof(vs_spirv));
  passthrough_ps_ = createShaderModule(ps_spirv, sizeof(ps_spirv));

  if (!passthrough_vs_ || !passthrough_ps_) {
    XELOGW("Failed to create passthrough shader modules");
  }

  // ── Create graphics pipeline ──────────────────────────────────────────
  if (passthrough_vs_ && passthrough_ps_ && vk_pipeline_layout_) {
    VkRenderPass render_pass = vulkan_swap_chain_->GetRenderPass();

    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = passthrough_vs_;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = passthrough_ps_;
    stages[1].pName = "main";

    // Vertex input: single binding, position as float4 (16 bytes)
    VkVertexInputBindingDescription vb_desc{};
    vb_desc.binding = 0;
    vb_desc.stride = 16;  // sizeof(float) * 4
    vb_desc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attr{};
    attr.location = 0;
    attr.binding = 0;
    attr.format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attr.offset = 0;

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &vb_desc;
    vi.vertexAttributeDescriptionCount = 1;
    vi.pVertexAttributeDescriptions = &attr;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rast{};
    rast.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rast.polygonMode = VK_POLYGON_MODE_FILL;
    rast.cullMode = VK_CULL_MODE_NONE;
    rast.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rast.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = VK_FALSE;

    VkPipelineColorBlendAttachmentState blend_att{};
    blend_att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend_att.blendEnable = VK_TRUE;
    blend_att.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blend_att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blend_att.colorBlendOp = VK_BLEND_OP_ADD;
    blend_att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blend_att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blend_att.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blend_att;

    VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dyn_states;

    VkGraphicsPipelineCreateInfo pipe_ci{};
    pipe_ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipe_ci.stageCount = 2;
    pipe_ci.pStages = stages;
    pipe_ci.pVertexInputState = &vi;
    pipe_ci.pInputAssemblyState = &ia;
    pipe_ci.pViewportState = &vp;
    pipe_ci.pRasterizationState = &rast;
    pipe_ci.pMultisampleState = &ms;
    pipe_ci.pDepthStencilState = &ds;
    pipe_ci.pColorBlendState = &blend;
    pipe_ci.pDynamicState = &dyn;
    pipe_ci.layout = vk_pipeline_layout_;
    pipe_ci.renderPass = render_pass;
    pipe_ci.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipe_ci,
                                  nullptr, &vk_draw_pipeline_) == VK_SUCCESS) {
      XELOGI("Created draw pipeline for guest vertex rendering");
    } else {
      XELOGW("Failed to create draw pipeline");
    }
  }

  XELOGI("GPU renderer initialized (cmd pool + staging buffers + pipeline)");
  return true;
}

bool Emulator::LoadGame(const std::string& path) {
  XELOGI("Loading game: {}", path);

  // ── Step 1: Detect format ─────────────────────────────────────────────
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    XELOGE("Failed to open: {}", path);
    return false;
  }
  auto file_size = file.tellg();
  file.seekg(0);
  XELOGI("File size: {} bytes", static_cast<uint64_t>(file_size));

  // Read magic
  uint8_t magic[4] = {};
  file.read(reinterpret_cast<char*>(magic), 4);
  file.seekg(0);

  uint32_t magic32 = (uint32_t(magic[0]) << 24) | (uint32_t(magic[1]) << 16) |
                      (uint32_t(magic[2]) << 8) | magic[3];

  bool is_xex = (magic32 == 0x58455832 || magic32 == 0x58455831);
  bool is_stfs = (magic32 == 0x434F4E20 || magic32 == 0x4C495645 || magic32 == 0x50495253);
  bool is_iso = false;

  if (!is_xex && !is_stfs) {
    if (file_size > 0x10004) {
      file.seekg(0x10000);
      uint8_t iso_hdr[4];
      file.read(reinterpret_cast<char*>(iso_hdr), 4);
      if (iso_hdr[0] == 'M' && iso_hdr[1] == 'I' && iso_hdr[2] == 'C' && iso_hdr[3] == 'R') {
        is_iso = true;
      }
      file.seekg(0);
    }
  }

  if (is_xex) {
    return LoadXex(path, file, file_size);
  } else if (is_stfs) {
    XELOGI("STFS container detected — extracting default.xex...");
    return LoadStfsPackage(path);
  } else if (is_iso) {
    XELOGI("ISO disc image detected");
    return LoadDiscImage(path);
  } else {
    XELOGW("Unknown format (magic=0x{:08X}), trying as XEX", magic32);
    return LoadXex(path, file, file_size);
  }
}

bool Emulator::LoadXex(const std::string& path, std::ifstream& file,
                        std::streampos file_size) {
  xe::loader::Xex2Loader loader;
  file.close();

  if (!loader.Load(path)) {
    XELOGE("Failed to parse XEX2 header");
    return false;
  }

  const auto& module = loader.module();
  XELOGI("XEX2 loaded: entry=0x{:08X}, base=0x{:08X}, image_size=0x{:X}",
         module.entry_point, module.base_address, module.image_size);

  // Map into guest memory
  uint8_t* guest_base = xe::memory::GetGuestBase();
  if (guest_base && module.image_size > 0) {
    xe::memory::Commit(guest_base + module.base_address, module.image_size,
                       xe::memory::PageAccess::kExecuteReadWrite);

    if (!module.pe_image.empty()) {
      memcpy(guest_base + module.base_address,
             module.pe_image.data(),
             module.pe_image.size());
      XELOGI("Mapped {} bytes to guest 0x{:08X}",
             module.pe_image.size(), module.base_address);
    }
  }

  // Create kernel module object
  auto* xmod = kernel_state_->LoadModule(path);
  xmod->set_base_address(module.base_address);
  xmod->set_entry_point(module.entry_point);
  kernel_state_->SetExecutableModule(xmod);

  // Log imports
  for (auto& lib : module.import_libs) {
    XELOGI("Import library: {} ({} records)", lib.name, lib.records.size());
  }

  // Resolve imports and register thunks with the CPU processor
  loader.ResolveImports(guest_base, processor_.get());

  // ── Allocate stack for main thread ────────────────────────────────────
  constexpr uint32_t kDefaultStackSize = 256 * 1024;
  static uint32_t stack_alloc_ptr = 0x70000000;
  uint32_t stack_base = stack_alloc_ptr;
  stack_alloc_ptr += kDefaultStackSize;
  xe::memory::Commit(guest_base + stack_base, kDefaultStackSize,
                     xe::memory::PageAccess::kReadWrite);

  // Create CPU thread state
  auto* cpu_thread = processor_->CreateThreadState(1);
  cpu_thread->pc = module.entry_point;
  cpu_thread->gpr[1] = stack_base + kDefaultStackSize - 128;  // SP below top
  cpu_thread->gpr[13] = 0;  // Small data area (r13) — zero for now

  // Create kernel thread object
  auto* main_thread = kernel_state_->CreateThread(
      kDefaultStackSize, module.entry_point, 0, false);
  main_thread->set_name("XThread Main");
  main_thread->set_thread_id(1);

  XELOGI("Main thread created: entry=0x{:08X}, stack=0x{:08X}-0x{:08X}",
         module.entry_point, stack_base, stack_base + kDefaultStackSize);

  game_loaded_ = true;
  return true;
}

bool Emulator::LoadStfsPackage(const std::string& path) {
  // Open the STFS file and look for default.xex in the file listing
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) return false;

  auto fsize = file.tellg();
  file.seekg(0);

  // Read entire file into memory for parsing
  std::vector<uint8_t> data(static_cast<size_t>(fsize));
  file.read(reinterpret_cast<char*>(data.data()), fsize);
  file.close();

  // STFS structure:
  // 0x000: Magic (CON / LIVE / PIRS)
  // 0x340: Content metadata (title, description, etc.)
  // 0x379: Content type (4 bytes)
  // 0x37D: Metadata version
  // 0x3A0: Title ID
  // File table at volume descriptor offset

  if (data.size() < 0x1000) {
    XELOGE("STFS file too small");
    return false;
  }

  // Read volume descriptor type at offset 0x379
  uint8_t vol_desc_type = data[0x379];
  XELOGD("STFS volume descriptor type: {}", vol_desc_type);

  // Content type at 0x344 (4 bytes, big-endian)
  uint32_t content_type = (data[0x344] << 24) | (data[0x345] << 16) |
                           (data[0x346] << 8) | data[0x347];
  XELOGD("STFS content type: 0x{:08X}", content_type);

  // Title ID at 0x360 (4 bytes, big-endian)
  uint32_t title_id = (data[0x360] << 24) | (data[0x361] << 16) |
                       (data[0x362] << 8) | data[0x363];
  XELOGI("STFS Title ID: {:08X}", title_id);

  // Volume descriptor starts at 0x379
  // STFS: block size = 0x1000 (4KB)
  // Hash table at block 0, file table follows
  // For simplicity: scan for "default.xex" in the file table area

  const char* xex_name = "default.xex";
  size_t xex_name_len = strlen(xex_name);

  // STFS file table entries are at the beginning of data blocks
  // Each entry: 0x40 bytes: name (0x28), flags, block count (3 bytes LE),
  // etc. Scan from 0xC000 onwards for file table entries
  size_t scan_start = 0xB000;
  size_t scan_end = std::min(data.size(), static_cast<size_t>(0x20000));

  for (size_t off = scan_start; off + 0x40 <= scan_end; off += 0x40) {
    // Check for "default.xex" at the entry name field
    if (memcmp(data.data() + off, xex_name, xex_name_len) == 0 &&
        data[off + xex_name_len] == 0) {
      XELOGI("Found default.xex entry in STFS at offset 0x{:X}", off);

      // Read starting block (3 bytes LE at offset +0x2F)
      uint32_t start_block = data[off + 0x2F] |
                              (data[off + 0x30] << 8) |
                              (data[off + 0x31] << 16);
      uint32_t file_size_stfs = data[off + 0x34] |
                                (data[off + 0x35] << 8) |
                                (data[off + 0x36] << 16) |
                                (data[off + 0x37] << 24);

      XELOGD("STFS default.xex: start_block={}, size={}", start_block, file_size_stfs);

      // Extract the XEX data from STFS blocks
      // Block data offset = 0xC000 + block * 0x1000
      constexpr uint32_t kStfsDataStart = 0xC000;
      constexpr uint32_t kStfsBlockSize = 0x1000;

      std::vector<uint8_t> xex_data;
      xex_data.reserve(file_size_stfs);
      uint32_t current_block = start_block;
      uint32_t remaining = file_size_stfs;

      while (remaining > 0 && current_block != 0xFFFFFF) {
        uint32_t block_offset = kStfsDataStart + current_block * kStfsBlockSize;
        if (block_offset + kStfsBlockSize > data.size()) break;

        uint32_t to_copy = std::min(remaining, kStfsBlockSize);
        xex_data.insert(xex_data.end(),
                        data.data() + block_offset,
                        data.data() + block_offset + to_copy);
        remaining -= to_copy;
        current_block++;  // Simplified: assume sequential blocks
      }

      if (xex_data.size() >= 4) {
        // Save extracted XEX to temp file and load
        std::string temp_path = storage_root_ + "/temp_default.xex";
        std::ofstream out(temp_path, std::ios::binary);
        if (out.is_open()) {
          out.write(reinterpret_cast<const char*>(xex_data.data()), xex_data.size());
          out.close();
          XELOGI("Extracted default.xex ({} bytes)", xex_data.size());

          std::ifstream xex_file(temp_path, std::ios::binary | std::ios::ate);
          auto xex_size = xex_file.tellg();
          xex_file.seekg(0);
          return LoadXex(temp_path, xex_file, xex_size);
        }
      }
      break;
    }
  }

  XELOGW("Could not find default.xex in STFS package");
  return false;
}

bool Emulator::LoadDiscImage(const std::string& path) {
  // XISO (Xbox ISO) format:
  // Sector 32 (0x10000): "MICROSOFT*XBOX*MEDIA" magic
  // Root directory table follows the volume descriptor
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) return false;

  auto fsize = file.tellg();
  file.seekg(0);

  // Read the volume descriptor at sector 32
  constexpr uint32_t kSectorSize = 0x800;
  constexpr uint64_t kRootSector = 32;
  constexpr uint64_t kVolumeDescOffset = kRootSector * kSectorSize;

  if (static_cast<uint64_t>(fsize) < kVolumeDescOffset + 0x24) {
    XELOGE("ISO too small for XISO header");
    return false;
  }

  file.seekg(kVolumeDescOffset);
  uint8_t vol_hdr[0x24];
  file.read(reinterpret_cast<char*>(vol_hdr), 0x24);

  // Verify magic: "MICROSOFT*XBOX*MEDIA"
  if (memcmp(vol_hdr, "MICROSOFT*XBOX*MEDIA", 20) != 0) {
    XELOGE("Not a valid XISO image");
    return false;
  }

  // Root directory entry at offset 0x14 in the volume descriptor:
  // 4 bytes LE: root dir table sector
  // 4 bytes LE: root dir table size
  uint32_t root_sector = vol_hdr[0x14] | (vol_hdr[0x15] << 8) |
                          (vol_hdr[0x16] << 16) | (vol_hdr[0x17] << 24);
  uint32_t root_size = vol_hdr[0x18] | (vol_hdr[0x19] << 8) |
                        (vol_hdr[0x1A] << 16) | (vol_hdr[0x1B] << 24);

  XELOGD("XISO root dir: sector={}, size={}", root_sector, root_size);

  // Read root directory table
  uint64_t root_offset = static_cast<uint64_t>(root_sector) * kSectorSize;
  if (static_cast<uint64_t>(fsize) < root_offset + root_size) {
    XELOGE("ISO truncated at root directory");
    return false;
  }

  std::vector<uint8_t> root_data(root_size);
  file.seekg(root_offset);
  file.read(reinterpret_cast<char*>(root_data.data()), root_size);

  // Scan directory entries for "default.xex"
  // Each entry: left_offset(2), right_offset(2), sector(4), size(4),
  //             attributes(1), name_len(1), name(variable)
  for (size_t off = 0; off + 14 <= root_data.size();) {
    uint16_t left = root_data[off] | (root_data[off + 1] << 8);
    uint16_t right = root_data[off + 2] | (root_data[off + 3] << 8);
    uint32_t sector = root_data[off + 4] | (root_data[off + 5] << 8) |
                      (root_data[off + 6] << 16) | (root_data[off + 7] << 24);
    uint32_t size = root_data[off + 8] | (root_data[off + 9] << 8) |
                    (root_data[off + 10] << 16) | (root_data[off + 11] << 24);
    uint8_t attrib = root_data[off + 12];
    uint8_t name_len = root_data[off + 13];

    if (name_len == 0 || off + 14 + name_len > root_data.size()) break;

    std::string name(reinterpret_cast<const char*>(root_data.data() + off + 14),
                      name_len);

    XELOGD("XISO entry: {} sector={} size={}", name, sector, size);

    if (name == "default.xex" && size > 0) {
      // Extract and load
      uint64_t xex_offset = static_cast<uint64_t>(sector) * kSectorSize;
      if (static_cast<uint64_t>(fsize) < xex_offset + size) {
        XELOGE("ISO truncated at default.xex data");
        return false;
      }

      std::vector<uint8_t> xex_data(size);
      file.seekg(xex_offset);
      file.read(reinterpret_cast<char*>(xex_data.data()), size);

      // Save to temp and load
      std::string temp_path = storage_root_ + "/temp_default.xex";
      std::ofstream out(temp_path, std::ios::binary);
      out.write(reinterpret_cast<const char*>(xex_data.data()), xex_data.size());
      out.close();

      XELOGI("Extracted default.xex from ISO ({} bytes)", size);
      std::ifstream xex_file(temp_path, std::ios::binary | std::ios::ate);
      auto xex_size = xex_file.tellg();
      xex_file.seekg(0);
      return LoadXex(temp_path, xex_file, xex_size);
    }

    // Move to next entry (aligned to 4 bytes)
    off += 14 + name_len;
    off = (off + 3) & ~3u;
  }

  XELOGW("Could not find default.xex in ISO image");
  file.close();
  return false;
}

void Emulator::Tick() {
  if (!running_ || !game_loaded_) return;

  frame_count_++;

  // ── Step 1: Check if all threads terminated ────────────────────────
  if (kernel_state_ && kernel_state_->GetActiveThreadCount() == 0 &&
      !kernel_state_->GetAllThreads().empty()) {
    auto* main = kernel_state_->GetAllThreads().front();
    if (main && main->is_terminated()) {
      XELOGI("All threads terminated (exit_code={})", main->exit_code());
      running_ = false;
      return;
    }
  }

  // ── Step 2: Execute PPC instructions (round-robin scheduler) ────────
  if (processor_ && kernel_state_) {
    const auto& threads = kernel_state_->GetAllThreads();
    size_t thread_count = threads.size();
    if (thread_count > 0) {
      // Give each runnable thread a time slice
      size_t start_idx = kernel_state_->current_thread_index();
      size_t active = kernel_state_->GetActiveThreadCount();
      uint32_t instructions_per_thread = active > 0
          ? kInstructionsPerTick / static_cast<uint32_t>(active) : 0;
      if (instructions_per_thread < 1000) instructions_per_thread = 1000;

      for (size_t i = 0; i < thread_count; ++i) {
        size_t idx = (start_idx + i) % thread_count;
        auto* thread = threads[idx];
        if (thread->is_terminated() || thread->is_suspended()) continue;

        kernel_state_->SetCurrentThread(thread);
        auto* cpu_thread = processor_->CreateThreadState(thread->thread_id());
        if (cpu_thread && cpu_thread->running) {
          processor_->ExecuteBounded(cpu_thread, cpu_thread->pc, instructions_per_thread);
        }
      }
      // Advance the round-robin start for next frame
      kernel_state_->set_current_thread_index((start_idx + 1) % thread_count);
    }
  }

  // ── Step 2: Process GPU command buffer ────────────────────────────────
  if (gpu_command_processor_) {
    gpu_command_processor_->ProcessPendingCommands();
  }

  // ── Step 3: Render frame via Vulkan ───────────────────────────────────
  if (vulkan_swap_chain_ && vulkan_device_ && vk_cmd_buffer_) {
    uint32_t image_index = 0;
    if (vulkan_swap_chain_->AcquireNextImage(&image_index)) {
      RenderFrame(image_index);
      vulkan_swap_chain_->Present(image_index);
    }
  }

  // Clear draw calls for next frame
  if (gpu_command_processor_) {
    gpu_command_processor_->ClearDrawCalls();
  }
}

void Emulator::RenderFrame(uint32_t image_index) {
  VkDevice device = vulkan_device_->GetHandle();
  VkRenderPass render_pass = vulkan_swap_chain_->GetRenderPass();
  VkFramebuffer framebuffer = vulkan_swap_chain_->GetFramebuffer(image_index);
  VkExtent2D extent = vulkan_swap_chain_->GetExtent();

  // Begin command buffer
  VkCommandBufferBeginInfo begin_ci{};
  begin_ci.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_ci.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkResetCommandBuffer(vk_cmd_buffer_, 0);
  vkBeginCommandBuffer(vk_cmd_buffer_, &begin_ci);

  // Clear color: black when GPU active, pulsing green when idle
  VkClearValue clear_color{};
  bool has_draws = gpu_command_processor_ &&
                   !gpu_command_processor_->GetDrawCalls().empty();
  if (has_draws) {
    clear_color.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
  } else {
    float pulse = static_cast<float>(frame_count_ % 120) / 120.0f;
    clear_color.color = {{0.0f, 0.05f + pulse * 0.1f, 0.0f, 1.0f}};
  }

  VkRenderPassBeginInfo rp_begin{};
  rp_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  rp_begin.renderPass = render_pass;
  rp_begin.framebuffer = framebuffer;
  rp_begin.renderArea.extent = extent;
  rp_begin.clearValueCount = 1;
  rp_begin.pClearValues = &clear_color;
  vkCmdBeginRenderPass(vk_cmd_buffer_, &rp_begin, VK_SUBPASS_CONTENTS_INLINE);

  VkViewport viewport{};
  viewport.width = static_cast<float>(extent.width);
  viewport.height = static_cast<float>(extent.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vkCmdSetViewport(vk_cmd_buffer_, 0, 1, &viewport);

  VkRect2D scissor{};
  scissor.extent = extent;
  vkCmdSetScissor(vk_cmd_buffer_, 0, 1, &scissor);

  // ── Render guest GPU draw calls ─────────────────────────────────────
  if (has_draws && vk_draw_pipeline_ && vk_staging_vb_) {
    uint8_t* guest_base = xe::memory::GetGuestBase();
    const auto& draw_calls = gpu_command_processor_->GetDrawCalls();

    // Map staging buffers
    uint8_t* vb_map = nullptr;
    uint8_t* ib_map = nullptr;
    vkMapMemory(device, vk_staging_vb_mem_, 0, kStagingBufferSize, 0,
                reinterpret_cast<void**>(&vb_map));
    vkMapMemory(device, vk_staging_ib_mem_, 0, kStagingBufferSize, 0,
                reinterpret_cast<void**>(&ib_map));

    uint32_t vb_offset = 0;
    uint32_t ib_offset = 0;

    vkCmdBindPipeline(vk_cmd_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      vk_draw_pipeline_);

    uint32_t draws_submitted = 0;

    for (const auto& dc : draw_calls) {
      if (dc.num_indices == 0 || dc.vertex_base_addr == 0) continue;

      uint32_t stride = dc.vertex_stride;
      if (stride == 0) stride = 16;  // Default to float4
      uint32_t vertex_count = dc.num_indices;
      uint32_t vertex_data_size = vertex_count * stride;

      // Bounds check
      if (vb_offset + vertex_data_size > kStagingBufferSize) break;

      // Copy vertex data from guest memory, byte-swapping float components
      if (guest_base && dc.vertex_base_addr < 0x100000000ULL) {
        const uint8_t* src = guest_base + dc.vertex_base_addr;
        uint8_t* dst = vb_map + vb_offset;

        // Byte-swap each 32-bit word (big-endian → little-endian)
        uint32_t words = vertex_data_size / 4;
        const uint32_t* src32 = reinterpret_cast<const uint32_t*>(src);
        uint32_t* dst32 = reinterpret_cast<uint32_t*>(dst);
        for (uint32_t w = 0; w < words; w++) {
          dst32[w] = __builtin_bswap32(src32[w]);
        }
        // Copy any remainder bytes
        uint32_t remainder = vertex_data_size % 4;
        if (remainder) {
          memcpy(dst + words * 4, src + words * 4, remainder);
        }
      }

      // If indexed, copy index data
      bool is_indexed = dc.index_base_addr != 0 && dc.index_size > 0;
      uint32_t index_data_size = 0;
      if (is_indexed) {
        uint32_t idx_elem_size = (dc.index_type == 2) ? 4 : 2;
        index_data_size = dc.num_indices * idx_elem_size;
        if (ib_offset + index_data_size <= kStagingBufferSize &&
            dc.index_base_addr < 0x100000000ULL && guest_base) {
          const uint8_t* isrc = guest_base + dc.index_base_addr;
          uint8_t* idst = ib_map + ib_offset;
          if (idx_elem_size == 4) {
            uint32_t cnt = dc.num_indices;
            const uint32_t* s32 = reinterpret_cast<const uint32_t*>(isrc);
            uint32_t* d32 = reinterpret_cast<uint32_t*>(idst);
            for (uint32_t i = 0; i < cnt; i++) d32[i] = __builtin_bswap32(s32[i]);
          } else {
            uint32_t cnt = dc.num_indices;
            const uint16_t* s16 = reinterpret_cast<const uint16_t*>(isrc);
            uint16_t* d16 = reinterpret_cast<uint16_t*>(idst);
            for (uint32_t i = 0; i < cnt; i++) d16[i] = __builtin_bswap16(s16[i]);
          }
        } else {
          is_indexed = false;
        }
      }

      // Bind vertex buffer at current offset
      VkDeviceSize vb_vk_offset = vb_offset;
      vkCmdBindVertexBuffers(vk_cmd_buffer_, 0, 1, &vk_staging_vb_, &vb_vk_offset);

      if (is_indexed) {
        VkIndexType vk_idx_type = (dc.index_type == 2)
            ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
        vkCmdBindIndexBuffer(vk_cmd_buffer_, vk_staging_ib_, ib_offset, vk_idx_type);
        vkCmdDrawIndexed(vk_cmd_buffer_, dc.num_indices, 1, 0, 0, 0);
        ib_offset += index_data_size;
      } else {
        vkCmdDraw(vk_cmd_buffer_, vertex_count, 1, 0, 0);
      }

      vb_offset += vertex_data_size;
      draws_submitted++;
    }

    vkUnmapMemory(device, vk_staging_vb_mem_);
    vkUnmapMemory(device, vk_staging_ib_mem_);

    if (draws_submitted > 0) {
      XELOGD("Frame {}: submitted {} draw calls ({} vertices)",
             frame_count_, draws_submitted, vb_offset / 16);
    }

    // Clear draw calls for next frame
    gpu_command_processor_->ClearDrawCalls();
  }

  // End render pass
  vkCmdEndRenderPass(vk_cmd_buffer_);
  vkEndCommandBuffer(vk_cmd_buffer_);

  // Submit
  VkSubmitInfo submit{};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &vk_cmd_buffer_;

  VkSemaphore wait_sems[] = {vulkan_swap_chain_->GetImageAvailableSemaphore()};
  VkPipelineStageFlags wait_stages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  VkSemaphore signal_sems[] = {vulkan_swap_chain_->GetRenderFinishedSemaphore()};
  submit.waitSemaphoreCount = 1;
  submit.pWaitSemaphores = wait_sems;
  submit.pWaitDstStageMask = wait_stages;
  submit.signalSemaphoreCount = 1;
  submit.pSignalSemaphores = signal_sems;

  vkQueueSubmit(vulkan_device_->GetGraphicsQueue(), 1, &submit,
                vulkan_swap_chain_->GetInFlightFence());
}

void Emulator::Pause() {
  running_ = false;
  xe::Clock::PauseGuest();
  XELOGI("Emulator paused (frame {})", frame_count_);
}

void Emulator::Resume() {
  running_ = true;
  xe::Clock::ResumeGuest();
  XELOGI("Emulator resumed");
}

void Emulator::OnSurfaceChanged(ANativeWindow* window, int width, int height) {
  native_window_ = window;
  surface_width_ = width;
  surface_height_ = height;
  XELOGI("Surface changed: {}x{}", width, height);

  if (vulkan_swap_chain_) {
    vulkan_swap_chain_->Recreate(static_cast<uint32_t>(width),
                                  static_cast<uint32_t>(height));
  }
}

void Emulator::OnSurfaceDestroyed() {
  XELOGI("Surface destroyed");
  native_window_ = nullptr;
  vulkan_swap_chain_.reset();
}

}  // namespace xe
