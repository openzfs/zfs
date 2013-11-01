COMMON_H =

KERNEL_H = \
	$(top_srcdir)/include/linux/bitops_compat.h \
	$(top_srcdir)/include/linux/compiler_compat.h \
	$(top_srcdir)/include/linux/delay_compat.h \
	$(top_srcdir)/include/linux/file_compat.h \
	$(top_srcdir)/include/linux/kallsyms_compat.h \
	$(top_srcdir)/include/linux/list_compat.h \
	$(top_srcdir)/include/linux/math64_compat.h \
	$(top_srcdir)/include/linux/mm_compat.h \
	$(top_srcdir)/include/linux/module_compat.h \
	$(top_srcdir)/include/linux/mutex_compat.h \
	$(top_srcdir)/include/linux/proc_compat.h \
	$(top_srcdir)/include/linux/rwsem_compat.h \
	$(top_srcdir)/include/linux/smp_compat.h \
	$(top_srcdir)/include/linux/sysctl_compat.h \
	$(top_srcdir)/include/linux/time_compat.h \
	$(top_srcdir)/include/linux/uaccess_compat.h \
	$(top_srcdir)/include/linux/zlib_compat.h

USER_H =

EXTRA_DIST = $(COMMON_H) $(KERNEL_H) $(USER_H)

if CONFIG_KERNEL
kerneldir = /usr/src/spl-$(VERSION)/include/linux
kernel_HEADERS = $(KERNEL_H)
endif
