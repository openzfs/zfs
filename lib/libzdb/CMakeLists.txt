
use_clang()

add_library(libzdb
    "libzdb.c"
)

target_include_directories(libzdb PRIVATE "${CMAKE_SOURCE_DIR}/include/os/windows/zfs")
