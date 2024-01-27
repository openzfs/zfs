if BUILD_LINUX
include $(srcdir)/%D%/os/linux/Makefile.am
endif
if BUILD_FREEBSD
include $(srcdir)/%D%/os/freebsd/Makefile.am
endif


COMMON_H = \
	cityhash.h \
	zfeature_common.h \
	zfs_comutil.h \
	zfs_deleg.h \
	zfs_fletcher.h \
	zfs_namecheck.h \
	zfs_prop.h \
	\
	sys/abd.h \
	sys/abd_impl.h \
	sys/aggsum.h \
	sys/arc.h \
	sys/arc_impl.h \
	sys/asm_linkage.h \
	sys/avl.h \
	sys/avl_impl.h \
	sys/bitmap.h \
	sys/bitops.h \
	sys/blake3.h \
	sys/blkptr.h \
	sys/bplist.h \
	sys/bpobj.h \
	sys/bptree.h \
	sys/bqueue.h \
	sys/btree.h \
	sys/brt.h \
	sys/brt_impl.h \
	sys/dataset_kstats.h \
	sys/dbuf.h \
	sys/ddt.h \
	sys/dmu.h \
	sys/dmu_impl.h \
	sys/dmu_objset.h \
	sys/dmu_recv.h \
	sys/dmu_redact.h \
	sys/dmu_send.h \
	sys/dmu_traverse.h \
	sys/dmu_tx.h \
	sys/dmu_zfetch.h \
	sys/dnode.h \
	sys/dsl_bookmark.h \
	sys/dsl_crypt.h \
	sys/dsl_dataset.h \
	sys/dsl_deadlist.h \
	sys/dsl_deleg.h \
	sys/dsl_destroy.h \
	sys/dsl_dir.h \
	sys/dsl_pool.h \
	sys/dsl_prop.h \
	sys/dsl_scan.h \
	sys/dsl_synctask.h \
	sys/dsl_userhold.h \
	sys/edonr.h \
	sys/efi_partition.h \
	sys/frame.h \
	sys/hkdf.h \
	sys/metaslab.h \
	sys/metaslab_impl.h \
	sys/mmp.h \
	sys/mntent.h \
	sys/mod.h \
	sys/multilist.h \
	sys/nvpair.h \
	sys/nvpair_impl.h \
	sys/objlist.h \
	sys/pathname.h \
	sys/qat.h \
	sys/range_tree.h \
	sys/rrwlock.h \
	sys/sa.h \
	sys/sa_impl.h \
	sys/sha2.h \
	sys/skein.h \
	sys/spa.h \
	sys/spa_checkpoint.h \
	sys/spa_checksum.h \
	sys/spa_impl.h \
	sys/spa_log_spacemap.h \
	sys/space_map.h \
	sys/space_reftree.h \
	sys/sysevent.h \
	sys/txg.h \
	sys/txg_impl.h \
	sys/u8_textprep.h \
	sys/u8_textprep_data.h \
	sys/uberblock.h \
	sys/uberblock_impl.h \
	sys/uio_impl.h \
	sys/unique.h \
	sys/uuid.h \
	sys/vdev.h \
	sys/vdev_disk.h \
	sys/vdev_draid.h \
	sys/vdev_file.h \
	sys/vdev_impl.h \
	sys/vdev_indirect_births.h \
	sys/vdev_indirect_mapping.h \
	sys/vdev_initialize.h \
	sys/vdev_raidz.h \
	sys/vdev_raidz_impl.h \
	sys/vdev_rebuild.h \
	sys/vdev_removal.h \
	sys/vdev_trim.h \
	sys/xvattr.h \
	sys/zap.h \
	sys/zap_impl.h \
	sys/zap_leaf.h \
	sys/zcp.h \
	sys/zcp_global.h \
	sys/zcp_iter.h \
	sys/zcp_prop.h \
	sys/zcp_set.h \
	sys/zfeature.h \
	sys/zfs_acl.h \
	sys/zfs_bootenv.h \
	sys/zfs_chksum.h \
	sys/zfs_context.h \
	sys/zfs_debug.h \
	sys/zfs_delay.h \
	sys/zfs_file.h \
	sys/zfs_fuid.h \
	sys/zfs_impl.h \
	sys/zfs_project.h \
	sys/zfs_quota.h \
	sys/zfs_racct.h \
	sys/zfs_ratelimit.h \
	sys/zfs_refcount.h \
	sys/zfs_rlock.h \
	sys/zfs_sa.h \
	sys/zfs_stat.h \
	sys/zfs_sysfs.h \
	sys/zfs_vfsops.h \
	sys/zfs_vnops.h \
	sys/zfs_znode.h \
	sys/zil.h \
	sys/zil_impl.h \
	sys/zio.h \
	sys/zio_checksum.h \
	sys/zio_compress.h \
	sys/zio_crypt.h \
	sys/zio_impl.h \
	sys/zio_priority.h \
	sys/zrlock.h \
	sys/zthr.h \
	\
	sys/crypto/api.h \
	sys/crypto/common.h \
	sys/crypto/icp.h \
	\
	sys/fm/protocol.h \
	sys/fm/util.h \
	sys/fm/fs/zfs.h \
	\
	sys/fs/zfs.h \
	\
	sys/lua/lauxlib.h \
	sys/lua/lua.h \
	sys/lua/luaconf.h \
	sys/lua/lualib.h \
	\
	sys/sysevent/dev.h \
	sys/sysevent/eventdefs.h \
	\
	sys/zstd/zstd.h


KERNEL_H = \
	sys/zfs_ioctl.h \
	sys/zfs_ioctl_impl.h \
	sys/zfs_onexit.h \
	sys/zvol.h \
	sys/zvol_impl.h


USER_H = \
	libnvpair.h \
	libuutil.h \
	libuutil_common.h \
	libuutil_impl.h \
	libzdb.h \
	libzfs.h \
	libzfs_core.h \
	libzfsbootenv.h \
	libzutil.h \
	thread_pool.h


if CONFIG_USER
libzfsdir = $(includedir)/libzfs
nobase_libzfs_HEADERS = $(COMMON_H) $(USER_H)
endif

kerneldir = $(prefix)/src/zfs-$(VERSION)/include
if CONFIG_KERNEL
if BUILD_LINUX
nobase_kernel_HEADERS = $(COMMON_H) $(KERNEL_H)
endif
endif
