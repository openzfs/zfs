include $(top_srcdir)/config/Rules.am

# Suppress unused but set variable warnings often due to ASSERTs
AM_CFLAGS += $(NO_UNUSED_BUT_SET_VARIABLE)

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
USER_C += \
	os/freebsd/zutil_device_path_os.c \
	os/freebsd/zutil_import_os.c \
	os/freebsd/zutil_compat.c
endif

libzutil_la_SOURCES = $(USER_C)

libzutil_la_LIBADD = \
	$(top_builddir)/lib/libavl/libavl.la \
	$(top_builddir)/lib/libtpool/libtpool.la

if BUILD_LINUX
libzutil_la_LIBADD += \
        $(top_builddir)/lib/libefi/libefi.la
endif

libzutil_la_LIBADD += -lm $(LIBBLKID) $(LIBUDEV)
