include $(top_srcdir)/config/Rules.am

pkgexecdir = $(datadir)/@PACKAGE@/zfs-tests/bin

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

pkgexec_PROGRAMS = getholes
getholes_SOURCES = getholes.c
getholes_LDADD = \
	$(top_builddir)/lib/libzfs/libzfs.la \
	$(top_builddir)/lib/libspl/libspl.la