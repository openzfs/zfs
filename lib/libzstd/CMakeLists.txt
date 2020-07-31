
use_clang()

set(ZSTD_MODULE_DIR "../../module/zstd")

add_library(libzstd
	"${ZSTD_MODULE_DIR}/lib/common/entropy_common.c"
	"${ZSTD_MODULE_DIR}/lib/common/error_private.c"
	"${ZSTD_MODULE_DIR}/lib/common/fse_decompress.c"
	"${ZSTD_MODULE_DIR}/lib/common/pool.c"
	"${ZSTD_MODULE_DIR}/lib/common/zstd_common.c"
	"${ZSTD_MODULE_DIR}/lib/common/xxhash.c"
	"${ZSTD_MODULE_DIR}/lib/compress/fse_compress.c"
	"${ZSTD_MODULE_DIR}/lib/compress/hist.c"
	"${ZSTD_MODULE_DIR}/lib/compress/huf_compress.c"
	"${ZSTD_MODULE_DIR}/lib/compress/zstd_compress_literals.c"
	"${ZSTD_MODULE_DIR}/lib/compress/zstd_compress_sequences.c"
	"${ZSTD_MODULE_DIR}/lib/compress/zstd_compress_superblock.c"
	"${ZSTD_MODULE_DIR}/lib/compress/zstd_compress.c"
	"${ZSTD_MODULE_DIR}/lib/compress/zstd_double_fast.c"
	"${ZSTD_MODULE_DIR}/lib/compress/zstd_fast.c"
	"${ZSTD_MODULE_DIR}/lib/compress/zstd_lazy.c"
	"${ZSTD_MODULE_DIR}/lib/compress/zstd_ldm.c"
	"${ZSTD_MODULE_DIR}/lib/compress/zstd_opt.c"
	"${ZSTD_MODULE_DIR}/lib/decompress/huf_decompress.c"
	"${ZSTD_MODULE_DIR}/lib/decompress/zstd_ddict.c"
	"${ZSTD_MODULE_DIR}/lib/decompress/zstd_decompress.c"
	"${ZSTD_MODULE_DIR}/lib/decompress/zstd_decompress_block.c"
	"${ZSTD_MODULE_DIR}/zfs_zstd.c"
)
target_link_libraries(libzstd PUBLIC libnvpair libpthread)
