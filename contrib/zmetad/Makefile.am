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

# check_PROGRAMS, not noinst_PROGRAMS: the concatenated top-level
# Makefile also defines noinst_PROGRAMS (upstream tests/unit framework,
# under CONFIG_USER), and automake cannot merge a primary across
# differing conditions. check_PROGRAMS is unused elsewhere; it still
# builds the binary for "make check" and TESTS below still runs it.
check_PROGRAMS = zmetad_schema_test

zmetad_schema_test_SOURCES = \
	%D%/zmetad_schema_test.c \
	%D%/zmetad_schema.c \
	%D%/zmetad_schema.h \
	%D%/schema_blob.h

zmetad_schema_test_LDADD = libnvpair.la

# Run the parser test under "make check".
TESTS = zmetad_schema_test

# Ship the schema artifacts in "make dist".
EXTRA_DIST = \
	%D%/schema-check.sh \
	%D%/events-schema.json \
	%D%/SCHEMA.md

PHONY += check-schema
check-schema:
	bash $(srcdir)/%D%/schema-check.sh $(top_srcdir)

# Enforce header/JSON/blob schema sync as part of "make check".
check-local: check-schema
