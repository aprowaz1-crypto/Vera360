###############################################################################
# xe_cpu — PowerPC 750 (Xenon) recompiler targeting ARM64
###############################################################################
add_library(xe_cpu STATIC
    frontend/ppc_decoder.cc
    frontend/ppc_disasm.cc
    frontend/ppc_scanner.cc
    backend/arm64/arm64_emitter.cc
    backend/arm64/arm64_backend.cc
    backend/arm64/arm64_sequences.cc
    cpu_module.cc
    thread_state.cc
    processor.cc
)

target_include_directories(xe_cpu PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(xe_cpu PUBLIC xe_base)
