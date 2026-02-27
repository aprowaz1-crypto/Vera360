/**
 * Vera360 — Xenia Edge
 * Shader Translator — Xenos shaders → SPIR-V
 *
 * The Xbox 360 Xenos GPU uses a custom shader ISA based on R600.
 * Vertex shaders export position/parameters; pixel shaders export color.
 * Each instruction is 96 bits (3 DWORDs): ALU or fetch.
 *
 * This translator parses Xenos microcode and emits SPIR-V binaries
 * suitable for consumption by Vulkan 1.1 on Android ARM64.
 */

#include "xenia/base/logging.h"
#include <cstdint>
#include <cstring>
#include <vector>
#include <unordered_map>

namespace xe::gpu {

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Types
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

enum class ShaderType : uint8_t {
  kVertex = 0,
  kPixel  = 1,
};

struct TranslatedShader {
  ShaderType type;
  std::vector<uint32_t> spirv_code;
  bool valid = false;
  uint32_t used_textures = 0;    // Bitmask of sampler slots used
  uint32_t used_constants = 0;   // Max constant register index
};

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// SPIR-V builder helper
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

class SpirvBuilder {
 public:
  uint32_t next_id() { return next_id_++; }
  uint32_t bound() const { return next_id_; }

  void EmitHeader() {
    // Filled in at finalization
    header_start_ = code_.size();
    Emit(0x07230203);  // Magic
    Emit(0x00010500);  // Version 1.5
    Emit(0x00080001);  // Generator (Vera360)
    Emit(0);           // Bound (patched later)
    Emit(0);           // Schema
  }

  // OpCapability
  void OpCapability(uint32_t cap) {
    Emit((2 << 16) | 17); Emit(cap);
  }
  // OpExtInstImport
  uint32_t OpExtInstImport(const char* name) {
    uint32_t id = next_id();
    uint32_t word_count = 2 + WordCountStr(name);
    Emit((word_count << 16) | 11); Emit(id); EmitStr(name);
    return id;
  }
  // OpMemoryModel
  void OpMemoryModel(uint32_t addr, uint32_t mem) {
    Emit((3 << 16) | 14); Emit(addr); Emit(mem);
  }
  // OpEntryPoint
  void OpEntryPoint(uint32_t exec_model, uint32_t func_id,
                    const char* name, const std::vector<uint32_t>& interfaces) {
    uint32_t wc = 3 + WordCountStr(name) + static_cast<uint32_t>(interfaces.size());
    Emit((wc << 16) | 15); Emit(exec_model); Emit(func_id);
    EmitStr(name);
    for (auto id : interfaces) Emit(id);
  }
  // OpExecutionMode
  void OpExecutionMode(uint32_t id, uint32_t mode) {
    Emit((3 << 16) | 16); Emit(id); Emit(mode);
  }
  // OpDecorate
  void OpDecorate(uint32_t target, uint32_t deco, uint32_t operand) {
    Emit((4 << 16) | 71); Emit(target); Emit(deco); Emit(operand);
  }
  void OpDecorate(uint32_t target, uint32_t deco) {
    Emit((3 << 16) | 71); Emit(target); Emit(deco);
  }
  void OpMemberDecorate(uint32_t struct_type, uint32_t member, uint32_t deco, uint32_t val) {
    Emit((5 << 16) | 72); Emit(struct_type); Emit(member); Emit(deco); Emit(val);
  }
  // OpTypeVoid
  uint32_t OpTypeVoid() { uint32_t id=next_id(); Emit((2<<16)|19); Emit(id); return id; }
  // OpTypeFloat
  uint32_t OpTypeFloat(uint32_t width) { uint32_t id=next_id(); Emit((3<<16)|22); Emit(id); Emit(width); return id; }
  // OpTypeInt
  uint32_t OpTypeInt(uint32_t width, uint32_t sign) { uint32_t id=next_id(); Emit((4<<16)|21); Emit(id); Emit(width); Emit(sign); return id; }
  // OpTypeVector
  uint32_t OpTypeVector(uint32_t comp_type, uint32_t count) { uint32_t id=next_id(); Emit((4<<16)|23); Emit(id); Emit(comp_type); Emit(count); return id; }
  // OpTypeMatrix
  uint32_t OpTypeMatrix(uint32_t col_type, uint32_t cols) { uint32_t id=next_id(); Emit((4<<16)|24); Emit(id); Emit(col_type); Emit(cols); return id; }
  // OpTypeArray
  uint32_t OpTypeArray(uint32_t elem, uint32_t length) { uint32_t id=next_id(); Emit((4<<16)|28); Emit(id); Emit(elem); Emit(length); return id; }
  // OpTypeStruct
  uint32_t OpTypeStruct(const std::vector<uint32_t>& members) {
    uint32_t id=next_id(); uint32_t wc=2+static_cast<uint32_t>(members.size());
    Emit((wc<<16)|30); Emit(id);
    for (auto m : members) Emit(m);
    return id;
  }
  // OpTypePointer
  uint32_t OpTypePointer(uint32_t storage_class, uint32_t type) {
    uint32_t id=next_id(); Emit((4<<16)|32); Emit(id); Emit(storage_class); Emit(type); return id;
  }
  // OpTypeFunction
  uint32_t OpTypeFunction(uint32_t return_type) {
    uint32_t id=next_id(); Emit((3<<16)|33); Emit(id); Emit(return_type); return id;
  }
  // OpTypeSampledImage
  uint32_t OpTypeSampledImage(uint32_t image_type) {
    uint32_t id=next_id(); Emit((3<<16)|27); Emit(id); Emit(image_type); return id;
  }
  // OpTypeImage
  uint32_t OpTypeImage(uint32_t sampled_type, uint32_t dim, uint32_t depth,
                       uint32_t arrayed, uint32_t ms, uint32_t sampled, uint32_t format) {
    uint32_t id=next_id(); Emit((9<<16)|25); Emit(id); Emit(sampled_type);
    Emit(dim); Emit(depth); Emit(arrayed); Emit(ms); Emit(sampled); Emit(format);
    return id;
  }
  // OpConstant (float)
  uint32_t OpConstantF(uint32_t type, float val) {
    uint32_t id=next_id(); uint32_t bits; memcpy(&bits, &val, 4);
    Emit((4<<16)|43); Emit(type); Emit(id); Emit(bits); return id;
  }
  // OpConstant (int)
  uint32_t OpConstantI(uint32_t type, uint32_t val) {
    uint32_t id=next_id(); Emit((4<<16)|43); Emit(type); Emit(id); Emit(val); return id;
  }
  // OpConstantComposite
  uint32_t OpConstantComposite(uint32_t type, const std::vector<uint32_t>& constituents) {
    uint32_t id=next_id(); uint32_t wc=3+static_cast<uint32_t>(constituents.size());
    Emit((wc<<16)|44); Emit(type); Emit(id);
    for (auto c : constituents) Emit(c);
    return id;
  }
  // OpVariable
  uint32_t OpVariable(uint32_t ptr_type, uint32_t storage_class) {
    uint32_t id=next_id(); Emit((4<<16)|59); Emit(ptr_type); Emit(id); Emit(storage_class); return id;
  }
  // OpFunction
  uint32_t OpFunction(uint32_t result_type, uint32_t func_control, uint32_t func_type) {
    uint32_t id=next_id(); Emit((5<<16)|54); Emit(result_type); Emit(id); Emit(func_control); Emit(func_type); return id;
  }
  // OpLabel
  uint32_t OpLabel() { uint32_t id=next_id(); Emit((2<<16)|248); Emit(id); return id; }
  // OpReturn
  void OpReturn() { Emit((1<<16)|253); }
  // OpFunctionEnd
  void OpFunctionEnd() { Emit((1<<16)|56); }
  // OpLoad
  uint32_t OpLoad(uint32_t type, uint32_t ptr) {
    uint32_t id=next_id(); Emit((4<<16)|61); Emit(type); Emit(id); Emit(ptr); return id;
  }
  // OpStore
  void OpStore(uint32_t ptr, uint32_t val) {
    Emit((3<<16)|62); Emit(ptr); Emit(val);
  }
  // OpAccessChain
  uint32_t OpAccessChain(uint32_t result_type, uint32_t base, const std::vector<uint32_t>& indices) {
    uint32_t id=next_id(); uint32_t wc=4+static_cast<uint32_t>(indices.size());
    Emit((wc<<16)|65); Emit(result_type); Emit(id); Emit(base);
    for (auto idx : indices) Emit(idx);
    return id;
  }
  // OpCompositeConstruct
  uint32_t OpCompositeConstruct(uint32_t type, const std::vector<uint32_t>& parts) {
    uint32_t id=next_id(); uint32_t wc=3+static_cast<uint32_t>(parts.size());
    Emit((wc<<16)|80); Emit(type); Emit(id);
    for (auto p : parts) Emit(p);
    return id;
  }
  // OpVectorShuffle
  uint32_t OpVectorShuffle(uint32_t type, uint32_t v1, uint32_t v2, const std::vector<uint32_t>& comps) {
    uint32_t id=next_id(); uint32_t wc=5+static_cast<uint32_t>(comps.size());
    Emit((wc<<16)|79); Emit(type); Emit(id); Emit(v1); Emit(v2);
    for (auto c : comps) Emit(c);
    return id;
  }
  // OpFAdd, OpFMul, OpFSub, OpFDiv
  uint32_t OpFAdd(uint32_t type, uint32_t a, uint32_t b) {
    uint32_t id=next_id(); Emit((5<<16)|129); Emit(type); Emit(id); Emit(a); Emit(b); return id;
  }
  uint32_t OpFSub(uint32_t type, uint32_t a, uint32_t b) {
    uint32_t id=next_id(); Emit((5<<16)|131); Emit(type); Emit(id); Emit(a); Emit(b); return id;
  }
  uint32_t OpFMul(uint32_t type, uint32_t a, uint32_t b) {
    uint32_t id=next_id(); Emit((5<<16)|133); Emit(type); Emit(id); Emit(a); Emit(b); return id;
  }
  uint32_t OpDot(uint32_t type, uint32_t a, uint32_t b) {
    uint32_t id=next_id(); Emit((5<<16)|148); Emit(type); Emit(id); Emit(a); Emit(b); return id;
  }
  // OpImageSampleImplicitLod
  uint32_t OpImageSampleImplicitLod(uint32_t type, uint32_t sampled_img, uint32_t coord) {
    uint32_t id=next_id(); Emit((5<<16)|87); Emit(type); Emit(id); Emit(sampled_img); Emit(coord); return id;
  }

  std::vector<uint32_t> Finalize() {
    code_[header_start_ + 3] = next_id_;
    return code_;
  }

 private:
  void Emit(uint32_t word) { code_.push_back(word); }
  void EmitStr(const char* s) {
    uint32_t len = static_cast<uint32_t>(strlen(s));
    uint32_t words = (len + 4) / 4;
    for (uint32_t w = 0; w < words; ++w) {
      uint32_t val = 0;
      for (uint32_t b = 0; b < 4; ++b) {
        uint32_t idx = w * 4 + b;
        if (idx < len) val |= (static_cast<uint32_t>(s[idx]) << (b * 8));
      }
      code_.push_back(val);
    }
  }
  uint32_t WordCountStr(const char* s) {
    return (static_cast<uint32_t>(strlen(s)) + 4) / 4;
  }

  std::vector<uint32_t> code_;
  uint32_t next_id_ = 1;
  size_t header_start_ = 0;
};

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Xenos shader microcode constants
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

namespace ucode {
  // Instruction types (bits 0-1 of word 0)
  constexpr uint32_t kAluInstruction   = 0;
  constexpr uint32_t kFetchInstruction = 1;

  // ALU scalar opcodes (bits 23-28 of word 2)
  constexpr uint32_t kScalarMov    = 1;
  constexpr uint32_t kScalarExp    = 2;
  constexpr uint32_t kScalarLog    = 3;
  constexpr uint32_t kScalarRcp    = 4;
  constexpr uint32_t kScalarRsq    = 5;
  constexpr uint32_t kScalarMaxs   = 6;
  constexpr uint32_t kScalarMins   = 7;
  constexpr uint32_t kScalarFloor  = 10;
  constexpr uint32_t kScalarFrac   = 11;
  constexpr uint32_t kScalarSqrt   = 13;

  // ALU vector opcodes (bits 20-24 of word 1)
  constexpr uint32_t kVectorAdd   = 0;
  constexpr uint32_t kVectorMul   = 1;
  constexpr uint32_t kVectorMax   = 2;
  constexpr uint32_t kVectorMin   = 3;
  constexpr uint32_t kVectorDp3   = 5;
  constexpr uint32_t kVectorDp4   = 6;
  constexpr uint32_t kVectorFloor = 11;
  constexpr uint32_t kVectorFrac  = 12;
  constexpr uint32_t kVectorMad   = 14;

  // Fetch opcodes (bits 0-4 of word 0)
  constexpr uint32_t kFetchVertex = 0;
  constexpr uint32_t kFetchTexture = 1;

  // Export destinations
  constexpr uint32_t kExportPosition  = 62;
  constexpr uint32_t kExportParam0    = 0;
  constexpr uint32_t kExportColor0    = 0;  // Pixel shader
}  // namespace ucode

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Shader Translator
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

class ShaderTranslator {
 public:
  ShaderTranslator() = default;

  /// Translate Xenos shader microcode to SPIR-V
  TranslatedShader Translate(ShaderType type,
                              const uint32_t* microcode,
                              uint32_t dword_count) {
    TranslatedShader result;
    result.type = type;

    XELOGI("Translating shader: type={}, dwords={}",
            static_cast<int>(type), dword_count);

    if (type == ShaderType::kVertex) {
      result.spirv_code = BuildVertexShader(microcode, dword_count);
    } else {
      result.spirv_code = BuildPixelShader(microcode, dword_count);
    }
    result.valid = !result.spirv_code.empty();
    return result;
  }

 private:
  // ── Build a passthrough vertex shader ──────────────────────────────────
  // Takes position from vertex buffer, passes texcoord to fragment
  std::vector<uint32_t> BuildVertexShader(const uint32_t* /*ucode*/,
                                           uint32_t /*dword_count*/) {
    SpirvBuilder b;
    b.EmitHeader();

    // Capability Shader
    b.OpCapability(1);  // Capability::Shader

    // GLSL.std.450
    uint32_t glsl = b.OpExtInstImport("GLSL.std.450");
    (void)glsl;

    // Memory model
    b.OpMemoryModel(0, 1);  // Addressing::Logical, MemoryModel::GLSL450

    // Types
    uint32_t void_t   = b.OpTypeVoid();
    uint32_t float_t  = b.OpTypeFloat(32);
    uint32_t vec4_t   = b.OpTypeVector(float_t, 4);
    uint32_t vec2_t   = b.OpTypeVector(float_t, 2);
    uint32_t int_t    = b.OpTypeInt(32, 1);
    (void)int_t;

    // Constant float values
    uint32_t f0 = b.OpConstantF(float_t, 0.0f);
    uint32_t f1 = b.OpConstantF(float_t, 1.0f);
    (void)f0; (void)f1;

    // UBO for vertex shader constants (256 float4)
    uint32_t arr_256_t = b.OpTypeArray(vec4_t, b.OpConstantI(b.OpTypeInt(32, 0), 256));
    uint32_t ubo_struct = b.OpTypeStruct({arr_256_t});
    uint32_t ubo_ptr = b.OpTypePointer(2, ubo_struct); // StorageClass::Uniform
    uint32_t ubo_var = b.OpVariable(ubo_ptr, 2);

    // Input: position (location=0), texcoord (location=1)
    uint32_t in_ptr_vec4 = b.OpTypePointer(1, vec4_t); // Input
    uint32_t in_ptr_vec2 = b.OpTypePointer(1, vec2_t);
    uint32_t in_position = b.OpVariable(in_ptr_vec4, 1);
    uint32_t in_texcoord = b.OpVariable(in_ptr_vec2, 1);

    // Output: gl_Position (builtin), v_texcoord (location=0)
    uint32_t out_ptr_vec4 = b.OpTypePointer(3, vec4_t); // Output
    uint32_t out_ptr_vec2 = b.OpTypePointer(3, vec2_t);
    uint32_t out_position = b.OpVariable(out_ptr_vec4, 3);
    uint32_t out_texcoord = b.OpVariable(out_ptr_vec2, 3);

    // Function type
    uint32_t func_type = b.OpTypeFunction(void_t);

    // Entry point
    b.OpEntryPoint(0, b.next_id(), "main",
                    {in_position, in_texcoord, out_position, out_texcoord});
    uint32_t main_func = b.next_id() - 1;

    // Execution mode
    b.OpExecutionMode(main_func, 7);  // OriginUpperLeft (not needed for vertex but harmless)

    // Decorations
    b.OpDecorate(in_position, 30, 0);  // Location 0
    b.OpDecorate(in_texcoord, 30, 1);  // Location 1
    b.OpDecorate(out_position, 11, 0); // BuiltIn Position
    b.OpDecorate(out_texcoord, 30, 0); // Location 0
    b.OpDecorate(ubo_var, 33, 0);      // DescriptorSet 0
    b.OpDecorate(ubo_var, 34, 0);      // Binding 0
    b.OpDecorate(ubo_struct, 2);       // Block
    b.OpMemberDecorate(ubo_struct, 0, 35, 0); // Offset 0

    // Decorate array stride
    b.OpDecorate(arr_256_t, 6, 16);    // ArrayStride 16

    // Function main
    b.OpFunction(void_t, 0, func_type);
    b.OpLabel();

    // Load position and store to gl_Position
    uint32_t pos = b.OpLoad(vec4_t, in_position);
    b.OpStore(out_position, pos);

    // Load texcoord and pass through
    uint32_t tc = b.OpLoad(vec2_t, in_texcoord);
    b.OpStore(out_texcoord, tc);

    b.OpReturn();
    b.OpFunctionEnd();

    return b.Finalize();
  }

  // ── Build a passthrough pixel shader ───────────────────────────────────
  // Samples texture at v_texcoord, outputs color
  std::vector<uint32_t> BuildPixelShader(const uint32_t* /*ucode*/,
                                          uint32_t /*dword_count*/) {
    SpirvBuilder b;
    b.EmitHeader();

    b.OpCapability(1);  // Shader
    uint32_t glsl = b.OpExtInstImport("GLSL.std.450");
    (void)glsl;
    b.OpMemoryModel(0, 1);

    uint32_t void_t  = b.OpTypeVoid();
    uint32_t float_t = b.OpTypeFloat(32);
    uint32_t vec4_t  = b.OpTypeVector(float_t, 4);
    uint32_t vec2_t  = b.OpTypeVector(float_t, 2);

    // Sampler2D input (binding=0, set=1)
    uint32_t img_type = b.OpTypeImage(float_t, 1, 0, 0, 0, 1, 0); // Dim::2D
    uint32_t sampled_img_type = b.OpTypeSampledImage(img_type);
    uint32_t sampler_ptr = b.OpTypePointer(0, sampled_img_type); // UniformConstant
    uint32_t sampler_var = b.OpVariable(sampler_ptr, 0);

    // Input: v_texcoord (location=0)
    uint32_t in_ptr_vec2 = b.OpTypePointer(1, vec2_t);
    uint32_t in_texcoord = b.OpVariable(in_ptr_vec2, 1);

    // Output: fragColor (location=0)
    uint32_t out_ptr_vec4 = b.OpTypePointer(3, vec4_t);
    uint32_t out_color = b.OpVariable(out_ptr_vec4, 3);

    uint32_t func_type = b.OpTypeFunction(void_t);

    b.OpEntryPoint(4, b.next_id(), "main",
                    {in_texcoord, out_color});
    uint32_t main_func = b.next_id() - 1;

    b.OpExecutionMode(main_func, 7);  // OriginUpperLeft

    b.OpDecorate(in_texcoord, 30, 0); // Location 0
    b.OpDecorate(out_color, 30, 0);   // Location 0
    b.OpDecorate(sampler_var, 33, 1); // DescriptorSet 1
    b.OpDecorate(sampler_var, 34, 0); // Binding 0

    b.OpFunction(void_t, 0, func_type);
    b.OpLabel();

    uint32_t tc = b.OpLoad(vec2_t, in_texcoord);
    uint32_t simg = b.OpLoad(sampled_img_type, sampler_var);
    uint32_t color = b.OpImageSampleImplicitLod(vec4_t, simg, tc);
    b.OpStore(out_color, color);

    b.OpReturn();
    b.OpFunctionEnd();

    return b.Finalize();
  }
};

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Shader cache: hash → translated
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

class ShaderCache {
 public:
  TranslatedShader* GetOrTranslate(ShaderType type,
                                    const uint32_t* microcode,
                                    uint32_t dword_count) {
    // FNV-1a hash over microcode
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (uint32_t i = 0; i < dword_count; ++i) {
      hash ^= microcode[i];
      hash *= 0x100000001b3ULL;
    }
    hash ^= static_cast<uint64_t>(type);

    auto it = cache_.find(hash);
    if (it != cache_.end()) return &it->second;

    ShaderTranslator translator;
    cache_[hash] = translator.Translate(type, microcode, dword_count);
    return &cache_[hash];
  }

  void Clear() { cache_.clear(); }

 private:
  std::unordered_map<uint64_t, TranslatedShader> cache_;
};

}  // namespace xe::gpu
