COMMON_H = \
	$(top_srcdir)/include/sys/zstd/limits.h \
	$(top_srcdir)/include/sys/zstd/stddef.h \
	$(top_srcdir)/include/sys/zstd/stdint.h \
	$(top_srcdir)/include/sys/zstd/stdio.h \
	$(top_srcdir)/include/sys/zstd/stdlib.h \
	$(top_srcdir)/include/sys/zstd/string.h \
	$(top_srcdir)/include/sys/zstd/zstd_kfreebsd.c \
	$(top_srcdir)/include/sys/zstd/zstd_kfreebsd.h \
	$(top_srcdir)/include/sys/zstd/zstd_kmalloc.c

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
