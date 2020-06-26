COMMON_H = \
	api.h \
	common.h \
	icp.h

if CONFIG_USER
libzfsdir = $(includedir)/libzfs/sys/crypto
libzfs_HEADERS = $(COMMON_H)
endif

if CONFIG_KERNEL
if BUILD_LINUX
kerneldir = @prefix@/src/zfs-$(VERSION)/include/sys/crypto
kernel_HEADERS = $(COMMON_H)
endif
endif
