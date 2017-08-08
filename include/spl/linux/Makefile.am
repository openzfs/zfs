COMMON_H =

KERNEL_H = \
	$(top_srcdir)/include/spl/linux/bitops_compat.h \
	$(top_srcdir)/include/spl/linux/compiler_compat.h \
	$(top_srcdir)/include/spl/linux/delay_compat.h \
	$(top_srcdir)/include/spl/linux/file_compat.h \
	$(top_srcdir)/include/spl/linux/list_compat.h \
	$(top_srcdir)/include/spl/linux/math64_compat.h \
	$(top_srcdir)/include/spl/linux/mm_compat.h \
	$(top_srcdir)/include/spl/linux/proc_compat.h \
	$(top_srcdir)/include/spl/linux/ratelimit_compat.h \
	$(top_srcdir)/include/spl/linux/rwsem_compat.h \
	$(top_srcdir)/include/spl/linux/wait_compat.h \
	$(top_srcdir)/include/spl/linux/zlib_compat.h

USER_H =

EXTRA_DIST = $(COMMON_H) $(KERNEL_H) $(USER_H)

if CONFIG_KERNEL
kerneldir = @prefix@/src/zfs-$(VERSION)/include/spl/linux
kernel_HEADERS = $(KERNEL_H)
endif
