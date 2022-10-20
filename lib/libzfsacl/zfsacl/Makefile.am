libzfsacl_la_CFLAGS  = $(AM_CFLAGS) $(LIBRARY_CFLAGS) -fPIC

lib_LTLIBRARIES += libzfsacl.la
CPPCHECKTARGETS += libzfsacl.la

libzfsacl_la_CPPFLAGS  = $(AM_CPPFLAGS)

if BUILD_LINUX
dist_libzfsacl_la_SOURCES = \
	%D%/libzfsacl_impl_common.c \
	%D%/libzfsacl_impl_linux.c
endif

if BUILD_FREEBSD
dist_libzfsacl_la_SOURCES = \
	%D%/libzfsacl_impl_common.c \
	%D%/libzfsacl_impl_freebsd.c
endif

libzfsacl_la_LIBADD = \
	libzfs.la \
	libspl.la

libzfsacl_la_LIBADD += $(LTLIBINTL)

libzfsacl_la_LDFLAGS =

if !ASAN_ENABLED
libzfsacl_la_LDFLAGS += -Wl,-z,defs
endif

libzfsacl_la_LDFLAGS += -version-info 1:0:0
