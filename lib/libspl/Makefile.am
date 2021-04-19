include $(top_srcdir)/config/Rules.am

SUBDIRS = include

noinst_LTLIBRARIES = libspl_assert.la libspl.la

libspl_assert_la_SOURCES = \
	assert.c

USER_C = \
	atomic.c \
	list.c \
	mkdirp.c \
	page.c \
	strlcat.c \
	strlcpy.c \
	timestamp.c \
	include/sys/list.h \
	include/sys/list_impl.h

if BUILD_LINUX
USER_C += \
	os/linux/getexecname.c \
	os/linux/gethostid.c \
	os/linux/getmntany.c \
	os/linux/zone.c
endif

if BUILD_FREEBSD
USER_C += \
	os/freebsd/getexecname.c \
	os/freebsd/gethostid.c \
	os/freebsd/getmntany.c \
	os/freebsd/mnttab.c \
	os/freebsd/zone.c
endif

libspl_la_SOURCES = $(USER_C)

libspl_la_LIBADD = \
	libspl_assert.la

libspl_la_LIBADD += $(LIBATOMIC_LIBS) $(LIBCLOCK_GETTIME)

include $(top_srcdir)/config/CppCheck.am
