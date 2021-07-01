
use_clang()

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

#variable_watch(CRYPTO_STATIC)
# set(CRYPTO_STATIC "notset")
set(CMAKE_FIND_DEBUG_MODE TRUE)
find_library(CRYPTO_STATIC_TEST
    NAMES
      libcrypto64MTd
    NAMES_PER_DIR
	HINTS
	  "C:/Program Files/OpenSSL-Win64/lib/VC/static"
    PATH_SUFFIXES
      lib
	REQUIRED
  )

target_include_directories(libzfs PRIVATE "${CMAKE_SOURCE_DIR}/lib/libzfs")
target_link_libraries(libzfs PUBLIC libpthread zlib libzutil libshare libzfs_core libnvpair libuutil)
target_link_libraries(libzfs PRIVATE Crypt32.lib ${CRYPTO_STATIC_TEST})
