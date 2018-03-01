include $(top_srcdir)/config/Rules.am

DEFAULT_INCLUDES += \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/lib/libspl/include

noinst_LTLIBRARIES = libtpool.la

USER_C = \
	thread_pool.c \
	thread_pool_impl.h

nodist_libtpool_la_SOURCES = $(USER_C)

libtpool_la_LIBADD = \
	$(top_builddir)/lib/libspl/libspl.la

libtpool_la_LDFLAGS = -pthread

EXTRA_DIST = $(USER_C)
