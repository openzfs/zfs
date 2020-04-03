COMMON_H =

KERNEL_H = \
	$(top_srcdir)/include/os/linux/kernel/linux/dcache_compat.h \
	$(top_srcdir)/include/os/linux/kernel/linux/xattr_compat.h \
	$(top_srcdir)/include/os/linux/kernel/linux/vfs_compat.h \
	$(top_srcdir)/include/os/linux/kernel/linux/blkdev_compat.h \
	$(top_srcdir)/include/os/linux/kernel/linux/utsname_compat.h \
	$(top_srcdir)/include/os/linux/kernel/linux/kmap_compat.h \
	$(top_srcdir)/include/os/linux/kernel/linux/simd.h \
	$(top_srcdir)/include/os/linux/kernel/linux/simd_x86.h \
	$(top_srcdir)/include/os/linux/kernel/linux/simd_aarch64.h \
	$(top_srcdir)/include/os/linux/kernel/linux/simd_powerpc.h \
	$(top_srcdir)/include/os/linux/kernel/linux/mod_compat.h \
	$(top_srcdir)/include/os/linux/kernel/linux/page_compat.h \
	$(top_srcdir)/include/os/linux/kernel/linux/compiler_compat.h

USER_H =

EXTRA_DIST = $(COMMON_H) $(KERNEL_H) $(USER_H)

if CONFIG_USER
libzfsdir = $(includedir)/libzfs/linux
libzfs_HEADERS = $(COMMON_H) $(USER_H)
endif

if CONFIG_KERNEL
kerneldir = @prefix@/src/zfs-$(VERSION)/include/linux
kernel_HEADERS = $(COMMON_H) $(KERNEL_H)
endif
