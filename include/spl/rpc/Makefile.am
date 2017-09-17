COMMON_H =

KERNEL_H = \
	$(top_srcdir)/include/spl/rpc/types.h \
	$(top_srcdir)/include/spl/rpc/xdr.h

USER_H =

EXTRA_DIST = $(COMMON_H) $(KERNEL_H) $(USER_H)

if CONFIG_KERNEL
kerneldir = @prefix@/src/zfs-$(VERSION)/include/spl/rpc
kernel_HEADERS = $(KERNEL_H)
endif
