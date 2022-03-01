include $(top_srcdir)/config/Rules.am

VPATH = $(top_srcdir)/module/zstd

# -fno-tree-vectorize is set for gcc in zstd/common/compiler.h
# Set it for other compilers, too.
AM_CFLAGS += -fno-tree-vectorize

noinst_LTLIBRARIES = libzstd.la

KERNEL_C = lib/common/entropy_common.c \
	lib/common/error_private.c \
	lib/common/fse_decompress.c \
	lib/common/pool.c \
	lib/common/zstd_common.c \
	lib/compress/fse_compress.c \
	lib/compress/hist.c \
	lib/compress/huf_compress.c \
	lib/compress/zstd_compress_literals.c \
	lib/compress/zstd_compress_sequences.c \
	lib/compress/zstd_compress_superblock.c \
	lib/compress/zstd_compress.c \
	lib/compress/zstd_double_fast.c \
	lib/compress/zstd_fast.c \
	lib/compress/zstd_lazy.c \
	lib/compress/zstd_ldm.c \
	lib/compress/zstd_opt.c \
	lib/decompress/huf_decompress.c \
	lib/decompress/zstd_ddict.c \
	lib/decompress/zstd_decompress.c \
	lib/decompress/zstd_decompress_block.c \
	zfs_zstd.c

nodist_libzstd_la_SOURCES = $(KERNEL_C)

%.$(OBJEXT):  CFLAGS += -fno-tree-vectorize -include $(top_srcdir)/module/zstd/include/zstd_compat_wrapper.h -Wp,-w
%.l$(OBJEXT): CFLAGS += -fno-tree-vectorize -include $(top_srcdir)/module/zstd/include/zstd_compat_wrapper.h -Wp,-w

zfs_zstd.$(OBJEXT):  CFLAGS += -include $(top_srcdir)/module/zstd/include/zstd_compat_wrapper.h
zfs_zstd.l$(OBJEXT): CFLAGS += -include $(top_srcdir)/module/zstd/include/zstd_compat_wrapper.h

include $(top_srcdir)/config/CppCheck.am
