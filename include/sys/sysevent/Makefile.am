COMMON_H = \
	eventdefs.h \
	dev.h

if CONFIG_USER
libzfsdir = $(includedir)/libzfs/sys/sysevent
libzfs_HEADERS = $(COMMON_H)
endif

if CONFIG_KERNEL
if BUILD_LINUX
kerneldir = @prefix@/src/zfs-$(VERSION)/include/sys/sysevent
kernel_HEADERS = $(COMMON_H)
endif
endif
