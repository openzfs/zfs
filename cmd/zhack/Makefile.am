include $(top_srcdir)/config/Rules.am

sbin_PROGRAMS = zhack

zhack_SOURCES = \
	zhack.c

zhack_LDADD = \
	$(abs_top_builddir)/lib/libzpool/libzpool.la \
	$(abs_top_builddir)/lib/libzfs_core/libzfs_core.la \
	$(abs_top_builddir)/lib/libnvpair/libnvpair.la
