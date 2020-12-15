
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

variable_watch(CRYPTO_STATIC)
# set(CRYPTO_STATIC "notset")
set(CMAKE_FIND_DEBUG_MODE TRUE)
find_library(CRYPTO_STATIC
    NAMES
      libcrypto_static
    NAMES_PER_DIR
    ${_OPENSSL_ROOT_HINTS_AND_PATHS}
    HINTS
      ${OPENSSL_ROOT_DIR}/lib
    PATH_SUFFIXES
      lib
	REQUIRED
  )

target_link_libraries(libzfs PUBLIC libpthread zlib libzutil libshare libzfs_core libnvpair libuutil ${CRYPTO_STATIC})
