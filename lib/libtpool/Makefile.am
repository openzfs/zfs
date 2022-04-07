libtpool_la_CFLAGS  = $(AM_CFLAGS) $(LIBRARY_CFLAGS)
libtpool_la_CFLAGS += -fvisibility=hidden
# https://gcc.gnu.org/bugzilla/show_bug.cgi?id=61118
libtpool_la_CFLAGS += $(NO_CLOBBERED)

noinst_LTLIBRARIES += libtpool.la
CPPCHECKTARGETS    += libtpool.la

libtpool_la_SOURCES = \
	%D%/thread_pool.c \
	%D%/thread_pool_impl.h
