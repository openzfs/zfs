include $(top_srcdir)/config/Rules.am

# Includes kernel code, generate warnings for large stack frames
AM_CFLAGS += $(FRAME_LARGER_THAN)

# Unconditionally enable ASSERTs
AM_CPPFLAGS += -DDEBUG -UNDEBUG

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

bin_PROGRAMS = raidz_test

raidz_test_SOURCES = \
	raidz_test.h \
	raidz_test.c \
	raidz_bench.c

raidz_test_LDADD = \
	$(top_builddir)/lib/libzpool/libzpool.la

raidz_test_LDADD += -lm -ldl
