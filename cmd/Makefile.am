DEFAULT_INCLUDES = -I. -I.. -I../lib
AM_CFLAGS = -g -O2 -W -Wall -Wstrict-prototypes -Wshadow

sbin_PROGRAMS = splat
splat_SOURCES = splat.c
splat_LDFLAGS = $(top_builddir)/lib/libcommon.la

EXTRA_DIST = splat.h
