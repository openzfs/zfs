include $(top_srcdir)/config/Rules.am

AM_CFLAGS += $(DEBUG_STACKFLAGS) $(FRAME_LARGER_THAN)

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

lib_LTLIBRARIES = libzpool.la

libzpool_la_SOURCES = \
	$(top_srcdir)/lib/libzpool/kernel.c \
	$(top_srcdir)/lib/libzpool/taskq.c \
	$(top_srcdir)/lib/libzpool/util.c \
	$(top_srcdir)/module/zcommon/zfs_comutil.c \
	$(top_srcdir)/module/zcommon/zfs_deleg.c \
	$(top_srcdir)/module/zcommon/zfs_fletcher.c \
	$(top_srcdir)/module/zcommon/zfs_namecheck.c \
	$(top_srcdir)/module/zcommon/zfs_prop.c \
	$(top_srcdir)/module/zcommon/zfs_uio.c \
	$(top_srcdir)/module/zcommon/zpool_prop.c \
	$(top_srcdir)/module/zcommon/zprop_common.c \
	$(top_srcdir)/module/zfs/arc.c \
	$(top_srcdir)/module/zfs/blkptr.c \
	$(top_srcdir)/module/zfs/bplist.c \
	$(top_srcdir)/module/zfs/bpobj.c \
	$(top_srcdir)/module/zfs/bptree.c \
	$(top_srcdir)/module/zfs/dbuf.c \
	$(top_srcdir)/module/zfs/dbuf_stats.c \
	$(top_srcdir)/module/zfs/ddt.c \
	$(top_srcdir)/module/zfs/ddt_zap.c \
	$(top_srcdir)/module/zfs/dmu.c \
	$(top_srcdir)/module/zfs/dmu_diff.c \
	$(top_srcdir)/module/zfs/dmu_object.c \
	$(top_srcdir)/module/zfs/dmu_objset.c \
	$(top_srcdir)/module/zfs/dmu_send.c \
	$(top_srcdir)/module/zfs/dmu_traverse.c \
	$(top_srcdir)/module/zfs/dmu_tx.c \
	$(top_srcdir)/module/zfs/dmu_zfetch.c \
	$(top_srcdir)/module/zfs/dnode.c \
	$(top_srcdir)/module/zfs/dnode_sync.c \
	$(top_srcdir)/module/zfs/dsl_bookmark.c \
	$(top_srcdir)/module/zfs/dsl_dataset.c \
	$(top_srcdir)/module/zfs/dsl_deadlist.c \
	$(top_srcdir)/module/zfs/dsl_deleg.c \
	$(top_srcdir)/module/zfs/dsl_dir.c \
	$(top_srcdir)/module/zfs/dsl_pool.c \
	$(top_srcdir)/module/zfs/dsl_prop.c \
	$(top_srcdir)/module/zfs/dsl_scan.c \
	$(top_srcdir)/module/zfs/dsl_synctask.c \
	$(top_srcdir)/module/zfs/dsl_destroy.c \
	$(top_srcdir)/module/zfs/dsl_userhold.c \
	$(top_srcdir)/module/zfs/fm.c \
	$(top_srcdir)/module/zfs/gzip.c \
	$(top_srcdir)/module/zfs/lzjb.c \
	$(top_srcdir)/module/zfs/lz4.c \
	$(top_srcdir)/module/zfs/metaslab.c \
	$(top_srcdir)/module/zfs/range_tree.c \
	$(top_srcdir)/module/zfs/refcount.c \
	$(top_srcdir)/module/zfs/rrwlock.c \
	$(top_srcdir)/module/zfs/sa.c \
	$(top_srcdir)/module/zfs/sha256.c \
	$(top_srcdir)/module/zfs/spa.c \
	$(top_srcdir)/module/zfs/spa_boot.c \
	$(top_srcdir)/module/zfs/spa_config.c \
	$(top_srcdir)/module/zfs/spa_errlog.c \
	$(top_srcdir)/module/zfs/spa_history.c \
	$(top_srcdir)/module/zfs/spa_misc.c \
	$(top_srcdir)/module/zfs/spa_stats.c \
	$(top_srcdir)/module/zfs/space_map.c \
	$(top_srcdir)/module/zfs/space_reftree.c \
	$(top_srcdir)/module/zfs/txg.c \
	$(top_srcdir)/module/zfs/uberblock.c \
	$(top_srcdir)/module/zfs/unique.c \
	$(top_srcdir)/module/zfs/vdev.c \
	$(top_srcdir)/module/zfs/vdev_cache.c \
	$(top_srcdir)/module/zfs/vdev_file.c \
	$(top_srcdir)/module/zfs/vdev_label.c \
	$(top_srcdir)/module/zfs/vdev_mirror.c \
	$(top_srcdir)/module/zfs/vdev_missing.c \
	$(top_srcdir)/module/zfs/vdev_queue.c \
	$(top_srcdir)/module/zfs/vdev_raidz.c \
	$(top_srcdir)/module/zfs/vdev_root.c \
	$(top_srcdir)/module/zfs/zap.c \
	$(top_srcdir)/module/zfs/zap_leaf.c \
	$(top_srcdir)/module/zfs/zap_micro.c \
	$(top_srcdir)/module/zfs/zfeature.c \
	$(top_srcdir)/module/zfs/zfeature_common.c \
	$(top_srcdir)/module/zfs/zfs_byteswap.c \
	$(top_srcdir)/module/zfs/zfs_debug.c \
	$(top_srcdir)/module/zfs/zfs_fm.c \
	$(top_srcdir)/module/zfs/zfs_fuid.c \
	$(top_srcdir)/module/zfs/zfs_sa.c \
	$(top_srcdir)/module/zfs/zfs_znode.c \
	$(top_srcdir)/module/zfs/zil.c \
	$(top_srcdir)/module/zfs/zio.c \
	$(top_srcdir)/module/zfs/zio_checksum.c \
	$(top_srcdir)/module/zfs/zio_compress.c \
	$(top_srcdir)/module/zfs/zio_inject.c \
	$(top_srcdir)/module/zfs/zle.c \
	$(top_srcdir)/module/zfs/zrlock.c

libzpool_la_LIBADD = \
	$(top_builddir)/lib/libunicode/libunicode.la \
	$(top_builddir)/lib/libuutil/libuutil.la \
	$(top_builddir)/lib/libnvpair/libnvpair.la

libzpool_la_LIBADD += $(ZLIB)
libzpool_la_LDFLAGS = -version-info 2:0:0

EXTRA_DIST = \
	$(top_srcdir)/module/zfs/vdev_disk.c \
	$(top_srcdir)/module/zfs/zfs_acl.c \
	$(top_srcdir)/module/zfs/zfs_ctldir.c \
	$(top_srcdir)/module/zfs/zfs_dir.c \
	$(top_srcdir)/module/zfs/zfs_ioctl.c \
	$(top_srcdir)/module/zfs/zfs_log.c \
	$(top_srcdir)/module/zfs/zfs_onexit.c \
	$(top_srcdir)/module/zfs/zfs_replay.c \
	$(top_srcdir)/module/zfs/zfs_rlock.c \
	$(top_srcdir)/module/zfs/zfs_vfsops.c \
	$(top_srcdir)/module/zfs/zfs_vnops.c \
	$(top_srcdir)/module/zfs/zpl_ctldir.c \
	$(top_srcdir)/module/zfs/zpl_export.c \
	$(top_srcdir)/module/zfs/zpl_file.c \
	$(top_srcdir)/module/zfs/zpl_inode.c \
	$(top_srcdir)/module/zfs/zpl_super.c \
	$(top_srcdir)/module/zfs/zpl_xattr.c \
	$(top_srcdir)/module/zfs/zvol.c \
	$(top_srcdir)/module/zpios/pios.c
