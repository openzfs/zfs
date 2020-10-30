wdk_add_library(zstdkern
	lib/zstd.c
	zfs_zstd.c
)

target_include_directories(zstdkern BEFORE PRIVATE "include")
