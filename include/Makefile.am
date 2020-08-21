SUBDIRS = sys os

COMMON_H = \
	cityhash.h \
	zfeature_common.h \
	zfs_comutil.h \
	zfs_deleg.h \
	zfs_fletcher.h \
	zfs_namecheck.h \
	zfs_prop.h

USER_H = \
	libnvpair.h \
	libuutil_common.h \
	libuutil.h \
	libuutil_impl.h \
	libzfs.h \
	libzfsbootenv.h \
	libzfs_core.h \
	libzfs_impl.h \
	libzutil.h \
	thread_pool.h

if CONFIG_USER
libzfsdir = $(includedir)/libzfs
libzfs_HEADERS = $(COMMON_H) $(USER_H)
endif

if CONFIG_KERNEL
if BUILD_LINUX
kerneldir = @prefix@/src/zfs-$(VERSION)/include
kernel_HEADERS = $(COMMON_H)
endif
endif
