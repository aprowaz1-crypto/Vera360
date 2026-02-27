###############################################################################
# Vera360 — Android Toolchain Helper
# This file is sourced AFTER the NDK toolchain to apply project-specific tweaks.
# Usage: cmake -DCMAKE_TOOLCHAIN_FILE=<ndk>/build/cmake/android.toolchain.cmake
#              -DCMAKE_PROJECT_INCLUDE=cmake/android_tweaks.cmake
###############################################################################

# Require API 29+ (Android 10) for Vulkan 1.1 baseline
if(ANDROID_NATIVE_API_LEVEL LESS 29)
    message(WARNING "ANDROID_NATIVE_API_LEVEL < 29; bumping to 29 for Vulkan 1.1 baseline")
    set(ANDROID_NATIVE_API_LEVEL 29 CACHE STRING "" FORCE)
endif()

# Prefer shared STL so we don't bloat the .so
if(NOT DEFINED ANDROID_STL)
    set(ANDROID_STL "c++_shared" CACHE STRING "" FORCE)
endif()

message(STATUS "[Vera360] ABI: ${CMAKE_ANDROID_ARCH_ABI}")
message(STATUS "[Vera360] API: ${ANDROID_NATIVE_API_LEVEL}")
message(STATUS "[Vera360] NDK: ${ANDROID_NDK}")
