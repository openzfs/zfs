include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libcommon

noinst_PROGRAMS = spl
sbin_PROGRAMS = splat

spl_SOURCES = spl.c

splat_SOURCES = splat.c
splat_LDFLAGS = $(top_builddir)/lib/libcommon/libcommon.la

EXTRA_DIST = splat.h
