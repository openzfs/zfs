SUBDIRS = fs linux rpc sharefs sys util vm

COMMON_H =

KERNEL_H = \
	$(top_srcdir)/include/strings.h

USER_H =

EXTRA_DIST = $(COMMON_H) $(KERNEL_H) $(USER_H)

if CONFIG_KERNEL
kerneldir = @prefix@/src/spl-$(VERSION)/include
kernel_HEADERS = $(KERNEL_H)
endif
