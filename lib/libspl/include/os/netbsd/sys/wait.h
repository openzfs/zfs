// SPDX-License-Identifier: CDDL-1.0
#ifndef _LIBSPL_SYS_WAIT_H
#define	_LIBSPL_SYS_WAIT_H

#include_next <sys/wait.h>

/*
 * POSIX WIF* take the wait status by value.  NetBSD userspace _W_INT
 * converts union wait by taking the address of its argument, so the
 * macros cannot be applied to a function return value.
 */
#undef	_W_INT
#define	_W_INT(i)	((int)(i))

#endif /* _LIBSPL_SYS_WAIT_H */
