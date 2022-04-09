zdb_CPPFLAGS = $(AM_CPPFLAGS) $(FORCEDEBUG_CPPFLAGS)

sbin_PROGRAMS   += zdb
CPPCHECKTARGETS += zdb

zdb_SOURCES = \
	%D%/zdb.c \
	%D%/zdb.h \
	%D%/zdb_il.c

zdb_LDADD = \
	libzpool.la \
	libzfs_core.la \
	libnvpair.la
