include $(top_srcdir)/config/Rules.am

sbin_PROGRAMS = zstream

zstream_SOURCES = \
	zstream.c \
	zstream.h \
	zstream_dump.c \
	zstream_redup.c \
	zstream_token.c

zstream_LDADD = \
	$(abs_top_builddir)/lib/libzfs/libzfs.la \
	$(abs_top_builddir)/lib/libzfs_core/libzfs_core.la \
	$(abs_top_builddir)/lib/libnvpair/libnvpair.la
