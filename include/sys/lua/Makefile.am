COMMON_H = \
	lua.h \
	luaconf.h \
	lualib.h \
	lauxlib.h

if CONFIG_USER
libzfsdir = $(includedir)/libzfs/sys/lua
libzfs_HEADERS = $(COMMON_H)
endif

if CONFIG_KERNEL
kerneldir = @prefix@/src/zfs-$(VERSION)/include/sys/lua
kernel_HEADERS = $(COMMON_H)
endif
