// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */
/*
 * Copyright 2008 Sun Microsystems, Inc. All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SPL_RWLOCK_H
#define	_SPL_RWLOCK_H

#ifdef DEBUG
#define	SPL_DEBUG_RWLOCK
#endif

#include <sys/types.h>
#include <kern/locks.h>

typedef enum {
	RW_DRIVER  = 2,
	RW_DEFAULT = 4
} krw_type_t;

typedef enum {
	RW_NONE   = 0,
	RW_WRITER = 1,
	RW_READER = 2
} krw_t;

#define	RW_NOLOCKDEP	0

struct krwlock {
	uint32_t	rw_lock[4];	/* opaque lck_rw_t data */
	kthread_t	*rw_owner;	/* writer (exclusive) lock only */
	int		rw_readers;	/* reader lock only */
	int		rw_pad;		/* */
#ifdef SPL_DEBUG_RWLOCK
	void		*leak;
#endif
};
typedef struct krwlock  krwlock_t;

#define	RW_WRITE_HELD(x)	(rw_write_held((x)))
#define	RW_LOCK_HELD(x)		(rw_lock_held((x)))
#define	RW_READ_HELD(x)		(rw_read_held((x)))

#ifdef SPL_DEBUG_RWLOCK
#define	rw_init(A, B, C, D) \
    rw_initx(A, B, C, D, __FILE__, __FUNCTION__, __LINE__)
extern  void  rw_initx(krwlock_t *, char *, krw_type_t, void *,
    const char *, const char *, int);
#else
extern  void  rw_init(krwlock_t *, char *, krw_type_t, void *);
#endif
extern  void  rw_destroy(krwlock_t *);
extern  void  rw_enter(krwlock_t *, krw_t);
extern  int   rw_tryenter(krwlock_t *, krw_t);
extern  void  rw_exit(krwlock_t *);
extern  void  rw_downgrade(krwlock_t *);
extern  int   rw_tryupgrade(krwlock_t *);
extern  int   rw_write_held(krwlock_t *);
extern  int   rw_read_held(krwlock_t *);
extern  int   rw_lock_held(krwlock_t *);
extern  int   rw_isinit(krwlock_t *);

int  spl_rwlock_init(void);
void spl_rwlock_fini(void);

#endif /* _SPL_RWLOCK_H */
