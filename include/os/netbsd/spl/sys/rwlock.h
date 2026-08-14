// SPDX-License-Identifier: BSD-2-Clause
/*
 * OpenZFS krwlock wrappers for NetBSD.  Native krwlock_t already
 * matches the Solaris enter/exit/tryenter API.
 */

#ifndef _OPENSOLARIS_SYS_RWLOCK_H_
#define	_OPENSOLARIS_SYS_RWLOCK_H_

#include_next <sys/rwlock.h>

#ifdef _KERNEL

typedef enum {
	RW_DRIVER = 2,
	RW_DEFAULT = 4
} krw_type_t;

#define	RW_NOLOCKDEP		0
/*
 * Native krw_t is RW_READER=0, RW_WRITER=1.  Linux/FreeBSD OpenZFS
 * uses RW_NONE=0, RW_WRITER=1, RW_READER=2.  zvol_find_by_name()
 * treats RW_NONE as "do not take zv_suspend_lock".  Defining
 * RW_NONE as 0 aliases native RW_READER, so find(RW_READER) skips
 * the lock and zvol_set_volsize then rw_exit()s an unheld lock.
 * Without LOCKDEBUG that underflows the reader count; zvol_suspend()
 * (recv, rollback) waits forever.  RW_NONE is only that sentinel,
 * never passed to rw_enter().
 */
#define	RW_NONE			((krw_t)2)
_Static_assert(RW_NONE != RW_READER, "RW_NONE aliases RW_READER");
_Static_assert(RW_NONE != RW_WRITER, "RW_NONE aliases RW_WRITER");
#define	RW_READ_HELD(x)		(rw_read_held((x)))
#define	RW_WRITE_HELD(x)	(rw_write_held((x)))
#define	RW_LOCK_HELD(x)		(rw_lock_held((x)))
#define	RW_ISWRITER(x)		(rw_write_held((x)))
/*
 * NetBSD rw_read_held() is "held for read by anyone", not by the
 * caller.  Fine for ASSERT(RW_READ_HELD) style checks; do not use
 * it to decide whether *this* thread may recurse or upgrade.
 */

static inline void
opensolaris_rw_init(krwlock_t *l, const char *name, int type, void *arg)
{
	(void) name;
	(void) type;
	(void) arg;
	rw_init(l);
}
#define	rw_init(a, ...)		opensolaris_rw_init((a), NULL, 0, NULL)

#ifndef rw_iswriter
#define	rw_iswriter(x)		rw_write_held(x)
#endif

#endif /* _KERNEL */

#endif /* _OPENSOLARIS_SYS_RWLOCK_H_ */
