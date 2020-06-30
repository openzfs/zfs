include $(top_srcdir)/config/Rules.am

# Unconditionally enable debugging for zdb
AM_CPPFLAGS += -DDEBUG -UNDEBUG

sbin_PROGRAMS = zdb

zdb_SOURCES = \
	zdb.c \
	zdb_il.c \
	zdb.h

zdb_LDADD = \
	$(top_builddir)/lib/libzpool/libzpool.la \
	$(top_builddir)/lib/libzfs_core/libzfs_core.la \
	$(top_builddir)/lib/libnvpair/libnvpair.la
