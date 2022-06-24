sbin_PROGRAMS   += zstream
CPPCHECKTARGETS += zstream

zstream_SOURCES = \
	%D%/zstream.c \
	%D%/zstream.h \
	%D%/zstream_decompress.c \
	%D%/zstream_dump.c \
	%D%/zstream_redup.c \
	%D%/zstream_token.c

zstream_LDADD = \
	libzfs.la \
	libzfs_core.la \
	libzpool.la \
	libnvpair.la

PHONY += install-exec-hook
install-exec-hook:
	cd $(DESTDIR)$(sbindir) && $(LN_S) -f zstream zstreamdump
