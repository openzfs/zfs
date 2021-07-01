
use_clang()

include_directories(PRIVATE .)

add_library(libshare
	libshare.c
    os/windows/nfs.c
    os/windows/smb.c
)
target_include_directories(libshare PRIVATE "${CMAKE_SOURCE_DIR}/lib/libzfs")
target_link_libraries(libshare PUBLIC libspl)
