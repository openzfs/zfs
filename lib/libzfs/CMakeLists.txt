
set(CMAKE_C_COMPILER clang-cl.exe)
set(CMAKE_CXX_COMPILER clang-cl.exe)

add_library(libzfs
	libzfs_changelist.c
	libzfs_config.c
	libzfs_crypto.c
	libzfs_dataset.c
	libzfs_diff.c
	libzfs_import.c
	libzfs_iter.c
	libzfs_mount.c
	libzfs_pool.c
	libzfs_sendrecv.c
	libzfs_status.c
	libzfs_util.c
	os/windows/libzfs_mount_os.c
	os/windows/libzfs_pool_os.c
	os/windows/libzfs_util_os.c
)
target_include_directories(libzfs BEFORE PRIVATE ../libspl/include)
target_link_libraries(libzfs PUBLIC libpthread zlib)
