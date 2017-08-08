COMMON_H =

KERNEL_H = \
	$(top_srcdir)/include/spl/vm/anon.h \
	$(top_srcdir)/include/spl/vm/pvn.h \
	$(top_srcdir)/include/spl/vm/seg_kmem.h

USER_H =

EXTRA_DIST = $(COMMON_H) $(KERNEL_H) $(USER_H)

if CONFIG_KERNEL
kerneldir = @prefix@/src/zfs-$(VERSION)/include/spl/vm
kernel_HEADERS = $(KERNEL_H)
endif
