include $(top_srcdir)/config/Rules.am

VPATH = \
	$(top_srcdir)/module/zfs \
	$(top_srcdir)/module/zcommon \
	$(top_srcdir)/module/lua \
	$(top_srcdir)/module/os/linux/zfs \
	$(top_srcdir)/lib/libzpool

if BUILD_FREEBSD
DEFAULT_INCLUDES += -I$(top_srcdir)/include/os/freebsd/zfs
endif
if BUILD_LINUX
DEFAULT_INCLUDES += -I$(top_srcdir)/include/os/linux/zfs
endif

# Unconditionally enable debugging for libzpool
AM_CPPFLAGS += -DDEBUG -UNDEBUG -DZFS_DEBUG

# Suppress unused but set variable warnings often due to ASSERTs
AM_CFLAGS += $(NO_UNUSED_BUT_SET_VARIABLE)

# Includes kernel code generate warnings for large stack frames
AM_CFLAGS += $(FRAME_LARGER_THAN)

AM_CFLAGS += $(ZLIB_CFLAGS)

AM_CFLAGS += -DLIB_ZPOOL_BUILD

lib_LTLIBRARIES = libzpool.la

USER_C = \
	kernel.c \
	taskq.c \
	util.c

KERNEL_C = \
	zfeature_common.c \
	zfs_comutil.c \
	zfs_deleg.c \
	zfs_fletcher.c \
	zfs_fletcher_aarch64_neon.c \
	zfs_fletcher_avx512.c \
	zfs_fletcher_intel.c \
	zfs_fletcher_sse.c \
	zfs_fletcher_superscalar.c \
	zfs_fletcher_superscalar4.c \
	zfs_namecheck.c \
	zfs_prop.c \
	zpool_prop.c \
	zprop_common.c \
	abd.c \
	abd_os.c \
	aggsum.c \
	arc.c \
	arc_os.c \
	blkptr.c \
	bplist.c \
	bpobj.c \
	bptree.c \
	bqueue.c \
	btree.c \
	cityhash.c \
	dbuf.c \
	dbuf_stats.c \
	ddt.c \
	ddt_zap.c \
	dmu.c \
	dmu_diff.c \
	dmu_object.c \
	dmu_objset.c \
	dmu_recv.c \
	dmu_redact.c \
	dmu_send.c \
	dmu_traverse.c \
	dmu_tx.c \
	dmu_zfetch.c \
	dnode.c \
	dnode_sync.c \
	dsl_bookmark.c \
	dsl_crypt.c \
	dsl_dataset.c \
	dsl_deadlist.c \
	dsl_deleg.c \
	dsl_destroy.c \
	dsl_dir.c \
	dsl_pool.c \
	dsl_prop.c \
	dsl_scan.c \
	dsl_synctask.c \
	dsl_userhold.c \
	edonr_zfs.c \
	fm.c \
	gzip.c \
	hkdf.c \
	lz4.c \
	lzjb.c \
	metaslab.c \
	mmp.c \
	multilist.c \
	objlist.c \
	pathname.c \
	range_tree.c \
	refcount.c \
	rrwlock.c \
	sa.c \
	sha256.c \
	skein_zfs.c \
	spa.c \
	spa_boot.c \
	spa_checkpoint.c \
	spa_config.c \
	spa_errlog.c \
	spa_history.c \
	spa_log_spacemap.c \
	spa_misc.c \
	spa_stats.c \
	space_map.c \
	space_reftree.c \
	trace.c \
	txg.c \
	uberblock.c \
	unique.c \
	vdev.c \
	vdev_cache.c \
	vdev_draid.c \
	vdev_draid_rand.c \
	vdev_file.c \
	vdev_indirect.c \
	vdev_indirect_births.c \
	vdev_indirect_mapping.c \
	vdev_initialize.c \
	vdev_label.c \
	vdev_mirror.c \
	vdev_missing.c \
	vdev_queue.c \
	vdev_raidz.c \
	vdev_raidz_math.c \
	vdev_raidz_math_aarch64_neon.c \
	vdev_raidz_math_aarch64_neonx2.c \
	vdev_raidz_math_avx2.c \
	vdev_raidz_math_avx512bw.c \
	vdev_raidz_math_avx512f.c \
	vdev_raidz_math_powerpc_altivec.c \
	vdev_raidz_math_scalar.c \
	vdev_raidz_math_sse2.c \
	vdev_raidz_math_ssse3.c \
	vdev_rebuild.c \
	vdev_removal.c \
	vdev_root.c \
	vdev_trim.c \
	zap.c \
	zap_leaf.c \
	zap_micro.c \
	zcp.c \
	zcp_get.c \
	zcp_global.c \
	zcp_iter.c \
	zcp_set.c \
	zcp_synctask.c \
	zfeature.c \
	zfs_byteswap.c \
	zfs_debug.c \
	zfs_fm.c \
	zfs_fuid.c \
	zfs_racct.c \
	zfs_sa.c \
	zfs_ratelimit.c \
	zfs_rlock.c \
	zfs_znode.c \
	zil.c \
	zio.c \
	zio_checksum.c \
	zio_compress.c \
	zio_crypt.c \
	zio_inject.c \
	zle.c \
	zrlock.c \
	zthr.c

LUA_C = \
	lapi.c \
	lauxlib.c \
	lbaselib.c \
	lcode.c \
	lcompat.c \
	lcorolib.c \
	lctype.c \
	ldebug.c \
	ldo.c \
	lfunc.c \
	lgc.c \
	llex.c \
	lmem.c \
	lobject.c \
	lopcodes.c \
	lparser.c \
	lstate.c \
	lstring.c \
	lstrlib.c \
	ltable.c \
	ltablib.c \
	ltm.c \
	lvm.c \
	lzio.c

dist_libzpool_la_SOURCES = \
	$(USER_C)

nodist_libzpool_la_SOURCES = \
	$(KERNEL_C) \
	$(LUA_C)

libzpool_la_LIBADD = \
	$(abs_top_builddir)/lib/libicp/libicp.la \
	$(abs_top_builddir)/lib/libunicode/libunicode.la \
	$(abs_top_builddir)/lib/libnvpair/libnvpair.la \
	$(abs_top_builddir)/lib/libzstd/libzstd.la \
	$(abs_top_builddir)/lib/libzutil/libzutil.la

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
vdev_raidz_math_powerpc_altivec.$(OBJEXT): CFLAGS += -maltivec
vdev_raidz_math_powerpc_altivec.l$(OBJEXT): CFLAGS += -maltivec
endif

include $(top_srcdir)/config/CppCheck.am
