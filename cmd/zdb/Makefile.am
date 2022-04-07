include $(top_srcdir)/config/Rules.am

AM_CPPFLAGS += $(FORCEDEBUG_CPPFLAGS)

sbin_PROGRAMS = zdb

zdb_SOURCES = \
	zdb.c \
	zdb_il.c \
	zdb.h

zdb_LDADD = \
	$(abs_top_builddir)/libzpool.la \
	$(abs_top_builddir)/libzfs_core.la \
	$(abs_top_builddir)/libnvpair.la

include $(top_srcdir)/config/CppCheck.am
CPPCHECKTARGETS += zdb
