include $(top_srcdir)/config/Rules.am

pkgexecdir = $(datadir)/@PACKAGE@/zfs-tests/bin

pkgexec_PROGRAMS = mkbusy
mkbusy_SOURCES = mkbusy.c

mkbusy_LDADD = $(abs_top_builddir)/lib/libzfs_core/libzfs_core.la
