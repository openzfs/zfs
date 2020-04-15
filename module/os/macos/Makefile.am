# Makefile used only by macOS. Should define no dependencies for
# other platforms.

spl_SRCS = 	\
	%D%/spl/spl-atomic.c \
	%D%/spl/spl-condvar.c \
	%D%/spl/spl-cred.c \
	%D%/spl/spl-debug.c \
	%D%/spl/spl-ddi.c \
	%D%/spl/spl-err.c \
	%D%/spl/spl-kmem.c \
	%D%/spl/spl-kstat.c \
	%D%/spl/spl-list.c \
	%D%/spl/spl-mutex.c \
	%D%/spl/spl-osx.c \
	%D%/spl/spl-policy.c \
	%D%/spl/spl-proc.c \
	%D%/spl/spl-processor.c \
	%D%/spl/spl-proc_list.c \
	%D%/spl/spl-qsort.c \
	%D%/spl/spl-rwlock.c \
	%D%/spl/spl-seg_kmem.c \
	%D%/spl/spl-taskq.c \
	%D%/spl/spl-thread.c \
	%D%/spl/spl-time.c \
	%D%/spl/spl-tsd.c \
	%D%/spl/spl-uio.c \
	%D%/spl/spl-vmem.c \
	%D%/spl/spl-vnode.c \
	%D%/spl/spl-xdr.c

# libspl_la_LIBS = -lnone


KEXTINFO_PLIST = %D%/zfs/Info.plist
PLIST_STRING = %D%/zfs/InfoPlist.strings

# ZFS_META_VERSION = @ZFS_META_VERSION@
ZFS_DEBUG_STR = @ZFS_DEBUG_STR@

macos_zfs_CPPFLAGS = \
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


macos_zfs_CPPFLAGS += @KERNEL_DEBUG_CPPFLAGS@

macos_zfs_CFLAGS = -Wno-sign-conversion -Wno-shorten-64-to-32 \
	-Wno-conditional-uninitialized -Wno-shadow \
	-Wno-implicit-int-conversion $(INLINE_HINT_FUNCTIONS) 

macos_zfs_CXXFLAGS = -Wno-sign-conversion -Wno-shorten-64-to-32 \
	-Wno-conditional-uninitialized -std=gnu++11 $(INLINE_HINT_FUNCTIONS)

macos_zfs_LDFLAGS = \
	-Xlinker \
	-kext \
	-nostdlib \
	-lkmodc++ \
	-lkmod \
	-lcc_kext

if TARGET_CPU_AARCH64
macos_zfs_CPPFLAGS+=-arch arm64e -arch x86_64
macos_zfs_LDFLAGS+=-arch arm64e -arch x86_64
#zfs_ASM_SOURCES_C = \
#	%D%/../../icp/asm-aarch64/os/macos/blake3/b3_aarch64_sse2.S \
#	%D%/../../icp/asm-aarch64/os/macos/blake3/b3_aarch64_sse41.S
endif

macos_zfs_LIBS =

# If we don't set this to nothing, it adds "-lz -liconv"
# LIBS =

bin_PROGRAMS += noinst_zfs.kext
noinst_PROGRAMS += macos_zfs

noinst_zfs_kext_SOURCES =


if TARGET_CPU_X86_64
zfs_ASM_SOURCES_C = \
	%D%/../../icp/asm-x86_64/aes/aeskey.c \
	%D%/../../icp/algs/modes/gcm_pclmulqdq.c \
	%D%/../../zcommon/zfs_fletcher_intel.c \
	%D%/../../zcommon/zfs_fletcher_sse.c \
	%D%/../../zcommon/zfs_fletcher_avx512.c \
	%D%/../../zfs/vdev_raidz_math_sse2.c \
	%D%/../../zfs/vdev_raidz_math_ssse3.c \
	%D%/../../zfs/vdev_raidz_math_avx2.c \
	%D%/../../zfs/vdev_raidz_math_avx512f.c \
	%D%/../../zfs/vdev_raidz_math_avx512bw.c
zfs_ASM_SOURCES_AS = \
	%D%/../../icp/asm-x86_64/os/macos/aes/aes_amd64.S \
	%D%/../../icp/asm-x86_64/os/macos/aes/aes_aesni.S \
	%D%/../../icp/asm-x86_64/os/macos/modes/aesni-gcm-x86_64.S \
	%D%/../../icp/asm-x86_64/os/macos/modes/ghash-x86_64.S \
	%D%/../../icp/asm-x86_64/os/macos/modes/gcm_pclmulqdq.S \
	%D%/../../icp/asm-x86_64/os/macos/sha2/sha256_impl.S \
	%D%/../../icp/asm-x86_64/os/macos/sha2/sha512_impl.S \
	%D%/../../icp/asm-x86_64/os/macos/blake3/blake3_avx2.S \
	%D%/../../icp/asm-x86_64/os/macos/blake3/blake3_avx512.S \
	%D%/../../icp/asm-x86_64/os/macos/blake3/blake3_sse2.S \
	%D%/../../icp/asm-x86_64/os/macos/blake3/blake3_sse41.S
else
zfs_ASM_SOURCES_C =
zfs_ASM_SOURCES_AS =
endif

zfs_common_SRCS = \
	%D%/../../zfs/abd.c \
	%D%/../../zfs/aggsum.c \
	%D%/../../zfs/arc.c \
	%D%/../../avl/avl.c \
	%D%/../../zfs/blake3_zfs.c \
	%D%/../../zfs/blkptr.c \
	%D%/../../zfs/bplist.c \
	%D%/../../zfs/bpobj.c \
	%D%/../../zfs/bptree.c \
	%D%/../../zfs/bqueue.c \
	%D%/../../zfs/btree.c \
	%D%/../../zfs/zfs_chksum.c \
	%D%/../../zcommon/cityhash.c \
	%D%/../../zfs/dataset_kstats.c \
	%D%/../../zfs/dbuf.c \
	%D%/../../zfs/dbuf_stats.c \
	%D%/../../zfs/ddt.c \
	%D%/../../zfs/ddt_zap.c \
	%D%/../../zfs/dmu.c \
	%D%/../../zfs/dmu_diff.c \
	%D%/../../zfs/dmu_object.c \
	%D%/../../zfs/dmu_objset.c \
	%D%/../../zfs/dmu_recv.c \
	%D%/../../zfs/dmu_redact.c \
	%D%/../../zfs/dmu_send.c \
	%D%/../../zfs/dmu_traverse.c \
	%D%/../../zfs/dmu_tx.c \
	%D%/../../zfs/dmu_zfetch.c \
	%D%/../../zfs/dnode.c \
	%D%/../../zfs/dnode_sync.c \
	%D%/../../zfs/dsl_bookmark.c \
	%D%/../../zfs/dsl_crypt.c \
	%D%/../../zfs/dsl_dataset.c \
	%D%/../../zfs/dsl_deadlist.c \
	%D%/../../zfs/dsl_deleg.c \
	%D%/../../zfs/dsl_destroy.c \
	%D%/../../zfs/dsl_dir.c \
	%D%/../../zfs/dsl_pool.c \
	%D%/../../zfs/dsl_prop.c \
	%D%/../../zfs/dsl_scan.c \
	%D%/../../zfs/dsl_synctask.c \
	%D%/../../zfs/dsl_userhold.c \
	%D%/../../zfs/edonr_zfs.c \
	%D%/../../zfs/fm.c \
	%D%/../../zfs/gzip.c \
	%D%/../../zfs/hkdf.c \
	%D%/../../zfs/lz4.c \
	%D%/../../zfs/lz4_zfs.c \
	%D%/../../zfs/lzjb.c \
	%D%/../../zfs/metaslab.c \
	%D%/../../zfs/mmp.c \
	%D%/../../zfs/multilist.c \
	%D%/../../zfs/objlist.c \
	%D%/../../zfs/pathname.c \
	%D%/../../zfs/range_tree.c \
	%D%/../../zfs/refcount.c \
	%D%/../../zfs/rrwlock.c \
	%D%/../../zfs/sa.c \
	%D%/../../zfs/sha256.c \
	%D%/../../zfs/skein_zfs.c \
	%D%/../../zfs/spa.c \
	%D%/../../zfs/spa_checkpoint.c \
	%D%/../../zfs/spa_config.c \
	%D%/../../zfs/spa_errlog.c \
	%D%/../../zfs/spa_history.c \
	%D%/../../zfs/spa_log_spacemap.c \
	%D%/../../zfs/spa_misc.c \
	%D%/../../zfs/spa_stats.c \
	%D%/../../zfs/space_map.c \
	%D%/../../zfs/space_reftree.c \
	%D%/../../zfs/txg.c \
	%D%/../../zfs/uberblock.c \
	%D%/../../zfs/unique.c \
	%D%/../../zfs/vdev.c \
	%D%/../../zfs/vdev_cache.c \
	%D%/../../zfs/vdev_draid.c \
	%D%/../../zfs/vdev_draid_rand.c \
	%D%/../../zfs/vdev_indirect.c \
	%D%/../../zfs/vdev_indirect_births.c \
	%D%/../../zfs/vdev_indirect_mapping.c \
	%D%/../../zfs/vdev_initialize.c \
	%D%/../../zfs/vdev_label.c \
	%D%/../../zfs/vdev_mirror.c \
	%D%/../../zfs/vdev_missing.c \
	%D%/../../zfs/vdev_queue.c \
	%D%/../../zfs/vdev_raidz.c \
	%D%/../../zfs/vdev_raidz_math.c \
	%D%/../../zfs/vdev_raidz_math_scalar.c \
	%D%/../../zfs/vdev_rebuild.c \
	%D%/../../zfs/vdev_removal.c \
	%D%/../../zfs/vdev_root.c \
	%D%/../../zfs/vdev_trim.c \
	%D%/../../zfs/zap.c \
	%D%/../../zfs/zap_leaf.c \
	%D%/../../zfs/zap_micro.c \
	%D%/../../zfs/zcp.c \
	%D%/../../zfs/zcp_get.c \
	%D%/../../zfs/zcp_global.c \
	%D%/../../zfs/zcp_iter.c \
	%D%/../../zfs/zcp_set.c \
	%D%/../../zfs/zcp_synctask.c \
	%D%/../../zfs/zfeature.c \
	%D%/../../zcommon/zfeature_common.c \
	%D%/../../zfs/zfs_byteswap.c \
	%D%/../../zfs/zfs_fm.c \
	%D%/../../zfs/zfs_fuid.c \
	%D%/../../zfs/zfs_ioctl.c \
	%D%/../../zfs/zfs_log.c \
	%D%/../../zfs/zfs_onexit.c \
	%D%/../../zfs/zfs_quota.c \
	%D%/../../zfs/zfs_ratelimit.c \
	%D%/../../zfs/zfs_replay.c \
	%D%/../../zfs/zfs_rlock.c \
	%D%/../../zfs/zfs_sa.c \
	%D%/../../zfs/zfs_vnops.c \
	%D%/../../zfs/zil.c \
	%D%/../../zfs/zio.c \
	%D%/../../zfs/zio_checksum.c \
	%D%/../../zfs/zio_compress.c \
	%D%/../../zfs/zio_inject.c \
	%D%/../../zfs/zle.c \
	%D%/../../zfs/zrlock.c \
	%D%/../../zfs/zthr.c \
	%D%/../../zfs/zvol.c

zfs_os_SRCS = \
	%D%/zfs/abd_os.c \
	%D%/zfs/arc_os.c \
	%D%/zfs/ldi_osx.c \
	%D%/zfs/ldi_vnode.c \
	%D%/zfs/ldi_iokit.cpp \
	%D%/zfs/spa_misc_os.c \
	%D%/zfs/sysctl_os.c \
	%D%/zfs/vdev_disk.c \
	%D%/zfs/vdev_file.c \
	%D%/zfs/zfs_acl.c \
	%D%/zfs/zfs_boot.cpp \
	%D%/zfs/zfs_ctldir.c \
	%D%/zfs/zfs_debug.c \
	%D%/zfs/zfs_dir.c \
	%D%/zfs/zfs_file_os.c \
	%D%/zfs/zfs_fuid_os.c \
	%D%/zfs/zfs_ioctl_os.c \
	%D%/zfs/zfs_osx.cpp \
	%D%/zfs/zfs_racct.c \
	%D%/zfs/zfs_vfsops.c \
	%D%/zfs/zfs_vnops_os.c \
	%D%/zfs/zfs_vnops_osx.c \
	%D%/zfs/zfs_vnops_osx_lib.c \
	%D%/zfs/zfs_vnops_osx_xattr.c \
	%D%/zfs/zfs_znode.c \
	%D%/zfs/zio_crypt.c \
	%D%/zfs/zvol_os.c \
	%D%/zfs/zvolIO.cpp \
	%D%/zfs/ZFSDatasetProxy.cpp \
	%D%/zfs/ZFSDatasetScheme.cpp \
	%D%/zfs/ZFSDataset.cpp \
	%D%/zfs/ZFSPool.cpp

zcommon_SRCS = \
	%D%/../../nvpair/fnvpair.c \
	%D%/../../nvpair/nvpair.c \
	%D%/../../nvpair/nvpair_alloc_fixed.c \
	%D%/../../nvpair/nvpair_alloc_spl.c \
	%D%/../../unicode/u8_textprep.c \
	%D%/../../unicode/uconv.c \
	%D%/../../zcommon/zfs_comutil.c \
	%D%/../../zcommon/zfs_deleg.c \
	%D%/../../zcommon/zfs_fletcher.c \
	%D%/../../zcommon/zfs_fletcher_superscalar.c \
	%D%/../../zcommon/zfs_fletcher_superscalar4.c \
	%D%/../../zcommon/zfs_namecheck.c \
	%D%/../../zcommon/zfs_prop.c \
	%D%/../../zcommon/zpool_prop.c \
	%D%/../../zcommon/zprop_common.c

icp_SRCS = \
	%D%/../../icp/api/kcf_cipher.c \
	%D%/../../icp/api/kcf_mac.c \
	%D%/../../icp/api/kcf_ctxops.c \
	%D%/../../icp/core/kcf_callprov.c \
	%D%/../../icp/core/kcf_prov_tabs.c \
	%D%/../../icp/core/kcf_sched.c \
	%D%/../../icp/core/kcf_mech_tabs.c \
	%D%/../../icp/core/kcf_prov_lib.c \
	%D%/../../icp/spi/kcf_spi.c \
	%D%/../../icp/io/aes.c \
	%D%/../../icp/io/sha2_mod.c \
	%D%/../../icp/io/skein_mod.c \
	%D%/../../icp/algs/blake3/blake3.c \
	%D%/../../icp/algs/blake3/blake3_generic.c \
	%D%/../../icp/algs/blake3/blake3_impl.c \
	%D%/../../icp/algs/blake3/blake3_x86-64.c \
	%D%/../../icp/algs/edonr/edonr.c \
	%D%/../../icp/algs/modes/cbc.c \
	%D%/../../icp/algs/modes/ccm.c \
	%D%/../../icp/algs/modes/ctr.c \
	%D%/../../icp/algs/modes/ecb.c \
	%D%/../../icp/algs/modes/gcm_generic.c \
	%D%/../../icp/algs/modes/gcm.c \
	%D%/../../icp/algs/modes/modes.c \
	%D%/../../icp/algs/sha2/sha2.c \
	%D%/../../icp/algs/skein/skein.c \
	%D%/../../icp/algs/skein/skein_block.c \
	%D%/../../icp/algs/skein/skein_iv.c \
	%D%/../../icp/algs/aes/aes_impl_aesni.c \
	%D%/../../icp/algs/aes/aes_impl_generic.c \
	%D%/../../icp/algs/aes/aes_impl_x86-64.c \
	%D%/../../icp/algs/aes/aes_impl.c \
	%D%/../../icp/algs/aes/aes_modes.c \
	%D%/../../icp/illumos-crypto.c

lua_SRCS = \
	%D%/../../lua/lapi.c \
	%D%/../../lua/lauxlib.c \
	%D%/../../lua/lbaselib.c \
	%D%/../../lua/lcode.c \
	%D%/../../lua/lcompat.c \
	%D%/../../lua/lcorolib.c \
	%D%/../../lua/lctype.c \
	%D%/../../lua/ldebug.c \
	%D%/../../lua/ldo.c \
	%D%/../../lua/lfunc.c \
	%D%/../../lua/lgc.c \
	%D%/../../lua/llex.c \
	%D%/../../lua/lmem.c \
	%D%/../../lua/lobject.c \
	%D%/../../lua/lopcodes.c \
	%D%/../../lua/lparser.c \
	%D%/../../lua/lstate.c \
	%D%/../../lua/lstring.c \
	%D%/../../lua/lstrlib.c \
	%D%/../../lua/ltable.c \
	%D%/../../lua/ltablib.c \
	%D%/../../lua/ltm.c \
	%D%/../../lua/lvm.c \
	%D%/../../lua/lzio.c \
	%D%/../../lua/setjmp/setjmp.S

zstd_SRCS = \
	%D%/../../zstd/zfs_zstd.c \
        %D%/../../zstd/lib/common/entropy_common.c \
        %D%/../../zstd/lib/common/error_private.c \
        %D%/../../zstd/lib/common/fse_decompress.c \
        %D%/../../zstd/lib/common/pool.c \
        %D%/../../zstd/lib/common/zstd_common.c \
        %D%/../../zstd/lib/common/xxhash.c \
        %D%/../../zstd/lib/compress/fse_compress.c \
        %D%/../../zstd/lib/compress/hist.c \
        %D%/../../zstd/lib/compress/huf_compress.c \
        %D%/../../zstd/lib/compress/zstd_compress.c \
        %D%/../../zstd/lib/compress/zstd_compress_literals.c \
        %D%/../../zstd/lib/compress/zstd_compress_sequences.c \
        %D%/../../zstd/lib/compress/zstd_compress_superblock.c \
        %D%/../../zstd/lib/compress/zstd_double_fast.c \
        %D%/../../zstd/lib/compress/zstd_fast.c \
        %D%/../../zstd/lib/compress/zstd_lazy.c \
        %D%/../../zstd/lib/compress/zstd_ldm.c \
        %D%/../../zstd/lib/compress/zstd_opt.c \
        %D%/../../zstd/lib/decompress/huf_decompress.c \
        %D%/../../zstd/lib/decompress/zstd_ddict.c \
        %D%/../../zstd/lib/decompress/zstd_decompress.c \
        %D%/../../zstd/lib/decompress/zstd_decompress_block.c

macos_zfs_SOURCES = \
	$(spl_SRCS) \
	$(zfs_common_SRCS) \
	$(zfs_os_SRCS) \
	$(zcommon_SRCS) \
	$(icp_SRCS) \
	$(lua_SRCS) \
	$(zstd_SRCS) \
        $(zfs_ASM_SOURCES_C) \
        $(zfs_ASM_SOURCES_AS)

# Ensure these files are always built with -O2 to avoid stack overflow.
%D%/../../zfs/macos_zfs-dsl_scan.$(OBJEXT): CFLAGS := $(CFLAGS:-O0%=-O2)
%D%/../../lua/macos_zfs-lvm.$(OBJEXT): CFLAGS := $(CFLAGS:-O0%=-O2)

# Zstd uses -O3 by default, so we should follow
%D%/../../zstd/lib/macos_zfs-zstd.$(OBJEXT): CFLAGS := -fno-tree-vectorize -O3


KERNEL_MODDIR=  $(DESTDIR)@KERNEL_MODPREFIX@/zfs.kext

dist_noinst_DATA += $(PLIST_STRING) $(KEXTINFO_PLIST)

noinst_zfs.kext$(EXEEXT): macos_zfs $(PLIST_STRING) $(KEXTINFO_PLIST)
	@echo ""
	@mkdir -p %D%/zfs.kext/Contents/Resources/English.lproj %D%/zfs.kext/Contents/MacOS
	@cp -f $(KEXTINFO_PLIST) %D%/zfs.kext/Contents/
	/usr/libexec/PlistBuddy -c "Set :CFBundleVersion $(ZFS_META_VERSION)" %D%/zfs.kext/Contents/Info.plist
	/usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $(ZFS_META_VERSION)" %D%/zfs.kext/Contents/Info.plist
	@cp -f $(PLIST_STRING) %D%/zfs.kext/Contents/Resources/English.lproj/
	@cp -f macos_zfs %D%/zfs.kext/Contents/MacOS/zfs
	@cp -f $(top_srcdir)/contrib/macOS/VolumeIcon.icns %D%/zfs.kext/Contents/Resources/VolumeIcon.icns
/usr/libexec/PlistBuddy -c "Add :OSBundleRequired string Root" %D%/zfs.kext/Contents/Info.plist
	@kextlibs -unsupported -undef-symbols -xml %D%/zfs.kext/ || echo "Ignoring errors..(Most of these are expected)"

INSTALL_DATA_HOOKS += kext-install-exec-local
kext-install-exec-local: noinst_zfs.kext
	rm -rf $(KERNEL_MODDIR)
	mkdir -p $(KERNEL_MODDIR)
	rsync -r %D%/zfs.kext/ $(KERNEL_MODDIR)
	chown -R root:wheel $(KERNEL_MODDIR) || echo "Unable to chown root:wheel $(KERNEL_MODDIR)"
	@echo
	@echo "To load module: kextload -v $(KERNEL_MODDIR)"
	@echo "To uninstall module: rm -rf $(KERNEL_MODDIR)"
	@echo

CLEANFILES += \
	%D%/macos_zfs \
	%D%/zfs.kext/Contents/Resources/English.lproj/InfoPlist.strings \
	%D%/zfs.kext/Contents/Resources/VolumeIcon.icns \
	%D%/zfs.kext/Contents/Info.plist \
	%D%/zfs.kext/Contents/PkgInfo \
	%D%/zfs.kext/Contents/MacOS/zfs

CLEAN_LOCAL += macos_clean_local
macos_clean_local:
	@rmdir \
	%D%/zfs.kext/Contents/Resources/English.lproj \
	%D%/zfs.kext/Contents/Resources \
	%D%/zfs.kext/Contents/MacOS \
	%D%/zfs.kext/Contents \
	%D%/zfs.kext || true
