include $(top_srcdir)/config/Rules.am

# Suppress unused but set variable warnings often due to ASSERTs
AM_CFLAGS += $(NO_UNUSED_BUT_SET_VARIABLE)

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

noinst_LTLIBRARIES = libzutil.la

USER_C = \
	zutil_device_path.c \
	zutil_import.c \
	zutil_nicenum.c \
	zutil_pool.c

nodist_libzutil_la_SOURCES = $(USER_C)

libzutil_la_LIBADD = \
	$(top_builddir)/lib/libavl/libavl.la \
        $(top_builddir)/lib/libefi/libefi.la \
	$(top_builddir)/lib/libtpool/libtpool.la

libzutil_la_LIBADD += -lm $(LIBBLKID) $(LIBUDEV)

EXTRA_DIST = $(USER_C)
