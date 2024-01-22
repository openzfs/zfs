zdb_CPPFLAGS = $(AM_CPPFLAGS) $(FORCEDEBUG_CPPFLAGS)
zdb_CFLAGS   = $(AM_CFLAGS) $(LIBCRYPTO_CFLAGS)

sbin_PROGRAMS   += zdb
CPPCHECKTARGETS += zdb

zdb_SOURCES = \
	%D%/zdb.c \
	%D%/zdb.h \
	%D%/zdb_il.c

zdb_LDADD = \
	libzdb.la \
	libzpool.la \
	libzfs_core.la \
	libnvpair.la

zdb_LDADD += $(LIBCRYPTO_LIBS)
