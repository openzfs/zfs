
INFO_PLIST = Info.plist
PLIST_STRING = InfoPlist.strings

ZFS_META_VERSION = @ZFS_META_VERSION@
ZFS_DEBUG_STR = @ZFS_DEBUG_STR@

zfs_CPPFLAGS = \
	-Wall \
	-nostdinc \
	-mkernel \
	-fno-builtin-printf \
	-Wframe-larger-than=400 \
	-D__KERNEL__ \
	-D_KERNEL \
	-DKERNEL \
	-DKERNEL_PRIVATE \
	-DDRIVER_PRIVATE \
	-UHAVE_LARGE_STACKS \
	-DNAMEDSTREAMS=1 \
	-D__DARWIN_64_BIT_INO_T=1 \
	-DAPPLE \
	-DNeXT \
	-include $(top_builddir)/zfs_config.h \
	-I$(top_srcdir)/include/os/macos/spl \
	-I$(top_srcdir)/include/os/macos/zfs \
	-I$(top_srcdir)/module/icp/include \
	-I$(top_srcdir)/include \
	-I@KERNEL_HEADERS@/Headers \
	-I@KERNEL_HEADERS@/PrivateHeaders \
	-I$(top_srcdir)/module/zstd/include


zfs_CPPFLAGS += @KERNEL_DEBUG_CPPFLAGS@

zfs_CFLAGS = -Wno-sign-conversion -Wno-shorten-64-to-32 -Wno-conditional-uninitialized
zfs_CFLAGS += -Wno-shadow -Wno-implicit-int-conversion
zfs_CFLAGS += -finline-hint-functions

zfs_CXXFLAGS = -Wno-sign-conversion -Wno-shorten-64-to-32 -Wno-conditional-uninitialized
zfs_CXXFLAGS += -std=gnu++11
zfs_CXXFLAGS += -finline-hint-functions

zfs_LDFLAGS = \
	-Xlinker \
	-kext \
	-nostdlib \
	-lkmodc++ \
	-lkmod \
	-lcc_kext

if TARGET_CPU_AARCH64
zfs_CPPFLAGS+=-arch arm64e -arch x86_64
zfs_LDFLAGS+=-arch arm64e -arch x86_64
endif

zfs_LDADD = \
	$(top_builddir)/module/os/macos/spl/libspl.la

zfs_LIBS =

# If we don't set this to nothing, it adds "-lz -liconv"
LIBS =

bin_PROGRAMS = zfs.kext
noinst_PROGRAMS = zfs

zfs_kext_SOURCES =

if TARGET_CPU_X86_64
zfs_ASM_SOURCES_C = \
	../../../icp/asm-x86_64/aes/aeskey.c \
	../../../icp/algs/modes/gcm_pclmulqdq.c \
	../../../zcommon/zfs_fletcher_intel.c \
	../../../zcommon/zfs_fletcher_sse.c \
	../../../zcommon/zfs_fletcher_avx512.c \
	../../../zfs/vdev_raidz_math_sse2.c \
	../../../zfs/vdev_raidz_math_ssse3.c \
	../../../zfs/vdev_raidz_math_avx2.c \
	../../../zfs/vdev_raidz_math_avx512f.c \
	../../../zfs/vdev_raidz_math_avx512bw.c
zfs_ASM_SOURCES_AS = \
	../../../icp/asm-x86_64/os/macos/aes/aes_amd64.S \
	../../../icp/asm-x86_64/os/macos/aes/aes_aesni.S \
	../../../icp/asm-x86_64/os/macos/modes/aesni-gcm-x86_64.S \
	../../../icp/asm-x86_64/os/macos/modes/ghash-x86_64.S \
	../../../icp/asm-x86_64/os/macos/modes/gcm_pclmulqdq.S \
	../../../icp/asm-x86_64/os/macos/sha2/sha256_impl.S \
	../../../icp/asm-x86_64/os/macos/sha2/sha512_impl.S

else
zfs_ASM_SOURCES_C =
zfs_ASM_SOURCES_AS =
endif

zfs_SOURCES = \
	../../../zfs/abd.c \
	abd_os.c \
	../../../zfs/aggsum.c \
	../../../zfs/arc.c \
	arc_os.c \
	../../../avl/avl.c \
	../../../zfs/blkptr.c \
	../../../zfs/bplist.c \
	../../../zfs/bpobj.c \
	../../../zfs/bptree.c \
	../../../zfs/bqueue.c \
	../../../zfs/btree.c \
	../../../zcommon/cityhash.c \
	../../../zfs/dataset_kstats.c \
	../../../zfs/dbuf.c \
	../../../zfs/dbuf_stats.c \
	../../../zfs/ddt.c \
	../../../zfs/ddt_zap.c \
	../../../zfs/dmu.c \
	../../../zfs/dmu_diff.c \
	../../../zfs/dmu_object.c \
	../../../zfs/dmu_objset.c \
	../../../zfs/dmu_recv.c \
	../../../zfs/dmu_redact.c \
	../../../zfs/dmu_send.c \
	../../../zfs/dmu_traverse.c \
	../../../zfs/dmu_tx.c \
	../../../zfs/dmu_zfetch.c \
	../../../zfs/dnode.c \
	../../../zfs/dnode_sync.c \
	../../../zfs/dsl_bookmark.c \
	../../../zfs/dsl_crypt.c \
	../../../zfs/dsl_dataset.c \
	../../../zfs/dsl_deadlist.c \
	../../../zfs/dsl_deleg.c \
	../../../zfs/dsl_destroy.c \
	../../../zfs/dsl_dir.c \
	../../../zfs/dsl_pool.c \
	../../../zfs/dsl_prop.c \
	../../../zfs/dsl_scan.c \
	../../../zfs/dsl_synctask.c \
	../../../zfs/dsl_userhold.c \
	../../../zfs/edonr_zfs.c \
	../../../zfs/fm.c \
	../../../zfs/gzip.c \
	../../../zfs/hkdf.c \
	ldi_osx.c \
	ldi_vnode.c \
	ldi_iokit.cpp \
	../../../zfs/lz4.c \
	../../../zfs/lz4_zfs.c \
	../../../zfs/lzjb.c \
	../../../zfs/metaslab.c \
	../../../zfs/mmp.c \
	../../../zfs/multilist.c \
	../../../zfs/objlist.c \
	../../../zfs/pathname.c \
	../../../zfs/range_tree.c \
	../../../zfs/refcount.c \
	../../../zfs/rrwlock.c \
	../../../zfs/sa.c \
	../../../zfs/sha256.c \
	../../../zfs/skein_zfs.c \
	../../../zfs/spa.c \
	../../../zfs/spa_checkpoint.c \
	../../../zfs/spa_config.c \
	../../../zfs/spa_errlog.c \
	../../../zfs/spa_history.c \
	../../../zfs/spa_log_spacemap.c \
	../../../zfs/spa_misc.c \
	spa_misc_os.c \
	../../../zfs/spa_stats.c \
	../../../zfs/space_map.c \
	../../../zfs/space_reftree.c \
	sysctl_os.c \
	../../../zfs/txg.c \
	../../../zfs/uberblock.c \
	../../../zfs/unique.c \
	../../../zfs/vdev.c \
	../../../zfs/vdev_cache.c \
	vdev_disk.c \
	../../../zfs/vdev_draid.c \
	../../../zfs/vdev_draid_rand.c \
	vdev_file.c \
	../../../zfs/vdev_indirect.c \
	../../../zfs/vdev_indirect_births.c \
	../../../zfs/vdev_indirect_mapping.c \
	../../../zfs/vdev_initialize.c \
	../../../zfs/vdev_label.c \
	../../../zfs/vdev_mirror.c \
	../../../zfs/vdev_missing.c \
	../../../zfs/vdev_queue.c \
	../../../zfs/vdev_raidz.c \
	../../../zfs/vdev_raidz_math.c \
	../../../zfs/vdev_raidz_math_scalar.c \
	../../../zfs/vdev_rebuild.c \
	../../../zfs/vdev_removal.c \
	../../../zfs/vdev_root.c \
	../../../zfs/vdev_trim.c \
	../../../zfs/zap.c \
	../../../zfs/zap_leaf.c \
	../../../zfs/zap_micro.c \
	../../../zfs/zcp.c \
	../../../zfs/zcp_get.c \
	../../../zfs/zcp_global.c \
	../../../zfs/zcp_iter.c \
	../../../zfs/zcp_set.c \
	../../../zfs/zcp_synctask.c \
	../../../zfs/zfeature.c \
	../../../zcommon/zfeature_common.c \
	zfs_acl.c \
	zfs_boot.cpp \
	../../../zfs/zfs_byteswap.c \
	zfs_ctldir.c \
	zfs_debug.c \
	zfs_dir.c \
	../../../zfs/zfs_fm.c \
	zfs_file_os.c \
	../../../zfs/zfs_fuid.c \
	zfs_fuid_os.c \
	../../../zfs/zfs_ioctl.c \
	zfs_ioctl_os.c \
	../../../zfs/zfs_log.c \
	../../../zfs/zfs_onexit.c \
	zfs_osx.cpp \
	../../../zfs/zfs_quota.c \
	zfs_racct.c \
	../../../zfs/zfs_ratelimit.c \
	../../../zfs/zfs_replay.c \
	../../../zfs/zfs_rlock.c \
	../../../zfs/zfs_sa.c \
	zfs_vfsops.c \
	../../../zfs/zfs_vnops.c \
	zfs_vnops_os.c \
	zfs_vnops_osx.c \
	zfs_vnops_osx_lib.c \
	zfs_vnops_osx_xattr.c \
	zfs_znode.c \
	../../../zfs/zil.c \
	../../../zfs/zio.c \
	../../../zfs/zio_checksum.c \
	zio_crypt.c \
	../../../zfs/zio_compress.c \
	../../../zfs/zio_inject.c \
	../../../zfs/zle.c \
	../../../zfs/zrlock.c \
	../../../zfs/zthr.c \
	../../../zfs/zvol.c \
	zvol_os.c \
	zvolIO.cpp \
	ZFSDatasetProxy.cpp \
	ZFSDatasetScheme.cpp \
	ZFSDataset.cpp \
	ZFSPool.cpp \
	../../../nvpair/fnvpair.c \
	../../../nvpair/nvpair.c \
	../../../nvpair/nvpair_alloc_fixed.c \
	../../../nvpair/nvpair_alloc_spl.c \
	../../../unicode/u8_textprep.c \
	../../../unicode/uconv.c \
	../../../zcommon/zfs_comutil.c \
	../../../zcommon/zfs_deleg.c \
	../../../zcommon/zfs_fletcher.c \
	../../../zcommon/zfs_fletcher_superscalar.c \
	../../../zcommon/zfs_fletcher_superscalar4.c \
	../../../zcommon/zfs_namecheck.c \
	../../../zcommon/zfs_prop.c \
	../../../zcommon/zpool_prop.c \
	../../../zcommon/zprop_common.c \
	../../../icp/api/kcf_cipher.c \
	../../../icp/api/kcf_mac.c \
	../../../icp/api/kcf_ctxops.c \
	../../../icp/core/kcf_callprov.c \
	../../../icp/core/kcf_prov_tabs.c \
	../../../icp/core/kcf_sched.c \
	../../../icp/core/kcf_mech_tabs.c \
	../../../icp/core/kcf_prov_lib.c \
	../../../icp/spi/kcf_spi.c \
	../../../icp/io/aes.c \
	../../../icp/io/sha2_mod.c \
	../../../icp/io/skein_mod.c \
	../../../icp/os/modhash.c \
	../../../icp/algs/edonr/edonr.c \
	../../../icp/algs/modes/cbc.c \
	../../../icp/algs/modes/ccm.c \
	../../../icp/algs/modes/ctr.c \
	../../../icp/algs/modes/ecb.c \
	../../../icp/algs/modes/gcm_generic.c \
	../../../icp/algs/modes/gcm.c \
	../../../icp/algs/modes/modes.c \
	../../../icp/algs/sha2/sha2.c \
	../../../icp/algs/skein/skein.c \
	../../../icp/algs/skein/skein_block.c \
	../../../icp/algs/skein/skein_iv.c \
	../../../icp/algs/aes/aes_impl_aesni.c \
	../../../icp/algs/aes/aes_impl_generic.c \
	../../../icp/algs/aes/aes_impl_x86-64.c \
	../../../icp/algs/aes/aes_impl.c \
	../../../icp/algs/aes/aes_modes.c \
	../../../icp/illumos-crypto.c \
	../../../lua/lapi.c \
	../../../lua/lauxlib.c \
	../../../lua/lbaselib.c \
	../../../lua/lcode.c \
	../../../lua/lcompat.c \
	../../../lua/lcorolib.c \
	../../../lua/lctype.c \
	../../../lua/ldebug.c \
	../../../lua/ldo.c \
	../../../lua/lfunc.c \
	../../../lua/lgc.c \
	../../../lua/llex.c \
	../../../lua/lmem.c \
	../../../lua/lobject.c \
	../../../lua/lopcodes.c \
	../../../lua/lparser.c \
	../../../lua/lstate.c \
	../../../lua/lstring.c \
	../../../lua/lstrlib.c \
	../../../lua/ltable.c \
	../../../lua/ltablib.c \
	../../../lua/ltm.c \
	../../../lua/lvm.c \
	../../../lua/lzio.c \
	../../../lua/setjmp/setjmp.S \
	../../../zstd/lib/zstd.c \
	../../../zstd/zfs_zstd.c \
        $(zfs_ASM_SOURCES_C) \
        $(zfs_ASM_SOURCES_AS)

# Ensure these files are always built with -O2 to avoid stack overflow.
../../../zfs/zfs-dsl_scan.$(OBJEXT): CFLAGS := $(CFLAGS:-O0%=-O2)
../../../lua/zfs-lvm.$(OBJEXT): CFLAGS := $(CFLAGS:-O0%=-O2)

# Zstd uses -O3 by default, so we should follow
../../../zstd/lib/zfs-zstd.$(OBJEXT): CFLAGS := -fno-tree-vectorize -O3


KERNEL_MODDIR=  $(DESTDIR)@KERNEL_MODPREFIX@/zfs.kext

dist_noinst_DATA = $(PLIST_STRING) $(INFO_PLIST)

zfs.kext$(EXEEXT): zfs $(PLIST_STRING) $(INFO_PLIST)
	@echo ""
	@mkdir -p zfs.kext/Contents/Resources/English.lproj zfs.kext/Contents/MacOS
	@cp -f $(INFO_PLIST) zfs.kext/Contents/
	/usr/libexec/PlistBuddy -c "Set :CFBundleVersion $(ZFS_META_VERSION)" zfs.kext/Contents/Info.plist
	/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $(ZFS_META_VERSION)" zfs.kext/Contents/Info.plist
	@cp -f $(PLIST_STRING) zfs.kext/Contents/Resources/English.lproj/
	@cp -f zfs zfs.kext/Contents/MacOS/
	@cp -f $(top_srcdir)/contrib/macOS/VolumeIcon.icns zfs.kext/Contents/Resources/VolumeIcon.icns
/usr/libexec/PlistBuddy -c "Add :OSBundleRequired string Root" zfs.kext/Contents/Info.plist
	@kextlibs -unsupported -undef-symbols -xml zfs.kext/ || echo "Ignoring errors..(Most of these are expected)"

install-exec-local: zfs.kext
	rm -rf $(KERNEL_MODDIR)
	mkdir -p $(KERNEL_MODDIR)
	rsync -r zfs.kext/ $(KERNEL_MODDIR)
	chown -R root:wheel $(KERNEL_MODDIR) || echo "Unable to chown root:wheel $(KERNEL_MODDIR)"
	@echo
	@echo "To load module: kextload -v $(KERNEL_MODDIR)"
	@echo "To uninstall module: rm -rf $(KERNEL_MODDIR)"
	@echo

uninstall-am:
	rm -rf $(KERNEL_MODDIR)

clean:
	rm -rf zfs.kext/
	rm -f *.o *.lo zfs $(zfs_OBJECTS)
