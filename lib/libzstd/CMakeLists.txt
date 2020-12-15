
use_clang()

set(ZSTD_MODULE_DIR "../../module/zstd")

add_library(libzstd
	"${ZSTD_MODULE_DIR}/zfs_zstd.c"
	"${ZSTD_MODULE_DIR}/lib/zstd.c"
)
target_link_libraries(libzstd PUBLIC libnvpair libpthread)
