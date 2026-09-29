zmetad_CFLAGS  = $(AM_CFLAGS)
zmetad_CFLAGS += $(LIBSQLITE3_CFLAGS)

sbin_PROGRAMS   += zmetad
CPPCHECKTARGETS += zmetad

zmetad_SOURCES = \
	%D%/zmetad.c \
	%D%/zmetad.h \
	%D%/zmetad_db.c \
	%D%/zmetad_schema.c \
	%D%/zmetad_schema.h \
	%D%/schema_blob.h

zmetad_LDADD = \
	libzfs.la \
	libzfs_core.la \
	libnvpair.la

zmetad_LDADD += $(LIBSQLITE3_LIBS)

noinst_PROGRAMS = zmetad_schema_test

zmetad_schema_test_SOURCES = \
	%D%/zmetad_schema_test.c \
	%D%/zmetad_schema.c

zmetad_schema_test_LDADD = libnvpair.la

check-schema:
	bash $(srcdir)/schema-check.sh $(top_srcdir)
.PHONY: check-schema
