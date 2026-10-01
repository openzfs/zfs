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

# "+=", not "=": this fragment is concatenated into the top-level
# Makefile via contrib/Makefile.am; a bare assignment would clobber
# anything the include tree adds later.
noinst_PROGRAMS += zmetad_schema_test

zmetad_schema_test_SOURCES = \
	%D%/zmetad_schema_test.c \
	%D%/zmetad_schema.c \
	%D%/zmetad_schema.h \
	%D%/schema_blob.h

zmetad_schema_test_LDADD = libnvpair.la

# Run the parser test under "make check".
TESTS += zmetad_schema_test

# Ship the schema artifacts in "make dist".
EXTRA_DIST += \
	%D%/schema-check.sh \
	%D%/events-schema.json \
	%D%/SCHEMA.md

check-schema:
	bash $(srcdir)/schema-check.sh $(top_srcdir)
.PHONY: check-schema

# Enforce header/JSON/blob schema sync as part of "make check".
check-local: check-schema
