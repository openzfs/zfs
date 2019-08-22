include $(top_srcdir)/config/Rules.am

pkgexecdir = $(datadir)/@PACKAGE@/zfs-tests/bin

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

mkholes_LDADD = \
	$(top_builddir)/lib/libzfs/libzfs.la

pkgexec_PROGRAMS = mkholes
mkholes_SOURCES = mkholes.c
