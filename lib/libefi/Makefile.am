libefi_la_CFLAGS  = $(AM_CFLAGS) $(LIBRARY_CFLAGS)
libefi_la_CFLAGS += $(LIBUUID_CFLAGS) $(ZLIB_CFLAGS)
libefi_la_CFLAGS += -fvisibility=hidden

noinst_LTLIBRARIES += libefi.la
CPPCHECKTARGETS    += libefi.la

libefi_la_SOURCES = \
	%D%/rdwr_efi.c

libefi_la_LIBADD = $(LIBUUID_LIBS) $(ZLIB_LIBS)
