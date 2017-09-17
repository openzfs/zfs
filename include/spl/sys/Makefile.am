SUBDIRS = fm fs sysevent

COMMON_H =

KERNEL_H = \
	$(top_srcdir)/include/spl/sys/acl.h \
	$(top_srcdir)/include/spl/sys/acl_impl.h \
	$(top_srcdir)/include/spl/sys/atomic.h \
	$(top_srcdir)/include/spl/sys/attr.h \
	$(top_srcdir)/include/spl/sys/bitmap.h \
	$(top_srcdir)/include/spl/sys/bootconf.h \
	$(top_srcdir)/include/spl/sys/bootprops.h \
	$(top_srcdir)/include/spl/sys/buf.h \
	$(top_srcdir)/include/spl/sys/byteorder.h \
	$(top_srcdir)/include/spl/sys/callb.h \
	$(top_srcdir)/include/spl/sys/callo.h \
	$(top_srcdir)/include/spl/sys/cmn_err.h \
	$(top_srcdir)/include/spl/sys/compress.h \
	$(top_srcdir)/include/spl/sys/condvar.h \
	$(top_srcdir)/include/spl/sys/conf.h \
	$(top_srcdir)/include/spl/sys/console.h \
	$(top_srcdir)/include/spl/sys/cpupart.h \
	$(top_srcdir)/include/spl/sys/cpuvar.h \
	$(top_srcdir)/include/spl/sys/crc32.h \
	$(top_srcdir)/include/spl/sys/cred.h \
	$(top_srcdir)/include/spl/sys/ctype.h \
	$(top_srcdir)/include/spl/sys/ddi.h \
	$(top_srcdir)/include/spl/sys/debug.h \
	$(top_srcdir)/include/spl/sys/dirent.h \
	$(top_srcdir)/include/spl/sys/disp.h \
	$(top_srcdir)/include/spl/sys/dkio.h \
	$(top_srcdir)/include/spl/sys/dkioc_free_util.h \
	$(top_srcdir)/include/spl/sys/dklabel.h \
	$(top_srcdir)/include/spl/sys/dnlc.h \
	$(top_srcdir)/include/spl/sys/dumphdr.h \
	$(top_srcdir)/include/spl/sys/efi_partition.h \
	$(top_srcdir)/include/spl/sys/errno.h \
	$(top_srcdir)/include/spl/sys/extdirent.h \
	$(top_srcdir)/include/spl/sys/fcntl.h \
	$(top_srcdir)/include/spl/sys/file.h \
	$(top_srcdir)/include/spl/sys/idmap.h \
	$(top_srcdir)/include/spl/sys/int_limits.h \
	$(top_srcdir)/include/spl/sys/int_types.h \
	$(top_srcdir)/include/spl/sys/inttypes.h \
	$(top_srcdir)/include/spl/sys/isa_defs.h \
	$(top_srcdir)/include/spl/sys/kidmap.h \
	$(top_srcdir)/include/spl/sys/kmem.h \
	$(top_srcdir)/include/spl/sys/kmem_cache.h \
	$(top_srcdir)/include/spl/sys/kobj.h \
	$(top_srcdir)/include/spl/sys/kstat.h \
	$(top_srcdir)/include/spl/sys/list.h \
	$(top_srcdir)/include/spl/sys/mkdev.h \
	$(top_srcdir)/include/spl/sys/mntent.h \
	$(top_srcdir)/include/spl/sys/modctl.h \
	$(top_srcdir)/include/spl/sys/mode.h \
	$(top_srcdir)/include/spl/sys/mount.h \
	$(top_srcdir)/include/spl/sys/mutex.h \
	$(top_srcdir)/include/spl/sys/note.h \
	$(top_srcdir)/include/spl/sys/open.h \
	$(top_srcdir)/include/spl/sys/param.h \
	$(top_srcdir)/include/spl/sys/pathname.h \
	$(top_srcdir)/include/spl/sys/policy.h \
	$(top_srcdir)/include/spl/sys/pool.h \
	$(top_srcdir)/include/spl/sys/priv_impl.h \
	$(top_srcdir)/include/spl/sys/processor.h \
	$(top_srcdir)/include/spl/sys/proc.h \
	$(top_srcdir)/include/spl/sys/pset.h \
	$(top_srcdir)/include/spl/sys/random.h \
	$(top_srcdir)/include/spl/sys/refstr.h \
	$(top_srcdir)/include/spl/sys/resource.h \
	$(top_srcdir)/include/spl/sys/rwlock.h \
	$(top_srcdir)/include/spl/sys/sdt.h \
	$(top_srcdir)/include/spl/sys/sid.h \
	$(top_srcdir)/include/spl/sys/signal.h \
	$(top_srcdir)/include/spl/sys/stat.h \
	$(top_srcdir)/include/spl/sys/stropts.h \
	$(top_srcdir)/include/spl/sys/sunddi.h \
	$(top_srcdir)/include/spl/sys/sunldi.h \
	$(top_srcdir)/include/spl/sys/sysdc.h \
	$(top_srcdir)/include/spl/sys/sysevent.h \
	$(top_srcdir)/include/spl/sys/sysmacros.h \
	$(top_srcdir)/include/spl/sys/systeminfo.h \
	$(top_srcdir)/include/spl/sys/systm.h \
	$(top_srcdir)/include/spl/sys/taskq.h \
	$(top_srcdir)/include/spl/sys/thread.h \
	$(top_srcdir)/include/spl/sys/time.h \
	$(top_srcdir)/include/spl/sys/timer.h \
	$(top_srcdir)/include/spl/sys/t_lock.h \
	$(top_srcdir)/include/spl/sys/tsd.h \
	$(top_srcdir)/include/spl/sys/types32.h \
	$(top_srcdir)/include/spl/sys/types.h \
	$(top_srcdir)/include/spl/sys/u8_textprep.h \
	$(top_srcdir)/include/spl/sys/uio.h \
	$(top_srcdir)/include/spl/sys/unistd.h \
	$(top_srcdir)/include/spl/sys/user.h \
	$(top_srcdir)/include/spl/sys/va_list.h \
	$(top_srcdir)/include/spl/sys/varargs.h \
	$(top_srcdir)/include/spl/sys/vfs.h \
	$(top_srcdir)/include/spl/sys/vfs_opreg.h \
	$(top_srcdir)/include/spl/sys/vmem.h \
	$(top_srcdir)/include/spl/sys/vmsystm.h \
	$(top_srcdir)/include/spl/sys/vnode.h \
	$(top_srcdir)/include/spl/sys/zmod.h \
	$(top_srcdir)/include/spl/sys/zone.h

USER_H =

EXTRA_DIST = $(COMMON_H) $(KERNEL_H) $(USER_H)

if CONFIG_KERNEL
kerneldir = @prefix@/src/zfs-$(VERSION)/include/spl/sys
kernel_HEADERS = $(KERNEL_H)
endif

