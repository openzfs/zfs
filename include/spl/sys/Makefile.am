KERNEL_H = \
	$(top_srcdir)/include/spl/sys/acl.h \
	$(top_srcdir)/include/spl/sys/atomic.h \
	$(top_srcdir)/include/spl/sys/byteorder.h \
	$(top_srcdir)/include/spl/sys/callb.h \
	$(top_srcdir)/include/spl/sys/callo.h \
	$(top_srcdir)/include/spl/sys/cmn_err.h \
	$(top_srcdir)/include/spl/sys/condvar.h \
	$(top_srcdir)/include/spl/sys/console.h \
	$(top_srcdir)/include/spl/sys/cred.h \
	$(top_srcdir)/include/spl/sys/ctype.h \
	$(top_srcdir)/include/spl/sys/debug.h \
	$(top_srcdir)/include/spl/sys/disp.h \
	$(top_srcdir)/include/spl/sys/dkio.h \
	$(top_srcdir)/include/spl/sys/errno.h \
	$(top_srcdir)/include/spl/sys/fcntl.h \
	$(top_srcdir)/include/spl/sys/file.h \
	$(top_srcdir)/include/spl/sys/inttypes.h \
	$(top_srcdir)/include/spl/sys/isa_defs.h \
	$(top_srcdir)/include/spl/sys/kmem_cache.h \
	$(top_srcdir)/include/spl/sys/kmem.h \
	$(top_srcdir)/include/spl/sys/kobj.h \
	$(top_srcdir)/include/spl/sys/kstat.h \
	$(top_srcdir)/include/spl/sys/list.h \
	$(top_srcdir)/include/spl/sys/mode.h \
	$(top_srcdir)/include/spl/sys/mutex.h \
	$(top_srcdir)/include/spl/sys/param.h \
	$(top_srcdir)/include/spl/sys/processor.h \
	$(top_srcdir)/include/spl/sys/proc.h \
	$(top_srcdir)/include/spl/sys/procfs_list.h \
	$(top_srcdir)/include/spl/sys/random.h \
	$(top_srcdir)/include/spl/sys/rwlock.h \
	$(top_srcdir)/include/spl/sys/shrinker.h \
	$(top_srcdir)/include/spl/sys/sid.h \
	$(top_srcdir)/include/spl/sys/signal.h \
	$(top_srcdir)/include/spl/sys/stat.h \
	$(top_srcdir)/include/spl/sys/strings.h \
	$(top_srcdir)/include/spl/sys/sunddi.h \
	$(top_srcdir)/include/spl/sys/sysmacros.h \
	$(top_srcdir)/include/spl/sys/systeminfo.h \
	$(top_srcdir)/include/spl/sys/taskq.h \
	$(top_srcdir)/include/spl/sys/thread.h \
	$(top_srcdir)/include/spl/sys/time.h \
	$(top_srcdir)/include/spl/sys/timer.h \
	$(top_srcdir)/include/spl/sys/tsd.h \
	$(top_srcdir)/include/spl/sys/types32.h \
	$(top_srcdir)/include/spl/sys/types.h \
	$(top_srcdir)/include/spl/sys/uio.h \
	$(top_srcdir)/include/spl/sys/user.h \
	$(top_srcdir)/include/spl/sys/vfs.h \
	$(top_srcdir)/include/spl/sys/vmem.h \
	$(top_srcdir)/include/spl/sys/vmsystm.h \
	$(top_srcdir)/include/spl/sys/vnode.h \
	$(top_srcdir)/include/spl/sys/wait.h \
	$(top_srcdir)/include/spl/sys/zmod.h \
	$(top_srcdir)/include/spl/sys/zone.h

if CONFIG_KERNEL
kerneldir = @prefix@/src/zfs-$(VERSION)/include/spl/sys
kernel_HEADERS = $(KERNEL_H)
endif
