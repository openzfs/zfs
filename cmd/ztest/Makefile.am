include $(top_srcdir)/config/Rules.am

AM_CFLAGS += $(KERNEL_CFLAGS)

# Get rid of compiler warning for unchecked truncating snprintfs on gcc 7.1.1
AM_CFLAGS += $(NO_FORMAT_TRUNCATION)

AM_CPPFLAGS += $(FORCEDEBUG_CPPFLAGS)

sbin_PROGRAMS = ztest

ztest_SOURCES = \
	ztest.c

ztest_LDADD = \
	$(abs_top_builddir)/libzpool.la \
	$(abs_top_builddir)/libzfs_core.la \
	$(abs_top_builddir)/libnvpair.la

ztest_LDADD += -lm
ztest_LDFLAGS = -pthread

include $(top_srcdir)/config/CppCheck.am
CPPCHECKTARGETS += ztest
