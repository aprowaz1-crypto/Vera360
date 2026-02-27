###############################################################################
# xe_base — Platform abstraction layer (POSIX / Android)
###############################################################################
add_library(xe_base STATIC
    memory_posix.cc
    platform_android.cc
    logging.cc
    cvar.cc
    threading_posix.cc
    clock_posix.cc
    string_util.cc
)

target_include_directories(xe_base PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(xe_base PUBLIC ${ANDROID_LOG_LIB})
