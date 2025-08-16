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
	%D%/sys/backtrace.h \
	%D%/sys/callb.h \
	%D%/sys/cmn_err.h \
	%D%/sys/condvar.h \
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
	%D%/sys/mod.h \
	%D%/sys/mutex.h \
	%D%/sys/policy.h \
	%D%/sys/poll.h \
	%D%/sys/priv.h \
	%D%/sys/processor.h \
	%D%/sys/rwlock.h \
	%D%/sys/simd.h \
	%D%/sys/stack.h \
	%D%/sys/stdtypes.h \
	%D%/sys/string.h \
	%D%/sys/sunddi.h \
	%D%/sys/systeminfo.h \
	%D%/sys/thread.h \
	%D%/sys/taskq.h \
	%D%/sys/time.h \
	%D%/sys/timer.h \
	%D%/sys/trace_spl.h \
	%D%/sys/tunables.h \
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
	%D%/os/linux/sys/vfs.h \

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
	%D%/os/freebsd/sys/vfs.h

libspl_ia32_HEADERS = \
	%D%/os/freebsd/sys/ia32/asm_linkage.h
endif


libspl_sys_dktpdir = $(libspl_sysdir)/dktp
libspl_sys_dktp_HEADERS = \
	%D%/sys/dktp/fdisk.h

