COMMON_H =

KERNEL_H = \
	$(top_srcdir)/include/spl/util/qsort.h \
	$(top_srcdir)/include/spl/util/sscanf.h

USER_H =

EXTRA_DIST = $(COMMON_H) $(KERNEL_H) $(USER_H)

if CONFIG_KERNEL
kerneldir = @prefix@/src/zfs-$(VERSION)/include/spl/util
kernel_HEADERS = $(KERNEL_H)
endif
