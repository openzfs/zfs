# SPDX-License-Identifier: CDDL-1.0
zstream_CPPFLAGS = $(AM_CPPFLAGS) $(LIBZPOOL_CPPFLAGS)

sbin_PROGRAMS   += zstream
CPPCHECKTARGETS += zstream

zstream_SOURCES = \
	%D%/zstream.c \
	%D%/zstream.h \
	%D%/zstream_byteswap.c \
	%D%/zstream_byteswap.h \
	%D%/zstream_chain.c \
	%D%/zstream_chain.h \
	%D%/zstream_decompress.c \
	%D%/zstream_drop_record.c \
	%D%/zstream_dump.c \
	%D%/zstream_fletcher4.c \
	%D%/zstream_fletcher4.h \
	%D%/zstream_io.c \
	%D%/zstream_io.h \
	%D%/zstream_modules.h \
	%D%/zstream_recompress.c \
	%D%/zstream_recompress.h \
	%D%/zstream_redup.c \
	%D%/zstream_token.c \
	%D%/zstream_util.c \
	%D%/zstream_util.h \
	%D%/zstream_validate.c \
	%D%/zstream_validate.h

zstream_LDADD = \
	libzfs.la \
	libzfs_core.la \
	libzpool.la \
	libnvpair.la

cmd-zstream-install-exec-hook:
	cd $(DESTDIR)$(sbindir) && $(LN_S) -f zstream zstreamdump

INSTALL_EXEC_HOOKS += cmd-zstream-install-exec-hook
