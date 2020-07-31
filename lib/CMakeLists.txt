
include_directories(PRIVATE "${CMAKE_SOURCE_DIR}/lib/libspl/include/os/windows" "${CMAKE_SOURCE_DIR}/lib/libspl/include" "${CMAKE_SOURCE_DIR}/include"
	"${CMAKE_SOURCE_DIR}/lib/os/windows/libpthread" "${OPENSSL_INCLUDE_DIR}")

add_definitions(-D_CRT_SECURE_NO_WARNINGS)
add_definitions(-D_CRT_LOADCFG_DISABLE_CET)

add_subdirectory(libavl)
add_subdirectory(libefi)
add_subdirectory(libicp)
add_subdirectory(libnvpair)
add_subdirectory(libshare)
add_subdirectory(libspl)
add_subdirectory(libtpool)
add_subdirectory(libunicode)
add_subdirectory(libuutil)
add_subdirectory(libzfs)
add_subdirectory(libzfs_core)
add_subdirectory(libzfsbootenv)
add_subdirectory(libzpool)
add_subdirectory(libzstd)
add_subdirectory(libzutil)
add_subdirectory(os/windows)
