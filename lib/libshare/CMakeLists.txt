
use_clang()

include_directories(PRIVATE .)

add_library(libshare
	libshare.c
    os/windows/nfs.c
    os/windows/smb.c
)
target_link_libraries(libshare PUBLIC libspl)
