include $(top_srcdir)/config/Rules.am

pkgexecdir = $(datadir)/@PACKAGE@/zfs-tests/bin

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

pkgexec_PROGRAMS = fiemap
fiemap_SOURCES = fiemap.c
fiemap_LDADD = \
	$(top_builddir)/lib/libavl/libavl.la \
	$(top_builddir)/lib/libzpool/libzpool.la
