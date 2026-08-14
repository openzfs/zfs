// SPDX-License-Identifier: BSD-2-Clause
/*
 * OpenZFS kmutex wrappers for NetBSD.  NetBSD already uses kmutex_t
 * with mutex_enter/mutex_exit.  The macro below is fixed at 4
 * arguments (Solaris mutex_init).  It is not dispatched on argument
 * count: a 3-arg call does not compile.  The inner mutex_init name
 * is not expanded again.
 */

#ifndef _OPENSOLARIS_SYS_MUTEX_H_
#define	_OPENSOLARIS_SYS_MUTEX_H_

#include_next <sys/mutex.h>

#ifdef _KERNEL
#include <sys/systm.h>

#define	MUTEX_HELD(x)		(mutex_owned(x))
#define	MUTEX_NOT_HELD(x)	(!mutex_owned(x) || panicstr != NULL)
#define	mutex_init(a, b, c, d)	\
	mutex_init(a, c, (c == MUTEX_SPIN) ? IPL_VM : IPL_NONE)

/*
 * OpenZFS lockdep annotation, not a NetBSD kmutex_type_t.
 */
#undef	MUTEX_NOLOCKDEP
#define	MUTEX_NOLOCKDEP		MUTEX_DEFAULT

#define	mutex_enter_nested(l, t)	mutex_enter(l)
#define	mutex_enter_interruptible(l)	(mutex_enter(l), 0)

#endif /* _KERNEL */

#endif /* _OPENSOLARIS_SYS_MUTEX_H_ */
