dnl #
dnl # Check if the glibc NPTL threading implementation includes the guard area
dnl # within the stack size allocation, rather than allocating extra space at
dnl # the end of the stack, as POSIX.1 requires.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_STACK_GUARD], [

	AC_MSG_CHECKING([whether pthread stack includes guard])

	saved_CFLAGS="$CFLAGS"
	CFLAGS="-fstack-check"
	saved_LDFLAGS="$LDFLAGS"
	LDFLAGS="-lpthread"

	AC_RUN_IFELSE([AC_LANG_PROGRAM(
	[
		#include <pthread.h>
		#include <sys/resource.h>
		#include <unistd.h>
		#include <bits/local_lim.h>

		#define PAGESIZE (sysconf(_SC_PAGESIZE))
		#define STACK_SIZE 8192
		#define BUFSIZE 4096

		void * func(void *arg)
		{
			char buf[[BUFSIZE]];
		}
	],
	[
		pthread_t tid;
		pthread_attr_t attr;
		struct rlimit l;

		l.rlim_cur = 0;
		l.rlim_max = 0;
		setrlimit(RLIMIT_CORE, &l);
		pthread_attr_init(&attr);
		pthread_attr_setstacksize(&attr, PTHREAD_STACK_MIN + STACK_SIZE);
		pthread_attr_setguardsize(&attr, PAGESIZE);
		pthread_create(&tid, &attr, func, NULL);
		pthread_join(tid, NULL);
	])],
	[
		AC_MSG_RESULT([no])
	],
	[
		AC_DEFINE([NPTL_GUARD_WITHIN_STACK], 1,
			[Define to 1 if NPTL threading implementation includes
			guard area in stack allocation])
		AC_MSG_RESULT([yes])
	])
	CFLAGS="$saved_CFLAGS"
	LDFLAGS="$saved_LDFLAGS"
])
