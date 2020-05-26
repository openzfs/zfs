COMMON_H =

KERNEL_H = \
	dcache_compat.h \
	xattr_compat.h \
	vfs_compat.h \
	blkdev_compat.h \
	utsname_compat.h \
	kmap_compat.h \
	percpu_compat.h \
	simd.h \
	simd_x86.h \
	simd_aarch64.h \
	simd_powerpc.h \
	mod_compat.h \
	page_compat.h \
	compiler_compat.h

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
