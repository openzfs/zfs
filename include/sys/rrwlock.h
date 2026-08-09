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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright (c) 2012 by Delphix. All rights reserved.
 */

#ifndef	_SYS_RR_RW_LOCK_H
#define	_SYS_RR_RW_LOCK_H



#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/inttypes.h>
#include <sys/zfs_context.h>
#include <sys/zfs_refcount.h>

extern uint_t rrw_tsd_key;

/*
 * A reader-writer lock implementation that allows re-entrant reads, but
 * still gives writers priority on "new" reads.
 *
 * See rrwlock.c for more details about the implementation.
 *
 * Fields of the rrwlock_t structure:
 * - rr_lock: protects modification and reading of rrwlock_t fields
 * - rr_cv: cv for waking up readers or waiting writers
 * - rr_writer: thread id of the current writer
 * - rr_anon_rount: number of active anonymous readers
 * - rr_linked_rcount: total number of non-anonymous active readers
 * - rr_writer_wanted: a writer wants the lock
 */
typedef struct rrwlock {
	kmutex_t	rr_lock;
	kcondvar_t	rr_cv;
	kthread_t	*rr_writer;
	zfs_refcount_t	rr_anon_rcount;
	zfs_refcount_t	rr_linked_rcount;
	boolean_t	rr_writer_wanted;
	boolean_t	rr_track_all;
} rrwlock_t;

/*
 * 'tag' is used in reference counting tracking.  The
 * 'tag' must be the same in a rrw_enter() as in its
 * corresponding rrw_exit().
 */
void rrw_init(rrwlock_t *rrl, boolean_t track_all);
void rrw_destroy(rrwlock_t *rrl);
void rrw_enter(rrwlock_t *rrl, krw_t rw, const void *tag);
void rrw_enter_read(rrwlock_t *rrl, const void *tag);
void rrw_enter_read_prio(rrwlock_t *rrl, const void *tag);
void rrw_enter_write(rrwlock_t *rrl);
void rrw_exit(rrwlock_t *rrl, const void *tag);
boolean_t rrw_held(rrwlock_t *rrl, krw_t rw);
void rrw_tsd_destroy(void *arg);

#define	RRW_READ_HELD(x)	rrw_held(x, RW_READER)
#define	RRW_WRITE_HELD(x)	rrw_held(x, RW_WRITER)
#define	RRW_LOCK_HELD(x) \
	(rrw_held(x, RW_WRITER) || rrw_held(x, RW_READER))

/*
 * A reader-mostly lock implementation, tuning above reader-writer locks
 * for hightly parallel read acquisitions, pessimizing write acquisitions.
 *
 * This should be a prime number.  See comment in rrwlock.c near
 * RRM_TD_LOCK() for details.
 */
#define	RRM_NUM_LOCKS		17
typedef struct rrmlock {
	rrwlock_t	locks[RRM_NUM_LOCKS];
} rrmlock_t;

void rrm_init(rrmlock_t *rrl, boolean_t track_all);
void rrm_destroy(rrmlock_t *rrl);
void rrm_enter(rrmlock_t *rrl, krw_t rw, const void *tag);
void rrm_enter_read(rrmlock_t *rrl, const void *tag);
void rrm_enter_write(rrmlock_t *rrl);
void rrm_exit(rrmlock_t *rrl, const void *tag);
boolean_t rrm_held(rrmlock_t *rrl, krw_t rw);

#define	RRM_READ_HELD(x)	rrm_held(x, RW_READER)
#define	RRM_WRITE_HELD(x)	rrm_held(x, RW_WRITER)
#define	RRM_LOCK_HELD(x) \
	(rrm_held(x, RW_WRITER) || rrm_held(x, RW_READER))

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_RR_RW_LOCK_H */
