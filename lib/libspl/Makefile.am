include $(top_srcdir)/config/Rules.am

VPATH = \
	$(top_srcdir)/lib/libspl \
	$(top_srcdir)/lib/libspl/$(TARGET_ASM_DIR)

SUBDIRS = include $(TARGET_ASM_DIR)
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

USER_ASM = atomic.S

nodist_libspl_la_SOURCES = \
	$(USER_C) \
	$(USER_ASM)

libspl_la_LIBADD = -lrt $(LIBTIRPC_LIBS)

EXTRA_DIST = $(USER_C)
