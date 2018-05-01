include $(top_srcdir)/config/Rules.am

pkgexecdir = $(datadir)/@PACKAGE@/zfs-tests/bin

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/lib/libspl/include

pkgexec_PROGRAMS = mkholes
mkholes_SOURCES = mkholes.c
mkholes_LDADD = \
	$(top_builddir)/lib/libspl/libspl.la
