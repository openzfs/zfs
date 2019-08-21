KERNEL_H = \
	$(top_srcdir)/include/os/linux/zfs/sys/policy.h \
	$(top_srcdir)/include/os/linux/zfs/sys/zfs_ctldir.h \
	$(top_srcdir)/include/os/linux/zfs/sys/zfs_dir.h \
	$(top_srcdir)/include/os/linux/zfs/sys/zfs_vfsops.h \
	$(top_srcdir)/include/os/linux/zfs/sys/zfs_vnops.h \
	$(top_srcdir)/include/os/linux/zfs/sys/zpl.h

if CONFIG_KERNEL
kerneldir = @prefix@/src/zfs-$(VERSION)/include/sys
kernel_HEADERS = $(KERNEL_H)
endif
