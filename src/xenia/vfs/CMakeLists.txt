###############################################################################
# xe_vfs — Virtual File System (STFS, SVOD, host path mapping)
###############################################################################
add_library(xe_vfs STATIC
    stfs_container.cc
    host_path_device.cc
    disc_image_device.cc
    virtual_file_system.cc
    vfs_entry.cc
)

target_include_directories(xe_vfs PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(xe_vfs PUBLIC xe_base)
