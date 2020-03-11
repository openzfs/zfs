include $(top_srcdir)/config/Rules.am

sbin_PROGRAMS = zstream

zstream_SOURCES = \
	zstream.c \
	zstream.h \
	zstream_dump.c \
	zstream_redup.c

zstream_LDADD = \
	$(top_builddir)/lib/libnvpair/libnvpair.la \
	$(top_builddir)/lib/libzfs/libzfs.la
