include $(top_srcdir)/config/Rules.am

VPATH = \
	$(top_srcdir)/lib/libspl \
	$(top_srcdir)/lib/libspl/$(TARGET_ASM_DIR)

AM_CFLAGS += $(DEBUG_STACKFLAGS) $(FRAME_LARGER_THAN)

SUBDIRS = include $(TARGET_ASM_DIR)
DIST_SUBDIRS = include asm-generic asm-i386 asm-x86_64

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/lib/libspl/include

AM_CCASFLAGS = \
	-I$(top_srcdir)/lib/libspl/include

noinst_LTLIBRARIES = libspl.la

USER_C = \
	getexecname.c \
	gethrtime.c \
	gethrestime.c \
	getmntany.c \
	list.c \
	mkdirp.c \
	strlcat.c \
	strlcpy.c \
	strnlen.c \
	timestamp.c \
	zone.c \
	xdr.c \
	include/sys/list.h \
	include/sys/list_impl.h

USER_ASM = atomic.S

KERNEL_C =

nodist_libspl_la_SOURCES = \
	$(USER_C) \
	$(USER_ASM) \
	$(KERNEL_C)

libspl_la_LIBADD = -lrt

EXTRA_DIST = $(USER_C)
