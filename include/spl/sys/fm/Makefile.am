COMMON_H =

KERNEL_H = \
	$(top_srcdir)/include/spl/sys/fm/protocol.h \
	$(top_srcdir)/include/spl/sys/fm/util.h

USER_H =

EXTRA_DIST = $(COMMON_H) $(KERNEL_H) $(USER_H)

if CONFIG_KERNEL
kerneldir = @prefix@/src/zfs-$(VERSION)/include/spl/sys/fm
kernel_HEADERS = $(KERNEL_H)
endif
