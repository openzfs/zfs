include $(top_srcdir)/config/Rules.am

# https://gcc.gnu.org/bugzilla/show_bug.cgi?id=61118
AM_CFLAGS += $(NO_CLOBBERED)

# See https://debbugs.gnu.org/cgi/bugreport.cgi?bug=54020
AM_CFLAGS += -no-suppress

noinst_LTLIBRARIES = libtpool.la

USER_C = \
	thread_pool.c \
	thread_pool_impl.h

libtpool_la_SOURCES = $(USER_C)

include $(top_srcdir)/config/CppCheck.am
