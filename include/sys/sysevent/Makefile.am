COMMON_H =

KERNEL_H = \
	$(top_srcdir)/include/sys/sysevent/eventdefs.h

USER_H =

EXTRA_DIST = $(COMMON_H) $(KERNEL_H) $(USER_H)

if CONFIG_KERNEL
kerneldir = /usr/src/spl-$(SPL_META_VERSION)-$(SPL_META_RELEASE)/$(LINUX_VERSION)/sys/sysevent
kernel_HEADERS = $(KERNEL_H)
endif
