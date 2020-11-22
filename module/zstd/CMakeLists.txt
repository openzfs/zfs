
set(CMAKE_C_COMPILER clang-cl.exe)
set(CMAKE_CXX_COMPILER clang-cl.exe)

wdk_add_library(zstdkern
	lib/zstd.c
	zfs_zstd.c
)

target_include_directories(zstdkern BEFORE PRIVATE "include")
