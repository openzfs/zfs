include $(top_srcdir)/config/Rules.am

sbin_PROGRAMS = zstream

zstream_SOURCES = \
	zstream.c \
	zstream.h \
	zstream_dump.c \
	zstream_redup.c \
	zstream_token.c

zstream_LDADD = \
	$(abs_top_builddir)/libzfs.la \
	$(abs_top_builddir)/libzfs_core.la \
	$(abs_top_builddir)/libnvpair.la

include $(top_srcdir)/config/CppCheck.am
CPPCHECKTARGETS += zstream

install-exec-hook:
	cd $(DESTDIR)$(sbindir) && $(LN_S) -f zstream zstreamdump
