include $(top_srcdir)/config/Rules.am

noinst_LTLIBRARIES = libefi.la

USER_C = \
	rdwr_efi.c

nodist_libefi_la_SOURCES = $(USER_C)

libefi_la_LIBADD = $(LIBUUID)

EXTRA_DIST = $(USER_C)
