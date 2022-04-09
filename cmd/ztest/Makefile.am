ztest_CFLAGS    = $(AM_CFLAGS) $(KERNEL_CFLAGS)
# Get rid of compiler warning for unchecked truncating snprintfs on gcc 7.1.1
ztest_CFLAGS   += $(NO_FORMAT_TRUNCATION)
ztest_CPPFLAGS  = $(AM_CPPFLAGS) $(FORCEDEBUG_CPPFLAGS)

sbin_PROGRAMS   += ztest
CPPCHECKTARGETS += ztest

ztest_SOURCES = \
	%D%/ztest.c

ztest_LDADD = \
	libzpool.la \
	libzfs_core.la \
	libnvpair.la

ztest_LDADD += -lm
ztest_LDFLAGS = -pthread
