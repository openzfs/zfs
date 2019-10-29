KERNEL_H = \
	$(top_srcdir)/include/os/linux/zfs/sys/policy.h \
	$(top_srcdir)/include/os/linux/zfs/sys/sha2.h \
	$(top_srcdir)/include/os/linux/zfs/sys/trace_acl.h \
	$(top_srcdir)/include/os/linux/zfs/sys/trace_arc.h \
	$(top_srcdir)/include/os/linux/zfs/sys/trace_common.h \
	$(top_srcdir)/include/os/linux/zfs/sys/trace_defs.h \
	$(top_srcdir)/include/os/linux/zfs/sys/trace_dbgmsg.h \
	$(top_srcdir)/include/os/linux/zfs/sys/trace_dbuf.h \
	$(top_srcdir)/include/os/linux/zfs/sys/trace_dmu.h \
	$(top_srcdir)/include/os/linux/zfs/sys/trace_dnode.h \
	$(top_srcdir)/include/os/linux/zfs/sys/trace_multilist.h \
	$(top_srcdir)/include/os/linux/zfs/sys/trace_rrwlock.h \
	$(top_srcdir)/include/os/linux/zfs/sys/trace_txg.h \
	$(top_srcdir)/include/os/linux/zfs/sys/trace_vdev.h \
	$(top_srcdir)/include/os/linux/zfs/sys/trace_zil.h \
	$(top_srcdir)/include/os/linux/zfs/sys/trace_zio.h \
	$(top_srcdir)/include/os/linux/zfs/sys/trace_zrlock.h \
	$(top_srcdir)/include/os/linux/zfs/sys/zfs_ctldir.h \
	$(top_srcdir)/include/os/linux/zfs/sys/zfs_dir.h \
	$(top_srcdir)/include/os/linux/zfs/sys/zfs_vfsops.h \
	$(top_srcdir)/include/os/linux/zfs/sys/zfs_vnops.h \
	$(top_srcdir)/include/os/linux/zfs/sys/zpl.h

if CONFIG_KERNEL
kerneldir = @prefix@/src/zfs-$(VERSION)/include/sys
kernel_HEADERS = $(KERNEL_H)
endif
