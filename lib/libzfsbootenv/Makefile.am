include $(top_srcdir)/config/Rules.am
PHONY =

pkgconfig_DATA = libzfsbootenv.pc

lib_LTLIBRARIES = libzfsbootenv.la

include $(top_srcdir)/config/Abigail.am

if BUILD_FREEBSD
DEFAULT_INCLUDES += -I$(top_srcdir)/include/os/freebsd/zfs
endif
if BUILD_LINUX
DEFAULT_INCLUDES += -I$(top_srcdir)/include/os/linux/zfs
endif

USER_C = \
	lzbe_device.c \
	lzbe_pair.c \
	lzbe_util.c

dist_libzfsbootenv_la_SOURCES = \
	$(USER_C)

libzfsbootenv_la_LIBADD = \
	$(abs_top_builddir)/lib/libzfs/libzfs.la \
	$(abs_top_builddir)/lib/libnvpair/libnvpair.la

libzfsbootenv_la_LDFLAGS =

if !ASAN_ENABLED
libzfsbootenv_la_LDFLAGS += -Wl,-z,defs
endif

libzfsbootenv_la_LDFLAGS += -version-info 1:0:0

# Library ABI
EXTRA_DIST = libzfsbootenv.abi libzfsbootenv.suppr
