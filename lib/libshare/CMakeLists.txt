
set(CMAKE_C_COMPILER clang-cl.exe)
set(CMAKE_CXX_COMPILER clang-cl.exe)

add_library(libshare
	libshare.c
    os/windows/nfs.c
    os/windows/smb.c
)
target_link_libraries(libshare PUBLIC libspl)
