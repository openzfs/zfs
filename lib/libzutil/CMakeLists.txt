
set(CMAKE_C_COMPILER clang-cl.exe)
set(CMAKE_CXX_COMPILER clang-cl.exe)

add_library(libzutil
	zutil_device_path.c
	zutil_import.c
	zutil_nicenum.c
	zutil_pool.c
)

target_link_libraries(libzutil PUBLIC
	libicp
	libnvpair
	libunicode
	libuutil
	libpthread
	libavl
	libefi
	libshare
	zlib
)
