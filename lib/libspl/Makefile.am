include $(top_srcdir)/config/Rules.am

if TARGET_CPU_I386
TARGET_CPU_DIR = asm-i386
else
if TARGET_CPU_X86_64
TARGET_CPU_DIR = asm-x86_64
else
TARGET_CPU_DIR = asm-generic
endif
endif

VPATH = \
	$(top_srcdir)/lib/libspl \
	$(top_srcdir)/lib/libspl/$(TARGET_CPU_DIR)

SUBDIRS = include $(TARGET_CPU_DIR)
DIST_SUBDIRS = include asm-generic asm-i386 asm-x86_64

AM_CFLAGS += $(LIBTIRPC_CFLAGS)

AM_CCASFLAGS = \
	$(CFLAGS)

noinst_LTLIBRARIES = libspl.la

USER_C = \
	list.c \
	mkdirp.c \
	page.c \
	strlcat.c \
	strlcpy.c \
	timestamp.c \
	zone.c \
	include/sys/list.h \
	include/sys/list_impl.h

if BUILD_LINUX
USER_C += \
	os/linux/getexecname.c \
	os/linux/gethostid.c \
	os/linux/getmntany.c
endif

if BUILD_FREEBSD
USER_C += \
	os/freebsd/getexecname.c \
	os/freebsd/gethostid.c \
	os/freebsd/getmntany.c \
	os/freebsd/mnttab.c

endif

USER_ASM = atomic.S

nodist_libspl_la_SOURCES = \
	$(USER_C) \
	$(USER_ASM)

libspl_la_LIBADD = -lrt $(LIBTIRPC_LIBS)

EXTRA_DIST = $(USER_C)
