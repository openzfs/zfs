include $(srcdir)/%D%/include/Makefile.am

libspl_assert_la_CFLAGS = $(AM_CFLAGS) $(LIBRARY_CFLAGS)
libspl_la_CFLAGS        = $(libspl_assert_la_CFLAGS)

noinst_LTLIBRARIES += libspl_assert.la libspl.la
CPPCHECKTARGETS    += libspl_assert.la libspl.la

libspl_assert_la_SOURCES = \
	%D%/assert.c

libspl_la_SOURCES = \
	%D%/libspl_impl.h \
	%D%/atomic.c \
	%D%/getexecname.c \
	%D%/list.c \
	%D%/mkdirp.c \
	%D%/page.c \
	%D%/strlcat.c \
	%D%/strlcpy.c \
	%D%/timestamp.c \
	%D%/include/sys/list.h \
	%D%/include/sys/list_impl.h

if BUILD_LINUX
libspl_la_SOURCES += \
	%D%/os/linux/getexecname.c \
	%D%/os/linux/gethostid.c \
	%D%/os/linux/getmntany.c \
	%D%/os/linux/zone.c
endif

if BUILD_FREEBSD
libspl_la_SOURCES += \
	%D%/os/freebsd/getexecname.c \
	%D%/os/freebsd/gethostid.c \
	%D%/os/freebsd/getmntany.c \
	%D%/os/freebsd/mnttab.c \
	%D%/os/freebsd/zone.c
endif

libspl_la_LIBADD = \
	libspl_assert.la

libspl_la_LIBADD += $(LIBATOMIC_LIBS) $(LIBCLOCK_GETTIME)
