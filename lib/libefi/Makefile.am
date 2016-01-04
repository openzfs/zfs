include $(top_srcdir)/config/Rules.am

AM_CFLAGS += $(DEBUG_STACKFLAGS) $(FRAME_LARGER_THAN)

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

noinst_LTLIBRARIES = libefi.la

USER_C = \
	rdwr_efi.c

KERNEL_C =

nodist_libefi_la_SOURCES = \
	$(USER_C) \
	$(KERNEL_C)

libefi_la_LIBADD = $(LIBUUID) $(ZLIB)

EXTRA_DIST = $(USER_C)
