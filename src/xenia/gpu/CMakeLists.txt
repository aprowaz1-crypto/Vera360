###############################################################################
# xe_gpu — Xenos GPU emulation via Vulkan
###############################################################################
add_library(xe_gpu STATIC
    vulkan/vulkan_instance.cc
    vulkan/vulkan_device.cc
    vulkan/vulkan_swap_chain.cc
    vulkan/vulkan_pipeline_cache.cc
    vulkan/vulkan_command_processor.cc
    vulkan/vulkan_texture_cache.cc
    vulkan/vulkan_render_target_cache.cc
    xenos_registers.cc
    gpu_command_processor.cc
    shader_translator.cc
)

target_include_directories(xe_gpu PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(xe_gpu PUBLIC xe_base Vulkan::Vulkan ${ANDROID_LIB})
