include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

noinst_LTLIBRARIES = libefi.la

USER_C = \
	rdwr_efi.c

nodist_libefi_la_SOURCES = $(USER_C)

libefi_la_LIBADD = $(LIBUUID)

EXTRA_DIST = $(USER_C)
