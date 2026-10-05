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
 * Copyright (C) 2017 Jorgen Lundman <lundman@lundman.net>
 */

#ifndef _SPL_RWLOCK_H
#define	_SPL_RWLOCK_H

#include <sys/types.h>

typedef enum {
	RW_DRIVER  = 2,
	RW_DEFAULT = 4
} krw_type_t;

typedef enum {
	RW_NONE   = 0,
	RW_WRITER = 1,
	RW_READER = 2
} krw_t;

struct krwlock {
	ERESOURCE	rw_lock;	/* opaque data */
	void		*rw_owner;	/* writer (exclusive) lock only */
	int		rw_readers;	/* reader lock only */
	int		rw_pad;
};
typedef struct krwlock  krwlock_t;

#define	RW_NOLOCKDEP	0

#define	RW_READ_HELD(x)		(rw_read_held((x)))
#define	RW_WRITE_HELD(x)	(rw_write_held((x)))
#define	RW_LOCK_HELD(x)		(rw_lock_held((x)))
#define	RW_ISWRITER(x)		(rw_iswriter(x))

extern  void  rw_init(krwlock_t *, char *, krw_type_t, void *);
extern  void  rw_destroy(krwlock_t *);
extern  void  rw_enter(krwlock_t *, krw_t);
extern  int   rw_tryenter(krwlock_t *, krw_t);
extern  void  rw_exit(krwlock_t *);
extern  void  rw_downgrade(krwlock_t *);
extern  int   rw_tryupgrade(krwlock_t *);
extern  int   rw_write_held(krwlock_t *);
extern  int   rw_lock_held(krwlock_t *);
extern  int   rw_isinit(krwlock_t *);
extern  int   rw_read_held(krwlock_t *);

int  spl_rwlock_init(void);
void spl_rwlock_fini(void);

#endif /* _SPL_RWLOCK_H */
