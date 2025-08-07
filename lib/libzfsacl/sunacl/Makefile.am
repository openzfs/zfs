libsunacl_la_CFLAGS  = $(AM_CFLAGS) $(LIBRARY_CFLAGS)

lib_LTLIBRARIES += libsunacl.la
CPPCHECKTARGETS += libsunacl.la

libsunacl_la_CPPFLAGS  = $(AM_CPPFLAGS)

dist_libsunacl_la_SOURCES = \
	%D%/libsunacl.c

libsunacl_la_LIBADD = \
	libzfsacl.la \
	libzfs.la

libsunacl_la_LDFLAGS =

if !ASAN_ENABLED
libsunacl_la_LDFLAGS += -Wl,-z,defs
endif

libsunacl_la_LDFLAGS += -version-info 1:0:0

