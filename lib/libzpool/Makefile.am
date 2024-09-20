include $(srcdir)/%D%/include/Makefile.am

libzpool_la_CFLAGS  = $(AM_CFLAGS) $(KERNEL_CFLAGS) $(LIBRARY_CFLAGS)
libzpool_la_CFLAGS += $(ZLIB_CFLAGS)

libzpool_la_CPPFLAGS  = $(AM_CPPFLAGS) $(LIBZPOOL_CPPFLAGS)
libzpool_la_CPPFLAGS += -I$(srcdir)/include/os/@ac_system_l@/zfs
libzpool_la_CPPFLAGS += -DLIB_ZPOOL_BUILD

lib_LTLIBRARIES += libzpool.la
CPPCHECKTARGETS += libzpool.la

dist_libzpool_la_SOURCES = \
	%D%/abd_os.c \
	%D%/arc_os.c \
	%D%/kernel.c \
	%D%/taskq.c \
	%D%/util.c \
	%D%/vdev_label_os.c \
	%D%/zfs_racct.c \
	%D%/zfs_debug.c

nodist_libzpool_la_SOURCES = \
	module/lua/lapi.c \
	module/lua/lauxlib.c \
	module/lua/lbaselib.c \
	module/lua/lcode.c \
	module/lua/lcompat.c \
	module/lua/lcorolib.c \
	module/lua/lctype.c \
	module/lua/ldebug.c \
	module/lua/ldo.c \
	module/lua/lfunc.c \
	module/lua/lgc.c \
	module/lua/llex.c \
	module/lua/lmem.c \
	module/lua/lobject.c \
	module/lua/lopcodes.c \
	module/lua/lparser.c \
	module/lua/lstate.c \
	module/lua/lstring.c \
	module/lua/lstrlib.c \
	module/lua/ltable.c \
	module/lua/ltablib.c \
	module/lua/ltm.c \
	module/lua/lvm.c \
	module/lua/lzio.c \
	\
	module/os/linux/zfs/vdev_file.c \
	module/os/linux/zfs/zio_crypt.c \
	\
	module/zcommon/cityhash.c \
	module/zcommon/simd_stat.c \
	module/zcommon/zfeature_common.c \
	module/zcommon/zfs_comutil.c \
	module/zcommon/zfs_deleg.c \
	module/zcommon/zfs_fletcher.c \
	module/zcommon/zfs_fletcher_aarch64_neon.c \
	module/zcommon/zfs_fletcher_avx512.c \
	module/zcommon/zfs_fletcher_intel.c \
	module/zcommon/zfs_fletcher_sse.c \
	module/zcommon/zfs_fletcher_superscalar.c \
	module/zcommon/zfs_fletcher_superscalar4.c \
	module/zcommon/zfs_namecheck.c \
	module/zcommon/zfs_prop.c \
	module/zcommon/zfs_valstr.c \
	module/zcommon/zpool_prop.c \
	module/zcommon/zprop_common.c \
	\
	module/zfs/abd.c \
	module/zfs/aggsum.c \
	module/zfs/arc.c \
	module/zfs/blake3_zfs.c \
	module/zfs/blkptr.c \
	module/zfs/bplist.c \
	module/zfs/bpobj.c \
	module/zfs/bptree.c \
	module/zfs/bqueue.c \
	module/zfs/btree.c \
	module/zfs/brt.c \
	module/zfs/dbuf.c \
	module/zfs/dbuf_stats.c \
	module/zfs/ddt.c \
	module/zfs/ddt_log.c \
	module/zfs/ddt_stats.c \
	module/zfs/ddt_zap.c \
	module/zfs/dmu.c \
	module/zfs/dmu_diff.c \
	module/zfs/dmu_direct.c \
	module/zfs/dmu_object.c \
	module/zfs/dmu_objset.c \
	module/zfs/dmu_recv.c \
	module/zfs/dmu_redact.c \
	module/zfs/dmu_send.c \
	module/zfs/dmu_traverse.c \
	module/zfs/dmu_tx.c \
	module/zfs/dmu_zfetch.c \
	module/zfs/dnode.c \
	module/zfs/dnode_sync.c \
	module/zfs/dsl_bookmark.c \
	module/zfs/dsl_crypt.c \
	module/zfs/dsl_dataset.c \
	module/zfs/dsl_deadlist.c \
	module/zfs/dsl_deleg.c \
	module/zfs/dsl_destroy.c \
	module/zfs/dsl_dir.c \
	module/zfs/dsl_pool.c \
	module/zfs/dsl_prop.c \
	module/zfs/dsl_scan.c \
	module/zfs/dsl_synctask.c \
	module/zfs/dsl_userhold.c \
	module/zfs/edonr_zfs.c \
	module/zfs/fm.c \
	module/zfs/gzip.c \
	module/zfs/hkdf.c \
	module/zfs/lz4.c \
	module/zfs/lz4_zfs.c \
	module/zfs/lzjb.c \
	module/zfs/metaslab.c \
	module/zfs/mmp.c \
	module/zfs/multilist.c \
	module/zfs/objlist.c \
	module/zfs/pathname.c \
	module/zfs/range_tree.c \
	module/zfs/refcount.c \
	module/zfs/rrwlock.c \
	module/zfs/sa.c \
	module/zfs/sha2_zfs.c \
	module/zfs/skein_zfs.c \
	module/zfs/spa.c \
	module/zfs/spa_checkpoint.c \
	module/zfs/spa_config.c \
	module/zfs/spa_errlog.c \
	module/zfs/spa_history.c \
	module/zfs/spa_log_spacemap.c \
	module/zfs/spa_misc.c \
	module/zfs/spa_stats.c \
	module/zfs/space_map.c \
	module/zfs/space_reftree.c \
	module/zfs/txg.c \
	module/zfs/uberblock.c \
	module/zfs/unique.c \
	module/zfs/vdev.c \
	module/zfs/vdev_draid.c \
	module/zfs/vdev_draid_rand.c \
	module/zfs/vdev_indirect.c \
	module/zfs/vdev_indirect_births.c \
	module/zfs/vdev_indirect_mapping.c \
	module/zfs/vdev_initialize.c \
	module/zfs/vdev_label.c \
	module/zfs/vdev_mirror.c \
	module/zfs/vdev_missing.c \
	module/zfs/vdev_queue.c \
	module/zfs/vdev_raidz.c \
	module/zfs/vdev_raidz_math.c \
	module/zfs/vdev_raidz_math_aarch64_neon.c \
	module/zfs/vdev_raidz_math_aarch64_neonx2.c \
	module/zfs/vdev_raidz_math_avx2.c \
	module/zfs/vdev_raidz_math_avx512bw.c \
	module/zfs/vdev_raidz_math_avx512f.c \
	module/zfs/vdev_raidz_math_powerpc_altivec.c \
	module/zfs/vdev_raidz_math_scalar.c \
	module/zfs/vdev_raidz_math_sse2.c \
	module/zfs/vdev_raidz_math_ssse3.c \
	module/zfs/vdev_rebuild.c \
	module/zfs/vdev_removal.c \
	module/zfs/vdev_root.c \
	module/zfs/vdev_trim.c \
	module/zfs/zap.c \
	module/zfs/zap_leaf.c \
	module/zfs/zap_micro.c \
	module/zfs/zcp.c \
	module/zfs/zcp_get.c \
	module/zfs/zcp_global.c \
	module/zfs/zcp_iter.c \
	module/zfs/zcp_set.c \
	module/zfs/zcp_synctask.c \
	module/zfs/zfeature.c \
	module/zfs/zfs_byteswap.c \
	module/zfs/zfs_chksum.c \
	module/zfs/zfs_fm.c \
	module/zfs/zfs_fuid.c \
	module/zfs/zfs_ratelimit.c \
	module/zfs/zfs_rlock.c \
	module/zfs/zfs_sa.c \
	module/zfs/zfs_znode.c \
	module/zfs/zil.c \
	module/zfs/zio.c \
	module/zfs/zio_checksum.c \
	module/zfs/zio_compress.c \
	module/zfs/zio_inject.c \
	module/zfs/zle.c \
	module/zfs/zrlock.c \
	module/zfs/zthr.c

libzpool_la_LIBADD = \
	libicp.la \
	libunicode.la \
	libnvpair.la \
	libzstd.la \
	libzutil.la

libzpool_la_LIBADD += $(LIBCLOCK_GETTIME) $(ZLIB_LIBS) -ldl -lm

libzpool_la_LDFLAGS = -pthread

if !ASAN_ENABLED
libzpool_la_LDFLAGS += -Wl,-z,defs
endif

if BUILD_FREEBSD
libzpool_la_LIBADD += -lgeom
endif

libzpool_la_LDFLAGS += -version-info 5:0:0

if TARGET_CPU_POWERPC
module/zfs/libzpool_la-vdev_raidz_math_powerpc_altivec.$(OBJEXT) : CFLAGS += -maltivec
module/zfs/libzpool_la-vdev_raidz_math_powerpc_altivec.l$(OBJEXT): CFLAGS += -maltivec
endif
