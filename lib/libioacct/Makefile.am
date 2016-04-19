include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

lib_LTLIBRARIES = libioacct.la

USER_C = \
	libioacct.c

KERNEL_C =

nodist_libioacct_la_SOURCES = \
	$(USER_C) \
	$(KERNEL_C)
