include $(top_srcdir)/config/Rules.am

if TARGET_CPU_I386
TARGET_CPU_ATOMIC_SOURCE = asm-i386/atomic.S
else
if TARGET_CPU_X86_64
TARGET_CPU_ATOMIC_SOURCE = asm-x86_64/atomic.S
else
TARGET_CPU_ATOMIC_SOURCE = asm-generic/atomic.c
endif
endif

SUBDIRS = include

AM_CCASFLAGS = \
	$(CFLAGS)

noinst_LTLIBRARIES = libspl_assert.la libspl.la

libspl_assert_la_SOURCES = \
	assert.c

USER_C = \
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

libspl_la_SOURCES = \
	$(USER_C) \
	$(TARGET_CPU_ATOMIC_SOURCE)

libspl_la_LIBADD = \
	libspl_assert.la

libspl_la_LIBADD += $(LIBCLOCK_GETTIME)

include $(top_srcdir)/config/CppCheck.am

# Override the default SOURCES which includes TARGET_CPU_ATOMIC_SOURCE
# in order to always evaluate the generic asm-generic/atomic.c source.
CPPCHECKSRC = $(USER_C) asm-generic/atomic.c
cppcheck:
	$(CPPCHECK) -j$(CPU_COUNT) $(CPPCHECKFLAGS) --force \
		$(DEFAULT_INCLUDES) $(CPPCHECKSRC)
