###############################################################################
# xe_app — Application entry point + JNI bridge
###############################################################################
add_library(xe_app STATIC
    emulator.cc
)

target_include_directories(xe_app PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(xe_app PUBLIC xe_base xe_cpu xe_gpu xe_kernel xe_hid xe_vfs xe_apu)
