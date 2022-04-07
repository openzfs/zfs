include $(top_srcdir)/config/Rules.am

AM_CFLAGS += $(KERNEL_CFLAGS)
AM_CPPFLAGS += $(FORCEDEBUG_CPPFLAGS)

bin_PROGRAMS = raidz_test

raidz_test_SOURCES = \
	raidz_test.h \
	raidz_test.c \
	raidz_bench.c

raidz_test_LDADD = \
	$(abs_top_builddir)/libzpool.la \
	$(abs_top_builddir)/libzfs_core.la

raidz_test_LDADD += -lm

include $(top_srcdir)/config/CppCheck.am
CPPCHECKTARGETS += raidz_test
