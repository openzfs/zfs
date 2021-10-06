include $(top_srcdir)/config/Rules.am

# Unconditionally enable debugging for zdb
AM_CPPFLAGS += -DDEBUG -UNDEBUG -DZFS_DEBUG

sbin_PROGRAMS = zdb

zdb_SOURCES = \
	zdb.c \
	zdb_il.c \
	zdb.h

zdb_LDADD = \
	$(abs_top_builddir)/lib/libzpool/libzpool.la \
	$(abs_top_builddir)/lib/libzfs_core/libzfs_core.la \
	$(abs_top_builddir)/lib/libnvpair/libnvpair.la

if BUILD_RUST
zdb_LDADD += -lzoa
zdb_LDFLAGS = -L$(abs_top_builddir)/cmd/zfs_object_agent
zdb_CFLAGS = -DHAVE_LIBZOA
endif

include $(top_srcdir)/config/CppCheck.am
