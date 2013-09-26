include $(top_srcdir)/config/Rules.am

AM_CFLAGS += $(DEBUG_STACKFLAGS) $(FRAME_LARGER_THAN)

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

noinst_LTLIBRARIES = libefi.la

libefi_la_SOURCES = \
	$(top_srcdir)/lib/libefi/rdwr_efi.c

libefi_la_LIBADD = $(LIBUUID) $(ZLIB)
