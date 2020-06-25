COMMON_H = \
	zfs.h

if CONFIG_USER
libzfsdir = $(includedir)/libzfs/sys/fs
libzfs_HEADERS = $(COMMON_H)
endif

if CONFIG_KERNEL
kerneldir = @prefix@/src/zfs-$(VERSION)/include/sys/fs
kernel_HEADERS = $(COMMON_H)
endif
