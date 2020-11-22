
set(CMAKE_C_COMPILER clang-cl.exe)
set(CMAKE_CXX_COMPILER clang-cl.exe)

wdk_add_library(zcommonkern
	cityhash.c
	zfeature_common.c
	zfs_comutil.c
	zfs_deleg.c
	zfs_fletcher.c
	zfs_fletcher_avx512.c
	zfs_fletcher_intel.c
	zfs_fletcher_sse.c
	zfs_fletcher_superscalar.c
	zfs_fletcher_superscalar4.c
	zfs_namecheck.c
	zfs_prop.c
	zpool_prop.c
	zprop_common.c
)

target_link_libraries(zcommonkern PRIVATE splkern)
