
include_directories(PRIVATE "${CMAKE_SOURCE_DIR}/lib/libspl/include/os/windows/" "${CMAKE_SOURCE_DIR}/lib/libspl/include" "${CMAKE_SOURCE_DIR}/include"
	"${CMAKE_SOURCE_DIR}/lib/os/windows/libpthread" "${OPENSSL_INCLUDE_DIR}")

add_definitions(-D_CRT_SECURE_NO_WARNINGS)

add_subdirectory(zpool)
add_subdirectory(zfs)
add_subdirectory(zdb)
add_subdirectory(zstream)
add_subdirectory(os/windows/kstat)
