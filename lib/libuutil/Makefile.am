libuutil_la_CFLAGS = $(AM_CFLAGS) $(LIBRARY_CFLAGS) $(LIBRARY_CFLAGS)

lib_LTLIBRARIES += libuutil.la
CPPCHECKTARGETS += libuutil.la

libuutil_la_SOURCES = \
	%D%/uu_alloc.c \
	%D%/uu_avl.c \
	%D%/uu_ident.c \
	%D%/uu_list.c \
	%D%/uu_misc.c \
	%D%/uu_string.c

libuutil_la_LIBADD = \
	libavl.la \
	libspl.la

libuutil_la_LIBADD += $(LTLIBINTL)

libuutil_la_LDFLAGS = -pthread

if !ASAN_ENABLED
libuutil_la_LDFLAGS += -Wl,-z,defs
endif

libuutil_la_LDFLAGS += -version-info 3:0:0

dist_noinst_DATA += %D%/libuutil.abi %D%/libuutil.suppr
