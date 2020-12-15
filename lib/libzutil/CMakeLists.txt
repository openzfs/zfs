
use_clang()

target_include_directories(libspl BEFORE PUBLIC "")

add_library(libzutil
	zutil_device_path.c
	zutil_import.c
	zutil_nicenum.c
	zutil_pool.c
	os/windows/zutil_compat.c
	os/windows/zutil_device_path_os.c
	os/windows/zutil_import_os.c
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
	libtpool
	zlib
)
