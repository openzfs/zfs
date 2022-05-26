libzfs_core_la_CFLAGS  = $(AM_CFLAGS) $(LIBRARY_CFLAGS)
libzfs_core_la_CFLAGS += -fvisibility=hidden

lib_LTLIBRARIES += libzfs_core.la
CPPCHECKTARGETS += libzfs_core.la

libzfs_core_la_SOURCES = \
	%D%/libzfs_core.c

if BUILD_LINUX
libzfs_core_la_SOURCES += \
	%D%/os/linux/libzfs_core_ioctl.c
endif

libzfs_core_la_CPPFLAGS  = $(AM_CPPFLAGS)
if BUILD_FREEBSD
libzfs_core_la_CPPFLAGS += -Iinclude/os/freebsd/zfs

libzfs_core_la_SOURCES += \
	%D%/os/freebsd/libzfs_core_ioctl.c

nodist_libzfs_core_la_SOURCES = \
	module/os/freebsd/zfs/zfs_ioctl_compat.c
endif

libzfs_core_la_LIBADD = \
	libnvpair.la \
	libspl.la

libzfs_core_la_LIBADD += $(LTLIBINTL)

libzfs_core_la_LDFLAGS = -pthread

if !ASAN_ENABLED
libzfs_core_la_LDFLAGS += -Wl,-z,defs
endif

if BUILD_FREEBSD
libzfs_core_la_LIBADD += -lutil -lgeom
endif

libzfs_core_la_LDFLAGS += -version-info 3:0:0

pkgconfig_DATA += %D%/libzfs_core.pc

dist_noinst_DATA += %D%/libzfs_core.abi %D%/libzfs_core.suppr
