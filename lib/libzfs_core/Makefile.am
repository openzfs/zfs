include $(top_srcdir)/config/Rules.am

pkgconfig_DATA = libzfs_core.pc

AM_CFLAGS += -fvisibility=hidden

lib_LTLIBRARIES = libzfs_core.la

include $(top_srcdir)/config/Abigail.am

USER_C = \
	libzfs_core.c

if BUILD_LINUX
USER_C += \
        os/linux/libzfs_core_ioctl.c
endif

if BUILD_FREEBSD
DEFAULT_INCLUDES += -I$(top_srcdir)/include/os/freebsd/zfs

USER_C += \
        os/freebsd/libzfs_core_ioctl.c

VPATH += $(top_srcdir)/module/os/freebsd/zfs

nodist_libzfs_core_la_SOURCES = zfs_ioctl_compat.c
endif

libzfs_core_la_SOURCES = $(USER_C)

libzfs_core_la_LIBADD = \
	$(abs_top_builddir)/lib/libnvpair/libnvpair.la \
	$(abs_top_builddir)/lib/libspl/libspl.la

libzfs_core_la_LIBADD += $(LTLIBINTL)

libzfs_core_la_LDFLAGS = -pthread

if !ASAN_ENABLED
libzfs_core_la_LDFLAGS += -Wl,-z,defs
endif

if BUILD_FREEBSD
libzfs_core_la_LIBADD += -lutil -lgeom
endif

libzfs_core_la_LDFLAGS += -version-info 3:0:0

include $(top_srcdir)/config/CppCheck.am

# Library ABI
EXTRA_DIST = libzfs_core.abi libzfs_core.suppr
