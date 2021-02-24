include $(top_srcdir)/config/Rules.am

pkgexecdir = $(datadir)/@PACKAGE@/zfs-tests/bin

pkgexec_PROGRAMS = send_doall

send_doall_SOURCES = send_doall.c
send_doall_LDADD = \
	$(abs_top_builddir)/lib/libzfs_core/libzfs_core.la \
	$(abs_top_builddir)/lib/libzfs/libzfs.la \
	$(abs_top_builddir)/lib/libnvpair/libnvpair.la
