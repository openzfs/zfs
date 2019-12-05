COMMON_H = \
	$(top_srcdir)/include/sys/zstd/bitstream.h \
	$(top_srcdir)/include/sys/zstd/cpu.h \
	$(top_srcdir)/include/sys/zstd/error_private.h \
	$(top_srcdir)/include/sys/zstd/hist.h \
	$(top_srcdir)/include/sys/zstd/mem.h \
	$(top_srcdir)/include/sys/zstd/zstd.h \
	$(top_srcdir)/include/sys/zstd/zstd_double_fast.h \
	$(top_srcdir)/include/sys/zstd/zstd_fast.h \
	$(top_srcdir)/include/sys/zstd/zstd_lazy.h \
	$(top_srcdir)/include/sys/zstd/zstd_opt.h \
	$(top_srcdir)/include/sys/zstd/compiler.h \
	$(top_srcdir)/include/sys/zstd/debug.h \
	$(top_srcdir)/include/sys/zstd/fse.h \
	$(top_srcdir)/include/sys/zstd/huf.h \
	$(top_srcdir)/include/sys/zstd/xxhash.h \
	$(top_srcdir)/include/sys/zstd/zstd_compress_internal.h \
	$(top_srcdir)/include/sys/zstd/zstd_compress_literals.h \
	$(top_srcdir)/include/sys/zstd/zstd_compress_sequences.h \
	$(top_srcdir)/include/sys/zstd/zstd_cwksp.h \
	$(top_srcdir)/include/sys/zstd/zstd_ddict.h \
	$(top_srcdir)/include/sys/zstd/zstd_decompress_internal.h \
	$(top_srcdir)/include/sys/zstd/zstd_decompress_block.h \
	$(top_srcdir)/include/sys/zstd/zstd_errors.h \
	$(top_srcdir)/include/sys/zstd/zstd_internal.h \
	$(top_srcdir)/include/sys/zstd/zstd_ldm.h

KERNEL_H =

USER_H =

EXTRA_DIST = $(COMMON_H) $(KERNEL_H) $(USER_H)

if CONFIG_USER
libzfsdir = $(includedir)/libzfs/sys/zstd
libzfs_HEADERS = $(COMMON_H) $(USER_H)
endif

if CONFIG_KERNEL
kerneldir = @prefix@/src/zfs-$(VERSION)/include/sys/zstd
kernel_HEADERS = $(COMMON_H) $(KERNEL_H)
endif
