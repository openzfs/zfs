include $(srcdir)/%D%/include/Makefile.am

libspl_assert_la_CFLAGS = $(AM_CFLAGS) $(LIBRARY_CFLAGS) $(LIBUNWIND_CFLAGS)
libspl_la_CFLAGS        = $(libspl_assert_la_CFLAGS)
if TARGET_CPU_I386
libspl_la_CFLAGS        += $(NO_ATOMIC_ALIGNMENT)
endif

noinst_LTLIBRARIES += libspl_assert.la libspl.la
CPPCHECKTARGETS    += libspl_assert.la libspl.la

libspl_assert_la_SOURCES = \
	%D%/assert.c \
	%D%/backtrace.c

libspl_la_SOURCES = \
	%D%/libspl_impl.h \
	%D%/atomic.c \
	%D%/condvar.c \
	%D%/cred.c \
	%D%/getexecname.c \
	%D%/kmem.c \
	%D%/kstat.c \
	%D%/libspl.c \
	%D%/list.c \
	%D%/mkdirp.c \
	%D%/mutex.c \
	%D%/page.c \
	%D%/procfs_list.c \
	%D%/random.c \
	%D%/rwlock.c \
	%D%/sid.c \
	%D%/strlcat.c \
	%D%/strlcpy.c \
	%D%/taskq.c \
	%D%/thread.c \
	%D%/timestamp.c \
	%D%/tunables.c \
	%D%/include/sys/list.h \
	%D%/include/sys/list_impl.h

if BUILD_LINUX
libspl_la_SOURCES += \
	%D%/os/linux/getexecname.c \
	%D%/os/linux/gethostid.c \
	%D%/os/linux/getmntany.c \
	%D%/os/linux/zone.c
endif

if BUILD_FREEBSD
libspl_la_SOURCES += \
	%D%/os/freebsd/getexecname.c \
	%D%/os/freebsd/gethostid.c \
	%D%/os/freebsd/getmntany.c \
	%D%/os/freebsd/mnttab.c \
	%D%/os/freebsd/zone.c
endif

libspl_la_LIBADD = \
	libspl_assert.la

libspl_la_LIBADD += $(LIBATOMIC_LIBS) $(LIBCLOCK_GETTIME)

libspl_assert_la_LIBADD = $(BACKTRACE_LIBS) $(LIBUNWIND_LIBS)

if BUILD_FREEBSD
libspl_assert_la_LIBADD += -lpthread
endif
