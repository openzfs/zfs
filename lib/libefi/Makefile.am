include $(top_srcdir)/config/Rules.am

AM_CFLAGS += $(LIBUUID_CFLAGS) $(ZLIB_CFLAGS)

noinst_LTLIBRARIES = libefi.la

if BUILD_LINUX
USER_C = \
	rdwr_efi.c
endif

if BUILD_MACOS
USER_C = \
	rdwr_efi_macos.c
libefi_la_LDFLAGS= -framework DiskArbitration -framework CoreServices
endif

libefi_la_SOURCES = $(USER_C)

libefi_la_LIBADD = $(LIBUUID_LIBS) $(ZLIB_LIBS)

include $(top_srcdir)/config/CppCheck.am
