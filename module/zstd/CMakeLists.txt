
use_clang()

wdk_add_library(zstdkern
	lib/common/entropy_common.c
	lib/common/error_private.c
	lib/common/fse_decompress.c
	lib/common/pool.c
	lib/common/zstd_common.c
	lib/compress/fse_compress.c
	lib/compress/hist.c
	lib/compress/huf_compress.c
	lib/compress/zstd_compress_literals.c
	lib/compress/zstd_compress_sequences.c
	lib/compress/zstd_compress_superblock.c
	lib/compress/zstd_compress.c
	lib/compress/zstd_double_fast.c
	lib/compress/zstd_fast.c
	lib/compress/zstd_lazy.c
	lib/compress/zstd_ldm.c
	lib/compress/zstd_opt.c
	lib/compress/zstd_preSplit.c
	lib/decompress/huf_decompress.c
	lib/decompress/huf_decompress_amd64.S
	lib/decompress/zstd_ddict.c
	lib/decompress/zstd_decompress.c
	lib/decompress/zstd_decompress_block.c
	zfs_zstd.c
)

# Work around zstd's #define of calloc.
target_compile_definitions(zstdkern PRIVATE -D_CRT_ALLOCATION_DEFINED -DZSTD_NO_INTRINSICS -DNO_PREFETCH)
target_compile_options(zstdkern PRIVATE /FI${CMAKE_SOURCE_DIR}/module/zstd/include/zstd_compat_wrapper.h)

target_include_directories(zstdkern BEFORE PRIVATE "include")
