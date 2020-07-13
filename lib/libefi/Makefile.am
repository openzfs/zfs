include $(top_srcdir)/config/Rules.am

AM_CFLAGS += $(LIBUUID_CFLAGS) $(ZLIB_CFLAGS)

noinst_LTLIBRARIES = libefi.la

USER_C = \
	rdwr_efi.c

libefi_la_SOURCES = $(USER_C)

libefi_la_LIBADD = $(LIBUUID_LIBS) $(ZLIB_LIBS)
