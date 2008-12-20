include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I${top_srcdir}/lib/libzpool/include \
	-I${top_srcdir}/lib/libspl/include \
	-I${top_srcdir}/lib/libnvpair/include \
	-I${top_srcdir}/module/zcommon/include \
	-I${top_srcdir}/module/nvpair/include \
	-I${top_srcdir}/module/avl/include \
	-I${top_srcdir}/module/zfs/include

lib_LTLIBRARIES = libzpool.la

libzpool_la_SOURCES = \
	${top_srcdir}/lib/libzpool/kernel.c \
	${top_srcdir}/lib/libzpool/taskq.c \
	${top_srcdir}/lib/libzpool/util.c \
	${top_srcdir}/module/zcommon/zfs_comutil.c \
	${top_srcdir}/module/zcommon/zfs_deleg.c \
	${top_srcdir}/module/zcommon/zfs_namecheck.c \
	${top_srcdir}/module/zcommon/zfs_prop.c \
	${top_srcdir}/module/zcommon/zpool_prop.c \
	${top_srcdir}/module/zcommon/zprop_common.c \
	${top_srcdir}/module/zcommon/include/sys/fm/fs/zfs.h \
	${top_srcdir}/module/zcommon/include/sys/fs \
	${top_srcdir}/module/zcommon/include/sys/fs/zfs.h \
	${top_srcdir}/module/zcommon/include/zfs_comutil.h \
	${top_srcdir}/module/zcommon/include/zfs_deleg.h \
	${top_srcdir}/module/zcommon/include/zfs_namecheck.h \
	${top_srcdir}/module/zcommon/include/zfs_prop.h \
	${top_srcdir}/module/zfs/dmu.c \
	${top_srcdir}/module/zfs/dmu_objset.c \
	${top_srcdir}/module/zfs/dmu_object.c \
	${top_srcdir}/module/zfs/txg.c \
	${top_srcdir}/module/zfs/dmu_tx.c \
	${top_srcdir}/module/zfs/dnode.c \
	${top_srcdir}/module/zfs/dnode_sync.c \
	${top_srcdir}/module/zfs/dbuf.c \
	${top_srcdir}/module/zfs/dmu_zfetch.c \
	${top_srcdir}/module/zfs/refcount.c \
	${top_srcdir}/module/zfs/dmu_send.c \
	${top_srcdir}/module/zfs/dsl_dir.c \
	${top_srcdir}/module/zfs/dsl_dataset.c \
	${top_srcdir}/module/zfs/dsl_pool.c \
	${top_srcdir}/module/zfs/dsl_prop.c \
	${top_srcdir}/module/zfs/dsl_deleg.c \
	${top_srcdir}/module/zfs/dsl_synctask.c \
	${top_srcdir}/module/zfs/dsl_scrub.c \
	${top_srcdir}/module/zfs/unique.c \
	${top_srcdir}/module/zfs/zap.c \
	${top_srcdir}/module/zfs/zap_leaf.c \
	${top_srcdir}/module/zfs/zap_micro.c \
	${top_srcdir}/module/zfs/zil.c \
	${top_srcdir}/module/zfs/arc.c \
	${top_srcdir}/module/zfs/spa.c \
	${top_srcdir}/module/zfs/spa_misc.c \
	${top_srcdir}/module/zfs/spa_config.c \
	${top_srcdir}/module/zfs/spa_errlog.c \
	${top_srcdir}/module/zfs/spa_history.c \
	${top_srcdir}/module/zfs/zfs_fm.c \
	${top_srcdir}/module/zfs/zfs_byteswap.c \
	${top_srcdir}/module/zfs/zio.c \
	${top_srcdir}/module/zfs/zio_checksum.c \
	${top_srcdir}/module/zfs/fletcher.c \
	${top_srcdir}/module/zfs/sha256.c \
	${top_srcdir}/module/zfs/zio_compress.c \
	${top_srcdir}/module/zfs/lzjb.c \
	${top_srcdir}/module/zfs/gzip.c \
	${top_srcdir}/module/zfs/uberblock.c \
	${top_srcdir}/module/zfs/bplist.c \
	${top_srcdir}/module/zfs/metaslab.c \
	${top_srcdir}/module/zfs/space_map.c \
	${top_srcdir}/module/zfs/zio_inject.c \
	${top_srcdir}/module/zfs/vdev.c \
	${top_srcdir}/module/zfs/vdev_file.c \
	${top_srcdir}/module/zfs/vdev_mirror.c \
	${top_srcdir}/module/zfs/vdev_raidz.c \
	${top_srcdir}/module/zfs/vdev_root.c \
	${top_srcdir}/module/zfs/vdev_missing.c \
	${top_srcdir}/module/zfs/vdev_label.c \
	${top_srcdir}/module/zfs/vdev_cache.c \
	${top_srcdir}/module/zfs/vdev_queue.c \
	${top_srcdir}/module/zfs/dmu_traverse.c
