set(CMAKE_C_COMPILER clang-cl.exe)
set(CMAKE_CXX_COMPILER clang-cl.exe)

set(ZSTD_MODULE_DIR "../../module/zstd")

add_library(libzstd
	"${ZSTD_MODULE_DIR}/zfs_zstd.c"
	"${ZSTD_MODULE_DIR}/lib/zstd.c"
)
target_link_libraries(libzstd PUBLIC libnvpair libpthread)
