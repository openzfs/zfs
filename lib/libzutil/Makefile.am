libzutil_la_CFLAGS  = $(AM_CFLAGS) $(LIBRARY_CFLAGS)
libzutil_la_CFLAGS += $(LIBBLKID_CFLAGS) $(LIBUDEV_CFLAGS)
libzutil_la_CFLAGS += -fvisibility=hidden

libzutil_la_CPPFLAGS  = $(AM_CPPFLAGS)
libzutil_la_CPPFLAGS += -I$(srcdir)/%D%

noinst_LTLIBRARIES += libzutil.la
CPPCHECKTARGETS    += libzutil.la

libzutil_la_SOURCES = \
	%D%/zutil_device_path.c \
	%D%/zutil_import.c \
	%D%/zutil_import.h \
	%D%/zutil_nicenum.c \
	%D%/zutil_pool.c

if BUILD_LINUX
libzutil_la_SOURCES += \
	%D%/os/linux/zutil_device_path_os.c \
	%D%/os/linux/zutil_import_os.c
endif

if BUILD_FREEBSD
libzutil_la_SOURCES += \
	%D%/os/freebsd/zutil_device_path_os.c \
	%D%/os/freebsd/zutil_import_os.c
endif

libzutil_la_LIBADD = \
	libavl.la \
	libtpool.la \
	libnvpair.la \
	libspl.la

if BUILD_LINUX
libzutil_la_LIBADD += \
	libefi.la \
	-lrt
endif

libzutil_la_LIBADD += -lm $(LIBBLKID_LIBS) $(LIBUDEV_LIBS)
