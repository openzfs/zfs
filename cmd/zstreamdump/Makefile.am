include $(top_srcdir)/config/Rules.am

sbin_PROGRAMS = zstreamdump

zstreamdump_SOURCES = \
	zstreamdump.c

zstreamdump_LDADD = \
	$(top_builddir)/lib/libnvpair/libnvpair.la \
	$(top_builddir)/lib/libzfs/libzfs.la
