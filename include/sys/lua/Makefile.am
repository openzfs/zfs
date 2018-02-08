COMMON_H = \
	$(top_srcdir)/include/sys/lua/lua.h \
	$(top_srcdir)/include/sys/lua/luaconf.h \
	$(top_srcdir)/include/sys/lua/lualib.h \
	$(top_srcdir)/include/sys/lua/lauxlib.h

KERNEL_H =

USER_H =

EXTRA_DIST = $(COMMON_H) $(KERNEL_H) $(USER_H)

if CONFIG_USER
libzfsdir = $(includedir)/libzfs/sys/lua
libzfs_HEADERS = $(COMMON_H) $(USER_H)
endif

if CONFIG_KERNEL
kerneldir = @prefix@/src/zfs-$(VERSION)/include/sys/lua
kernel_HEADERS = $(COMMON_H) $(KERNEL_H)
endif
