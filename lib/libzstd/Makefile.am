libzstd_la_CFLAGS  = $(AM_CFLAGS) $(LIBRARY_CFLAGS)
libzstd_la_CPPFLAGS = $(AM_CPPFLAGS) $(LIBZPOOL_CPPFLAGS)

# -fno-tree-vectorize is set for gcc in zstd/common/compiler.h
# Set it for other compilers, too.
libzstd_la_CFLAGS += -fno-tree-vectorize

noinst_LTLIBRARIES += libzstd.la

nodist_libzstd_la_SOURCES = \
	module/zstd/lib/common/entropy_common.c \
	module/zstd/lib/common/error_private.c \
	module/zstd/lib/common/fse_decompress.c \
	module/zstd/lib/common/pool.c \
	module/zstd/lib/common/zstd_common.c \
	module/zstd/lib/compress/fse_compress.c \
	module/zstd/lib/compress/hist.c \
	module/zstd/lib/compress/huf_compress.c \
	module/zstd/lib/compress/zstd_compress_literals.c \
	module/zstd/lib/compress/zstd_compress_sequences.c \
	module/zstd/lib/compress/zstd_compress_superblock.c \
	module/zstd/lib/compress/zstd_compress.c \
	module/zstd/lib/compress/zstd_double_fast.c \
	module/zstd/lib/compress/zstd_fast.c \
	module/zstd/lib/compress/zstd_lazy.c \
	module/zstd/lib/compress/zstd_ldm.c \
	module/zstd/lib/compress/zstd_opt.c \
	module/zstd/lib/decompress/huf_decompress.c \
	module/zstd/lib/decompress/zstd_ddict.c \
	module/zstd/lib/decompress/zstd_decompress.c \
	module/zstd/lib/decompress/zstd_decompress_block.c \
	module/zstd/zfs_zstd.c

libzstd_la_CFLAGS += -include $(top_srcdir)/module/zstd/include/zstd_compat_wrapper.h -fno-tree-vectorize -Wp,-w $(AM_CFLAGS)
