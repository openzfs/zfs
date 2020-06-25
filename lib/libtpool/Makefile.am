include $(top_srcdir)/config/Rules.am

noinst_LTLIBRARIES = libtpool.la

USER_C = \
	thread_pool.c \
	thread_pool_impl.h

libtpool_la_SOURCES = $(USER_C)

libtpool_la_LIBADD = \
	$(top_builddir)/lib/libspl/libspl.la

libtpool_la_LDFLAGS = -pthread
