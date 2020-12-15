
use_clang()

# Unfortunately, libbootenv uses kernel headers as well.
include_directories(PRIVATE "${CMAKE_SOURCE_DIR}/include/os/windows/zfs")

add_library(libzfsbootenv
	lzbe_device.c
	lzbe_pair.c
	lzbe_util.c
)
target_link_libraries(libzfsbootenv PUBLIC libnvpair libpthread)
