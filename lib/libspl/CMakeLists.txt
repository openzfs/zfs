
set(CMAKE_C_COMPILER clang-cl.exe)
set(CMAKE_CXX_COMPILER clang-cl.exe)

add_library(libspl
    asm-generic/atomic.c
    list.c
    mkdirp.c
	page.c
	strlcat.c
	strlcpy.c
    timestamp.c
    #posix.c
    #fdatasync.c
    #gethrestime.c
    #gethrtime.c
    #getmntany.c
    #getopt.c
    #getoptl.c
    #crc32.c
    #uio.c
    #xdr.c
    #xdr_array.c
    #xdr_float.c
    #xdr_mem.c
    #os/windows/zmount.c
    #os/windows/zone.c
)
target_include_directories(libspl BEFORE PUBLIC include)
target_link_libraries(libspl PRIVATE libpthread advapi32 shell32)
