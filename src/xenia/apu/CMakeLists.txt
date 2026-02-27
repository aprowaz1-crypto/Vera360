###############################################################################
# xe_apu — Audio Processing Unit (XMA decoder stub)
###############################################################################
add_library(xe_apu STATIC
    apu_system.cc
    xma_decoder.cc
)

target_include_directories(xe_apu PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(xe_apu PUBLIC xe_base)
