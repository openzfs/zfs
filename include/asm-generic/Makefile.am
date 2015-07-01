COMMON_H = \
	$(top_srcdir)/include/asm-generic/sha256.h

KERNEL_H =

USER_H =

EXTRA_DIST = $(COMMON_H) $(KERNEL_H) $(USER_H)

if CONFIG_USER
libzfsdir = $(includedir)/libzfs/linux
libzfs_HEADERS = $(COMMON_H) $(USER_H)
endif

if CONFIG_KERNEL
kerneldir = /usr/src/zfs-$(VERSION)/include/linux
kernel_HEADERS = $(COMMON_H) $(KERNEL_H)
endif
