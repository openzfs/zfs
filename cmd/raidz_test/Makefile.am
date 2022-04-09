raidz_test_CFLAGS   = $(AM_CFLAGS)   $(KERNEL_CFLAGS)
raidz_test_CPPFLAGS = $(AM_CPPFLAGS) $(FORCEDEBUG_CPPFLAGS)

bin_PROGRAMS    += raidz_test
CPPCHECKTARGETS += raidz_test

raidz_test_SOURCES = \
	%D%/raidz_bench.c \
	%D%/raidz_test.c \
	%D%/raidz_test.h

raidz_test_LDADD = \
	libzpool.la \
	libzfs_core.la

raidz_test_LDADD += -lm
