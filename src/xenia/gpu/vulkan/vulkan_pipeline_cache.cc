/**
 * Vera360 — Xenia Edge
 * Vulkan Pipeline Cache — manages VkPipeline objects for Xenos draw states
 *
 * Maps Xenos render state (shader pair, blend, depth, rasterizer state)
 * to VkPipeline objects, caching them for reuse across frames.
 */

#include "xenia/base/logging.h"
#include <vulkan/vulkan.h>
#include <vector>
#include <unordered_map>
#include <cstring>
#include <cstdint>

namespace xe::gpu::vulkan {

/// Hash key for pipeline state
struct PipelineStateKey {
  uint64_t vertex_shader_hash;
  uint64_t pixel_shader_hash;
  uint32_t primitive_type;
  uint32_t blend_control;
  uint32_t depth_control;
  uint32_t stencil_ref_mask;
  uint32_t cull_mode;
  uint32_t color_format;
  uint32_t depth_format;

  bool operator==(const PipelineStateKey& o) const {
    return memcmp(this, &o, sizeof(*this)) == 0;
  }
};

struct PipelineStateKeyHash {
  size_t operator()(const PipelineStateKey& k) const {
    // FNV-1a hash
    uint64_t h = 0xcbf29ce484222325ULL;
    const auto* p = reinterpret_cast<const uint8_t*>(&k);
    for (size_t i = 0; i < sizeof(k); ++i) {
      h ^= p[i];
      h *= 0x100000001b3ULL;
    }
    return static_cast<size_t>(h);
  }
};

class VulkanPipelineCache {
 public:
  bool Initialize(VkDevice device, VkRenderPass render_pass) {
    device_ = device;
    render_pass_ = render_pass;

    VkPipelineCacheCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    if (vkCreatePipelineCache(device_, &ci, nullptr, &vk_cache_) != VK_SUCCESS) {
      XELOGE("Failed to create Vulkan pipeline cache");
      return false;
    }

    CreateDefaultPipelineLayout();

    XELOGI("Vulkan pipeline cache initialized");
    return true;
  }

  void Shutdown() {
    for (auto& [key, pipeline] : cache_) {
      vkDestroyPipeline(device_, pipeline, nullptr);
    }
    cache_.clear();
    if (vk_cache_ != VK_NULL_HANDLE) {
      vkDestroyPipelineCache(device_, vk_cache_, nullptr);
      vk_cache_ = VK_NULL_HANDLE;
    }
    if (pipeline_layout_ != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
      pipeline_layout_ = VK_NULL_HANDLE;
    }
    for (auto dsl : desc_set_layouts_) {
      vkDestroyDescriptorSetLayout(device_, dsl, nullptr);
    }
    desc_set_layouts_.clear();
  }

  /// Get or create a VkPipeline for the given state key + compiled shaders
  VkPipeline GetPipeline(const PipelineStateKey& key,
                          VkShaderModule vs, VkShaderModule ps) {
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second;

    VkPipeline pipeline = CreatePipeline(key, vs, ps);
    if (pipeline != VK_NULL_HANDLE) {
      cache_[key] = pipeline;
    }
    return pipeline;
  }

  VkPipelineLayout layout() const { return pipeline_layout_; }

 private:
  void CreateDefaultPipelineLayout() {
    // Set 0: Vertex UBO (256 float4)
    // Set 1: Fragment sampler2D
    VkDescriptorSetLayoutBinding ubo_binding{};
    ubo_binding.binding = 0;
    ubo_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    ubo_binding.descriptorCount = 1;
    ubo_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo set0_ci{};
    set0_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set0_ci.bindingCount = 1;
    set0_ci.pBindings = &ubo_binding;

    VkDescriptorSetLayout set0;
    vkCreateDescriptorSetLayout(device_, &set0_ci, nullptr, &set0);
    desc_set_layouts_.push_back(set0);

    VkDescriptorSetLayoutBinding sampler_binding{};
    sampler_binding.binding = 0;
    sampler_binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sampler_binding.descriptorCount = 16;  // Up to 16 texture slots
    sampler_binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo set1_ci{};
    set1_ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set1_ci.bindingCount = 1;
    set1_ci.pBindings = &sampler_binding;

    VkDescriptorSetLayout set1;
    vkCreateDescriptorSetLayout(device_, &set1_ci, nullptr, &set1);
    desc_set_layouts_.push_back(set1);

    // Push constant range for per-draw data
    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    push.offset = 0;
    push.size = 64;  // 16 floats for model/view transform

    VkPipelineLayoutCreateInfo layout_ci{};
    layout_ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout_ci.setLayoutCount = static_cast<uint32_t>(desc_set_layouts_.size());
    layout_ci.pSetLayouts = desc_set_layouts_.data();
    layout_ci.pushConstantRangeCount = 1;
    layout_ci.pPushConstantRanges = &push;

    vkCreatePipelineLayout(device_, &layout_ci, nullptr, &pipeline_layout_);
  }

  VkPipeline CreatePipeline(const PipelineStateKey& key,
                             VkShaderModule vs, VkShaderModule ps) {
    // Shader stages
    VkPipelineShaderStageCreateInfo stages[2] = {};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = ps;
    stages[1].pName = "main";

    // Vertex input: position(vec4) + texcoord(vec2)
    VkVertexInputBindingDescription vb{};
    vb.binding = 0;
    vb.stride = 24;  // 4*4 + 2*4
    vb.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attrs[2] = {};
    attrs[0].location = 0;
    attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32B32A32_SFLOAT;
    attrs[0].offset = 0;
    attrs[1].location = 1;
    attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[1].offset = 16;

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &vb;
    vi.vertexAttributeDescriptionCount = 2;
    vi.pVertexAttributeDescriptions = attrs;

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = MapPrimitiveType(key.primitive_type);

    // Viewport/scissor (dynamic)
    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.scissorCount = 1;

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rast{};
    rast.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rast.polygonMode = VK_POLYGON_MODE_FILL;
    rast.cullMode = MapCullMode(key.cull_mode);
    rast.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rast.lineWidth = 1.0f;

    // MSAA
    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth/stencil
    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = (key.depth_control & 0x2) ? VK_TRUE : VK_FALSE;
    ds.depthWriteEnable = (key.depth_control & 0x4) ? VK_TRUE : VK_FALSE;
    ds.depthCompareOp = MapCompareOp((key.depth_control >> 4) & 0x7);

    // Color blend
    VkPipelineColorBlendAttachmentState blend_att{};
    blend_att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend_att.blendEnable = (key.blend_control != 0) ? VK_TRUE : VK_FALSE;
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

    // Dynamic state
    VkDynamicState dyn_states[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dyn_states;

    // Pipeline creation
    VkGraphicsPipelineCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    ci.stageCount = 2;
    ci.pStages = stages;
    ci.pVertexInputState = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vp;
    ci.pRasterizationState = &rast;
    ci.pMultisampleState = &ms;
    ci.pDepthStencilState = &ds;
    ci.pColorBlendState = &blend;
    ci.pDynamicState = &dyn;
    ci.layout = pipeline_layout_;
    ci.renderPass = render_pass_;
    ci.subpass = 0;

    VkPipeline result;
    if (vkCreateGraphicsPipelines(device_, vk_cache_, 1, &ci, nullptr, &result) != VK_SUCCESS) {
      XELOGW("Failed to create graphics pipeline");
      return VK_NULL_HANDLE;
    }
    return result;
  }

  static VkPrimitiveTopology MapPrimitiveType(uint32_t xenos_prim) {
    switch (xenos_prim) {
      case 0x01: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
      case 0x02: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
      case 0x03: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
      case 0x04: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
      case 0x05: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
      case 0x06: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
      case 0x08: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; // rect→tri
      case 0x0D: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; // quad→tri
      default:   return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
  }

  static VkCullModeFlags MapCullMode(uint32_t mode) {
    switch (mode & 0x3) {
      case 0: return VK_CULL_MODE_NONE;
      case 1: return VK_CULL_MODE_FRONT_BIT;
      case 2: return VK_CULL_MODE_BACK_BIT;
      default: return VK_CULL_MODE_NONE;
    }
  }

  static VkCompareOp MapCompareOp(uint32_t op) {
    switch (op) {
      case 0: return VK_COMPARE_OP_NEVER;
      case 1: return VK_COMPARE_OP_LESS;
      case 2: return VK_COMPARE_OP_EQUAL;
      case 3: return VK_COMPARE_OP_LESS_OR_EQUAL;
      case 4: return VK_COMPARE_OP_GREATER;
      case 5: return VK_COMPARE_OP_NOT_EQUAL;
      case 6: return VK_COMPARE_OP_GREATER_OR_EQUAL;
      case 7: return VK_COMPARE_OP_ALWAYS;
      default: return VK_COMPARE_OP_ALWAYS;
    }
  }

  VkDevice device_ = VK_NULL_HANDLE;
  VkRenderPass render_pass_ = VK_NULL_HANDLE;
  VkPipelineCache vk_cache_ = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
  std::vector<VkDescriptorSetLayout> desc_set_layouts_;
  std::unordered_map<PipelineStateKey, VkPipeline, PipelineStateKeyHash> cache_;
};

}  // namespace xe::gpu::vulkan
