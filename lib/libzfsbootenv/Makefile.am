libzfsbootenv_la_CFLAGS  = $(AM_CFLAGS) $(LIBRARY_CFLAGS)
libzfsbootenv_la_CFLAGS += -fvisibility=hidden

lib_LTLIBRARIES += libzfsbootenv.la
CPPCHECKTARGETS += libzfsbootenv.la

libzfsbootenv_la_CPPFLAGS  = $(AM_CPPFLAGS)
libzfsbootenv_la_CPPFLAGS += -I$(srcdir)/include/os/@ac_system_l@/zfs

dist_libzfsbootenv_la_SOURCES = \
	%D%/lzbe_device.c \
	%D%/lzbe_pair.c \
	%D%/lzbe_util.c

libzfsbootenv_la_LIBADD = \
	libzfs.la \
	libnvpair.la

libzfsbootenv_la_LDFLAGS =

if !ASAN_ENABLED
libzfsbootenv_la_LDFLAGS += -Wl,-z,defs
endif

libzfsbootenv_la_LDFLAGS += -version-info 1:0:0

pkgconfig_DATA += %D%/libzfsbootenv.pc

dist_noinst_DATA += %D%/libzfsbootenv.abi %D%/libzfsbootenv.suppr
