include $(top_srcdir)/config/Rules.am

pkgexecdir = $(datadir)/@PACKAGE@/zfs-tests/bin

pkgexec_PROGRAMS = mkfile
mkfile_SOURCES = mkfile.c
if BUILD_FREEBSD
mkfile_LDADD = -L/usr/local/lib -lintl
endif
