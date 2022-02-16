include $(top_srcdir)/config/Rules.am

AM_CFLAGS += -fvisibility=hidden
# https://gcc.gnu.org/bugzilla/show_bug.cgi?id=61118
AM_CFLAGS += $(NO_CLOBBERED)

noinst_LTLIBRARIES = libtpool.la

USER_C = \
	thread_pool.c \
	thread_pool_impl.h

libtpool_la_SOURCES = $(USER_C)

include $(top_srcdir)/config/CppCheck.am
