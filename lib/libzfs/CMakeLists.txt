
use_clang()

add_library(libzfs
	libzfs_changelist.c
	libzfs_config.c
	libzfs_crypto.c
	libzfs_dataset.c
	libzfs_diff.c
	libzfs_import.c
	libzfs_iter.c
	libzfs_mnttab.c
	libzfs_mount.c
	libzfs_pool.c
	libzfs_sendrecv.c
	libzfs_share.c
	libzfs_status.c
	libzfs_util.c
	os/windows/libzfs_share_nfs.c
	os/windows/libzfs_share_smb.c
	os/windows/libzfs_disk_os.cpp
	os/windows/libzfs_mount_os.c
	os/windows/libzfs_pool_os.c
	os/windows/libzfs_util_os.c
	os/windows/libzfs_weakpragma.c
 )

#variable_watch(CRYPTO_STATIC)
# set(CRYPTO_STATIC "notset")
# '/c/Program Files/OpenSSL-Win64/lib/VC/x64/MTd/libssl_static.lib'
set(CMAKE_FIND_DEBUG_MODE TRUE)

if (CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "x86")
elseif (CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
	find_library(CRYPTO_STATIC_TEST
		NAMES
		  libcrypto_static
		NAMES_PER_DIR
		HINTS
		  "C:/Program Files/OpenSSL-Win64/lib/VC/x64/MTd"
		PATH_SUFFIXES
		  lib
		REQUIRED
	  )
elseif (CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "ARM64")
	find_library(CRYPTO_STATIC_TEST
		NAMES
		  libcrypto_static
		NAMES_PER_DIR
		HINTS
		  "C:/Program Files/OpenSSL-Win64-ARM/lib/VC/arm64/MTd"
		PATH_SUFFIXES
		  lib
		REQUIRED
	  )
endif()

# Optionally, if there is a "lib/curl*" directory
# for example, "lib/curl-8.6.0_4-win64-mingw" we also enable
# LIBFETCH_IS_LIBCURL define, and link in
# "lib/curl-8.6.0_4-win64-mingw/lib/libcurl"
# https://curl.se/windows/
#file( GLOB curl_paths "${CMAKE_SOURCE_DIR}/lib/curl*" )
#list(LENGTH curl_paths paths_len)
#if(paths_len GREATER 0)
#	list(GET curl_paths 0 curl_path)
#	message("Good News Everyone: Found lib/curl: ${curl_path}")
#	add_definitions(-DLIBFETCH_IS_LIBCURL -DCURL_STATICLIB -DCURL_DISABLE_LDAP)
#	include_directories(AFTER "${curl_path}/include")
#	target_link_libraries(libzfs PRIVATE
#		"${curl_path}/lib/libcurl.a"
#		"${curl_path}/lib/libssh2.a"
#		"${curl_path}/lib/libpsl.a"
#		"${curl_path}/lib/libnghttp2.a")
#endif()


target_include_directories(libzfs PRIVATE "${CMAKE_SOURCE_DIR}/lib/libzfs" "${CMAKE_SOURCE_DIR}/lib/os/windows/libfetch")
target_link_libraries(libzfs PUBLIC libpthread zlib libzutil libzfs_core libnvpair libfetch)
target_link_libraries(libzfs PRIVATE Crypt32.lib ${CRYPTO_STATIC_TEST})
