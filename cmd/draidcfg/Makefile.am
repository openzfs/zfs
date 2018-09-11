include $(top_srcdir)/config/Rules.am

AM_CPPFLAGS += -DDEBUG

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

bin_PROGRAMS = draidcfg

draidcfg_SOURCES = \
	draidcfg.c \
	draid_permutation.c \
	draid_permutation.h

draidcfg_LDADD = \
	$(top_builddir)/lib/libnvpair/libnvpair.la \
	$(top_builddir)/lib/libzpool/libzpool.la \
	$(top_builddir)/lib/libzfs/libzfs.la
draidcfg_LDADD += -lm
