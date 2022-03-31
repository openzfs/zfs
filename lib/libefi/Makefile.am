include $(top_srcdir)/config/Rules.am

AM_CFLAGS += $(LIBUUID_CFLAGS) $(ZLIB_CFLAGS)

# See https://debbugs.gnu.org/cgi/bugreport.cgi?bug=54020
AM_CFLAGS += -no-suppress

noinst_LTLIBRARIES = libefi.la

USER_C = \
	rdwr_efi.c

libefi_la_SOURCES = $(USER_C)

libefi_la_LIBADD = $(LIBUUID_LIBS) $(ZLIB_LIBS)

include $(top_srcdir)/config/CppCheck.am
