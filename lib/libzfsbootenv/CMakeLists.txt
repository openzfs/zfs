
set(CMAKE_C_COMPILER clang-cl.exe)
set(CMAKE_CXX_COMPILER clang-cl.exe)

add_library(libzfsbootenv
	lzbe_device.c
	lzbe_pair.c
	lzbe_util.c
)
target_link_libraries(libzfsbootenv PUBLIC libnvpair libpthread)
