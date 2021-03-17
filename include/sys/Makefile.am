SUBDIRS = fm fs crypto lua sysevent zstd

COMMON_H = \
	abd.h \
	abd_impl.h \
	aggsum.h \
	arc.h \
	arc_impl.h \
	avl.h \
	avl_impl.h \
	bitops.h \
	blkptr.h \
	bplist.h \
	bpobj.h \
	bptree.h \
	btree.h \
	bqueue.h \
	dataset_kstats.h \
	dbuf.h \
	ddt.h \
	dmu.h \
	dmu_impl.h \
	dmu_objset.h \
	dmu_recv.h \
	dmu_redact.h \
	dmu_send.h \
	dmu_traverse.h \
	dmu_tx.h \
	dmu_zfetch.h \
	dnode.h \
	dsl_bookmark.h \
	dsl_dataset.h \
	dsl_deadlist.h \
	dsl_deleg.h \
	dsl_destroy.h \
	dsl_dir.h \
	dsl_crypt.h \
	dsl_pool.h \
	dsl_prop.h \
	dsl_scan.h \
	dsl_synctask.h \
	dsl_userhold.h \
	edonr.h \
	efi_partition.h \
	frame.h \
	hkdf.h \
	metaslab.h \
	metaslab_impl.h \
	mmp.h \
	mntent.h \
	mod.h \
	multilist.h \
	note.h \
	nvpair.h \
	nvpair_impl.h \
	objlist.h \
	pathname.h \
	qat.h \
	range_tree.h \
	rrwlock.h \
	sa.h \
	sa_impl.h \
	skein.h \
	spa_boot.h \
	spa_checkpoint.h \
	spa_log_spacemap.h \
	space_map.h \
	space_reftree.h \
	spa.h \
	spa_impl.h \
	spa_checksum.h \
	sysevent.h \
	txg.h \
	txg_impl.h \
	u8_textprep_data.h \
	u8_textprep.h \
	uberblock.h \
	uberblock_impl.h \
	uio_impl.h \
	unique.h \
	uuid.h \
	vdev_disk.h \
	vdev_file.h \
	vdev.h \
	vdev_draid.h \
	vdev_impl.h \
	vdev_indirect_births.h \
	vdev_indirect_mapping.h \
	vdev_initialize.h \
	vdev_raidz.h \
	vdev_raidz_impl.h \
	vdev_rebuild.h \
	vdev_removal.h \
	vdev_trim.h \
	xvattr.h \
	zap.h \
	zap_impl.h \
	zap_leaf.h \
	zcp.h \
	zcp_global.h \
	zcp_iter.h \
	zcp_prop.h \
	zcp_set.h \
	zfeature.h \
	zfs_acl.h \
	zfs_bootenv.h \
	zfs_context.h \
	zfs_debug.h \
	zfs_delay.h \
	zfs_file.h \
	zfs_fuid.h \
	zfs_project.h \
	zfs_quota.h \
	zfs_racct.h \
	zfs_ratelimit.h \
	zfs_refcount.h \
	zfs_rlock.h \
	zfs_sa.h \
	zfs_stat.h \
	zfs_sysfs.h \
	zfs_vfsops.h \
	zfs_vnops.h \
	zfs_znode.h \
	zil.h \
	zil_impl.h \
	zio_checksum.h \
	zio_compress.h \
	zio_crypt.h \
	zio.h \
	zio_impl.h \
	zio_priority.h \
	zrlock.h \
	zthr.h

KERNEL_H = \
	zfs_ioctl.h \
	zfs_ioctl_impl.h \
	zfs_onexit.h \
	zvol.h \
	zvol_impl.h

if CONFIG_USER
libzfsdir = $(includedir)/libzfs/sys
libzfs_HEADERS = $(COMMON_H)
endif

if CONFIG_KERNEL
if BUILD_LINUX
kerneldir = @prefix@/src/zfs-$(VERSION)/include/sys
kernel_HEADERS = $(COMMON_H) $(KERNEL_H)
endif
endif
