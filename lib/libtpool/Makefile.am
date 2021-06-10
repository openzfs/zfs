include $(top_srcdir)/config/Rules.am

AM_CFLAGS += -fvisibility=hidden

noinst_LTLIBRARIES = libtpool.la

USER_C = \
	thread_pool.c \
	thread_pool_impl.h

libtpool_la_SOURCES = $(USER_C)

include $(top_srcdir)/config/CppCheck.am
