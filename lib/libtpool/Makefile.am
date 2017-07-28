include $(top_srcdir)/config/Rules.am

AM_CFLAGS += $(DEBUG_STACKFLAGS)

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

noinst_LTLIBRARIES = libtpool.la

USER_C = \
	thread_pool.c \
	thread_pool_impl.h

KERNEL_C =

nodist_libtpool_la_SOURCES = \
	$(USER_C) \
	$(KERNEL_C)

libtpool_la_LIBADD = \
	$(top_builddir)/lib/libspl/libspl.la

libtpool_la_LDFLAGS = -pthread

EXTRA_DIST = $(USER_C)
