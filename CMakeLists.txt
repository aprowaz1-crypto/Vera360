###############################################################################
# Vera360 — Xenia Edge: Xbox 360 Emulator for Android (ARM64/Vulkan)
# Root CMakeLists.txt
###############################################################################
cmake_minimum_required(VERSION 3.22)

project(Vera360
    VERSION   0.1.0
    LANGUAGES C CXX ASM
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Platform gate ────────────────────────────────────────────────────────────
if(NOT ANDROID)
    message(FATAL_ERROR
        "Vera360 targets Android only. "
        "Use the NDK toolchain: -DCMAKE_TOOLCHAIN_FILE=<ndk>/build/cmake/android.toolchain.cmake"
    )
endif()

if(NOT CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
    message(FATAL_ERROR "Only arm64-v8a is supported (got: ${CMAKE_ANDROID_ARCH_ABI})")
endif()

# ── Global compile flags ────────────────────────────────────────────────────
add_compile_options(
    -Wall -Wextra -Wpedantic
    -fno-rtti
    -fno-exceptions          # we use error codes, not exceptions
    -fvisibility=hidden
    -march=armv8-a+crc+crypto
    -DXENIA_EDGE=1
    -DXENIA_PLATFORM_ANDROID=1
    -DVK_USE_PLATFORM_ANDROID_KHR=1
)

# Release optimisations
if(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    add_compile_options(-O3 -flto=thin -DNDEBUG)
    add_link_options(-flto=thin -Wl,--gc-sections -Wl,--icf=all)
endif()

# ── Find Vulkan ──────────────────────────────────────────────────────────────
find_package(Vulkan REQUIRED)
find_library(ANDROID_LOG_LIB log)
find_library(ANDROID_LIB android)

# ── Includes ─────────────────────────────────────────────────────────────────
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/third_party
)

# ── Sub-projects ─────────────────────────────────────────────────────────────
add_subdirectory(src/xenia/base)
add_subdirectory(src/xenia/cpu)
add_subdirectory(src/xenia/gpu)
add_subdirectory(src/xenia/kernel)
add_subdirectory(src/xenia/hid)
add_subdirectory(src/xenia/vfs)
add_subdirectory(src/xenia/apu)
add_subdirectory(src/xenia/app)

# ── Main shared library (loaded by Java/Kotlin) ─────────────────────────────
add_library(vera360 SHARED
    src/xenia/app/jni_bridge.cc
)

target_link_libraries(vera360
    PRIVATE
        xe_app
        xe_gpu
        xe_cpu
        xe_kernel
        xe_hid
        xe_vfs
        xe_apu
        xe_base
        Vulkan::Vulkan
        ${ANDROID_LOG_LIB}
        ${ANDROID_LIB}
)

set_target_properties(vera360 PROPERTIES
    OUTPUT_NAME "vera360"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/android/app/src/main/jniLibs/arm64-v8a"
)
