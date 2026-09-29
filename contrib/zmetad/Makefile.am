zmetad_CFLAGS  = $(AM_CFLAGS)
zmetad_CFLAGS += $(LIBSQLITE3_CFLAGS)

sbin_PROGRAMS   += zmetad
CPPCHECKTARGETS += zmetad

zmetad_SOURCES = \
	%D%/zmetad.c \
	%D%/zmetad.h \
	%D%/zmetad_db.c

zmetad_LDADD = \
	libzfs.la \
	libzfs_core.la \
	libnvpair.la

zmetad_LDADD += $(LIBSQLITE3_LIBS)

check-schema:
	bash $(srcdir)/schema-check.sh $(top_srcdir)
.PHONY: check-schema
