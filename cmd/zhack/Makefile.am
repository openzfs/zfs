zhack_CPPFLAGS = $(AM_CPPFLAGS) $(FORCEDEBUG_CPPFLAGS)

sbin_PROGRAMS   += zhack
CPPCHECKTARGETS += zhack

zhack_SOURCES = \
	%D%/zhack.c

zhack_LDADD = \
	libzpool.la \
	libzfs_core.la \
	libnvpair.la
