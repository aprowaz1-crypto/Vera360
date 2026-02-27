###############################################################################
# xe_hid — Human Interface Devices (touch controller overlay)
###############################################################################
add_library(xe_hid STATIC
    hid_android.cc
    input_system.cc
)

target_include_directories(xe_hid PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(xe_hid PUBLIC xe_base)
