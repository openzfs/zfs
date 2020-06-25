KERNEL_H = \
	freebsd_crypto.h \
	sha2.h \
	vdev_os.h \
	zfs_context_os.h \
	zfs_ctldir.h \
	zfs_dir.h \
	zfs_ioctl_compat.h \
	zfs_vfsops.h \
	zfs_vnops.h \
	zfs_znode_impl.h \
	zpl.h

if CONFIG_KERNEL
kerneldir = @prefix@/src/zfs-$(VERSION)/include/sys
kernel_HEADERS = $(KERNEL_H)
endif
