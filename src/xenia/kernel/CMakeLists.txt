###############################################################################
# xe_kernel — Xbox 360 kernel / XAM / XboxKrnl shim
###############################################################################
add_library(xe_kernel STATIC
    xboxkrnl_module.cc
    xboxkrnl_memory.cc
    xboxkrnl_threading.cc
    xboxkrnl_io.cc
    xam_module.cc
    xam_user.cc
    xam_content.cc
    kernel_state.cc
    xmodule.cc
    xthread.cc
    xobject.cc
    xex2_loader.cc
)

target_include_directories(xe_kernel PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(xe_kernel PUBLIC xe_base xe_cpu xe_vfs)
