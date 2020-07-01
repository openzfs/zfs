include $(top_srcdir)/config/Rules.am

noinst_LTLIBRARIES = libefi.la

USER_C = \
	rdwr_efi.c

libefi_la_SOURCES = $(USER_C)

libefi_la_LIBADD = $(LIBUUID)
