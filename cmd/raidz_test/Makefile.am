include $(top_srcdir)/config/Rules.am

# Includes kernel code, generate warnings for large stack frames
AM_CFLAGS += $(FRAME_LARGER_THAN)

# Unconditionally enable ASSERTs
AM_CPPFLAGS += -DDEBUG -UNDEBUG -DZFS_DEBUG

bin_PROGRAMS = raidz_test

raidz_test_SOURCES = \
	raidz_test.h \
	raidz_test.c \
	raidz_bench.c

raidz_test_LDADD = \
	$(abs_top_builddir)/lib/libzpool/libzpool.la \
	$(abs_top_builddir)/lib/libzfs_core/libzfs_core.la

raidz_test_LDADD += -lm

include $(top_srcdir)/config/CppCheck.am
