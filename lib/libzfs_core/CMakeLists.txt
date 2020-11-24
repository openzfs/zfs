
set(CMAKE_C_COMPILER clang-cl.exe)
set(CMAKE_CXX_COMPILER clang-cl.exe)

add_library(libzfs_core
	libzfs_core.c
)
target_link_libraries(libzfs_core PUBLIC libnvpair libpthread)
