COMMON_H = \
	$(top_srcdir)/include/sys/zstd/zstd.h

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
