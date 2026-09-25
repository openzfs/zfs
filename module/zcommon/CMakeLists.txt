
use_clang()

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
	zfs_valstr.c
	zpool_prop.c
	zprop_common.c
)

if (CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "x86")
elseif (CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
	target_sources(zcommonkern PRIVATE
		zfs_fletcher_avx512.c
		zfs_fletcher_intel.c
		zfs_fletcher_sse.c
	)
elseif (CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "ARM64")
endif()


target_link_libraries(zcommonkern PRIVATE splkern)
