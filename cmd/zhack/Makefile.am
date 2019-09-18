include $(top_srcdir)/config/Rules.am

sbin_PROGRAMS = zhack

zhack_SOURCES = \
	zhack.c

zhack_LDADD = \
	$(top_builddir)/lib/libnvpair/libnvpair.la \
	$(top_builddir)/lib/libzpool/libzpool.la
