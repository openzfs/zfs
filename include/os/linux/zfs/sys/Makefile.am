KERNEL_H = \
	policy.h \
	sha2.h \
	trace_acl.h \
	trace_arc.h \
	trace_common.h \
	trace_zfs.h \
	trace_dbgmsg.h \
	trace_dbuf.h \
	trace_dmu.h \
	trace_dnode.h \
	trace_multilist.h \
	trace_rrwlock.h \
	trace_txg.h \
	trace_vdev.h \
	trace_zil.h \
	trace_zio.h \
	trace_zrlock.h \
	zfs_bootenv_os.h \
	zfs_context_os.h \
	zfs_ctldir.h \
	zfs_dir.h \
	zfs_vfsops_os.h \
	zfs_vnops.h \
	zfs_znode_impl.h \
	zpl.h

if CONFIG_KERNEL
kerneldir = @prefix@/src/zfs-$(VERSION)/include/sys
kernel_HEADERS = $(KERNEL_H)
endif
