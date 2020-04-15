libspldir = $(includedir)/libspl
libspl_HEADERS = \
	%D%/assert.h \
	%D%/atomic.h \
	%D%/libgen.h \
	%D%/libshare.h \
	%D%/statcommon.h \
	%D%/stdlib.h \
	%D%/string.h \
	%D%/umem.h \
	%D%/unistd.h \
	%D%/zone.h

if BUILD_FREEBSD
libspl_HEADERS += \
	%D%/os/freebsd/fcntl.h
endif


libspl_rpcdir = $(libspldir)/rpc
libspl_rpc_HEADERS = \
	%D%/rpc/xdr.h


libspl_sysdir = $(libspldir)/sys
libspl_sys_HEADERS = \
	%D%/sys/acl.h \
	%D%/sys/acl_impl.h \
	%D%/sys/asm_linkage.h \
	%D%/sys/callb.h \
	%D%/sys/cmn_err.h \
	%D%/sys/cred.h \
	%D%/sys/debug.h \
	%D%/sys/dkio.h \
	%D%/sys/dklabel.h \
	%D%/sys/feature_tests.h \
	%D%/sys/inttypes.h \
	%D%/sys/isa_defs.h \
	%D%/sys/kmem.h \
	%D%/sys/kstat.h \
	%D%/sys/list.h \
	%D%/sys/list_impl.h \
	%D%/sys/mhd.h \
	%D%/sys/mkdev.h \
	%D%/sys/policy.h \
	%D%/sys/poll.h \
	%D%/sys/priv.h \
	%D%/sys/processor.h \
	%D%/sys/sha2.h \
	%D%/sys/simd.h \
	%D%/sys/stack.h \
	%D%/sys/stdtypes.h \
	%D%/sys/string.h \
	%D%/sys/sunddi.h \
	%D%/sys/systeminfo.h \
	%D%/sys/time.h \
	%D%/sys/trace_spl.h \
	%D%/sys/trace_zfs.h \
	%D%/sys/types.h \
	%D%/sys/types32.h \
	%D%/sys/uio.h \
	%D%/sys/vnode.h \
	%D%/sys/wmsum.h \
	%D%/sys/zone.h

libspl_ia32dir = $(libspldir)/sys/ia32

if BUILD_LINUX
libspl_sys_HEADERS += \
	%D%/os/linux/sys/byteorder.h \
	%D%/os/linux/sys/errno.h \
	%D%/os/linux/sys/mnttab.h \
	%D%/os/linux/sys/mount.h \
	%D%/os/linux/sys/param.h \
	%D%/os/linux/sys/stat.h \
	%D%/os/linux/sys/sysmacros.h \
	%D%/os/linux/sys/zfs_context_os.h

libspl_ia32_HEADERS = \
	%D%/os/linux/sys/ia32/asm_linkage.h
endif

if BUILD_FREEBSD
libspl_sys_HEADERS += \
	%D%/os/freebsd/sys/byteorder.h \
	%D%/os/freebsd/sys/fcntl.h \
	%D%/os/freebsd/sys/file.h \
	%D%/os/freebsd/sys/mnttab.h \
	%D%/os/freebsd/sys/mount.h \
	%D%/os/freebsd/sys/param.h \
	%D%/os/freebsd/sys/stat.h \
	%D%/os/freebsd/sys/sysmacros.h \
	%D%/os/freebsd/sys/vfs.h \
	%D%/os/freebsd/sys/zfs_context_os.h

libspl_ia32_HEADERS = \
	%D%/os/freebsd/sys/ia32/asm_linkage.h
endif


libspl_sys_dktpdir = $(libspl_sysdir)/dktp
libspl_sys_dktp_HEADERS = \
	%D%/sys/dktp/fdisk.h

if BUILD_MACOS

# macOS does not promote header location (legacy)
libsplmacosdir = $(includedir)/libspl/os/macos
libsplmacos_HEADERS = \
	%D%/os/macos/dirent.h \
	%D%/os/macos/libdiskmgt.h \
	%D%/os/macos/mntent.h \
	%D%/os/macos/poll.h \
	%D%/os/macos/pthread.h \
	%D%/os/macos/stdio.h \
	%D%/os/macos/stdlib.h \
	%D%/os/macos/string.h \
	%D%/os/macos/synch.h \
	%D%/os/macos/time.h \
	%D%/os/macos/unistd.h

libsplmacos_machdir = $(libsplmacosdir)/mach
libsplmacos_mach_HEADERS = \
	%D%/os/macos/mach/boolean.h \
	%D%/os/macos/mach/task.h

# To be removed with Unify Assembler
libsplmacos_osxdir = $(libsplmacosdir)/osx
libsplmacos_osx_HEADERS = \
	%D%/os/macos/osx/asm_linkage.h

libsplmacos_sysdir = $(libsplmacosdir)/sys
libsplmacos_sys_HEADERS = \
	%D%/os/macos/sys/byteorder.h \
	%D%/os/macos/sys/errno.h \
	%D%/os/macos/sys/fcntl.h \
	%D%/os/macos/sys/file.h \
	%D%/os/macos/sys/kernel_types.h \
	%D%/os/macos/sys/misc.h \
	%D%/os/macos/sys/mnttab.h \
	%D%/os/macos/sys/mount.h \
	%D%/os/macos/sys/param.h \
	%D%/os/macos/sys/stat.h \
	%D%/os/macos/sys/sysmacros.h \
	%D%/os/macos/sys/time.h \
	%D%/os/macos/sys/uio.h \
	%D%/os/macos/sys/vfs.h \
	%D%/os/macos/sys/xattr.h \
	%D%/os/macos/sys/zfs_context_os.h
endif
