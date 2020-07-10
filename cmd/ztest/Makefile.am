include $(top_srcdir)/config/Rules.am

# Get rid of compiler warning for unchecked truncating snprintfs on gcc 7.1.1
AM_CFLAGS += $(NO_FORMAT_TRUNCATION)

# Includes kernel code, generate warnings for large stack frames
AM_CFLAGS += $(FRAME_LARGER_THAN)

# Unconditionally enable ASSERTs
AM_CPPFLAGS += -DDEBUG -UNDEBUG -DZFS_DEBUG

sbin_PROGRAMS = ztest

ztest_SOURCES = \
	ztest.c

ztest_LDADD = \
	$(abs_top_builddir)/lib/libzpool/libzpool.la \
	$(abs_top_builddir)/lib/libzfs_core/libzfs_core.la \
	$(abs_top_builddir)/lib/libnvpair/libnvpair.la

ztest_LDADD += -lm
ztest_LDFLAGS = -pthread
