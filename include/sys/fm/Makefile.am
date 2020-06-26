SUBDIRS = fs

COMMON_H = \
	protocol.h \
	util.h

if CONFIG_USER
libzfsdir = $(includedir)/libzfs/sys/fm
libzfs_HEADERS = $(COMMON_H)
endif

if CONFIG_KERNEL
if BUILD_LINUX
kerneldir = @prefix@/src/zfs-$(VERSION)/include/sys/fm
kernel_HEADERS = $(COMMON_H)
endif
endif
