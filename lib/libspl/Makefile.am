include $(top_srcdir)/config/Rules.am

AM_CFLAGS += $(DEBUG_STACKFLAGS) $(FRAME_LARGER_THAN)

SUBDIRS = include $(TARGET_ASM_DIR)
DIST_SUBDIRS = include asm-generic asm-i386 asm-x86_64

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/lib/libspl/include

AM_CCASFLAGS = \
	-I$(top_srcdir)/lib/libspl/include

noinst_LTLIBRARIES = libspl.la

libspl_la_SOURCES = \
	$(top_srcdir)/lib/libspl/getexecname.c \
	$(top_srcdir)/lib/libspl/gethrtime.c \
	$(top_srcdir)/lib/libspl/gethrestime.c \
	$(top_srcdir)/lib/libspl/getmntany.c \
	$(top_srcdir)/lib/libspl/list.c \
	$(top_srcdir)/lib/libspl/mkdirp.c \
	$(top_srcdir)/lib/libspl/strlcat.c \
	$(top_srcdir)/lib/libspl/strlcpy.c \
	$(top_srcdir)/lib/libspl/strnlen.c \
	$(top_srcdir)/lib/libspl/timestamp.c \
	$(top_srcdir)/lib/libspl/zone.c \
	$(top_srcdir)/lib/libspl/xdr.c \
	$(top_srcdir)/lib/libspl/$(TARGET_ASM_DIR)/atomic.S \
	$(top_srcdir)/lib/libspl/include/sys/list.h \
	$(top_srcdir)/lib/libspl/include/sys/list_impl.h

libspl_la_LIBADD = -lrt
