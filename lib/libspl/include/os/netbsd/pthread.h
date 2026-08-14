// SPDX-License-Identifier: CDDL-1.0
#ifndef	_LIBSPL_PTHREAD_H
#define	_LIBSPL_PTHREAD_H

/*
 * Native <pthread.h> includes <time.h>, which includes <sys/time.h>
 * then __RENAME(__clock_gettime50).  libspl <sys/time.h> calls
 * clock_gettime() from gethrtime().  If that happens before the
 * rename, clang rejects it (GCC on amd64/mips does not).
 *
 * Pull libspl <sys/time.h> first so include_next reaches the native
 * sys/time.h, then <time.h> applies the rename, then gethrtime() is
 * parsed.  Do not include <time.h> here — that is the order that
 * breaks the rename.
 */
#include <sys/time.h>
#include_next <pthread.h>

/*
 * NetBSD's pthread_setname_np(thread, fmt, arg) is printf-style.
 * OpenZFS call sites use the Linux/FreeBSD 2-arg form.
 */
static inline int
zfs_pthread_setname_np(pthread_t t, const char *name)
{
	return (pthread_setname_np(t, "%s", (void *)(uintptr_t)name));
}
#undef pthread_setname_np
#define	pthread_setname_np(t, n)	zfs_pthread_setname_np((t), (n))

#ifndef pthread_setconcurrency
static inline int
pthread_setconcurrency(int n)
{
	(void) n;
	return (0);
}
#endif

#endif
