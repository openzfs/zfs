include $(top_srcdir)/config/Rules.am

# Suppress unused but set variable warnings often due to ASSERTs
AM_CFLAGS += $(NO_UNUSED_BUT_SET_VARIABLE)
AM_CFLAGS += $(LIBBLKID_CFLAGS) $(LIBUDEV_CFLAGS)

DEFAULT_INCLUDES += -I$(srcdir)

noinst_LTLIBRARIES = libzutil.la

USER_C = \
	zutil_device_path.c \
	zutil_import.c \
	zutil_import.h \
	zutil_nicenum.c \
	zutil_pool.c

if BUILD_LINUX
USER_C += \
	os/linux/zutil_device_path_os.c \
	os/linux/zutil_import_os.c \
	os/linux/zutil_compat.c
endif

if BUILD_FREEBSD
DEFAULT_INCLUDES += -I$(top_srcdir)/include/os/freebsd/zfs

USER_C += \
	os/freebsd/zutil_device_path_os.c \
	os/freebsd/zutil_import_os.c \
	os/freebsd/zutil_compat.c

VPATH += $(top_srcdir)/module/os/freebsd/zfs

nodist_libzutil_la_SOURCES = zfs_ioctl_compat.c
endif

libzutil_la_SOURCES = $(USER_C)

libzutil_la_LIBADD = \
	$(abs_top_builddir)/lib/libavl/libavl.la \
	$(abs_top_builddir)/lib/libtpool/libtpool.la \
	$(abs_top_builddir)/lib/libnvpair/libnvpair.la \
	$(abs_top_builddir)/lib/libspl/libspl.la

if BUILD_LINUX
libzutil_la_LIBADD += \
        $(abs_top_builddir)/lib/libefi/libefi.la
endif

libzutil_la_LIBADD += -lm $(LIBBLKID_LIBS) $(LIBUDEV_LIBS)
