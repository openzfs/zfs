/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/refcount.h>
#include <sys/rrwlock.h>

/*
 * This file contains the implementation of a re-entrant read
 * reader/writer lock (aka "rrwlock").
 *
 * This is a normal reader/writer lock with the additional feature
 * of allowing threads who have already obtained a read lock to
 * re-enter another read lock (re-entrant read) - even if there are
 * waiting writers.
 *
 * Callers who have not obtained a read lock give waiting writers priority.
 *
 * The rrwlock_t lock does not allow re-entrant writers, nor does it
 * allow a re-entrant mix of reads and writes (that is, it does not
 * allow a caller who has already obtained a read lock to be able to
 * then grab a write lock without first dropping all read locks, and
 * vice versa).
 *
 * The rrwlock_t uses tsd (thread specific data) to keep a list of
 * nodes (rrw_node_t), where each node keeps track of which specific
 * lock (rrw_node_t::rn_rrl) the thread has grabbed.  Since re-entering
 * should be rare, a thread that grabs multiple reads on the same rrwlock_t
 * will store multiple rrw_node_ts of the same 'rrn_rrl'. Nodes on the
 * tsd list can represent a different rrwlock_t.  This allows a thread
 * to enter multiple and unique rrwlock_ts for read locks at the same time.
 *
 * Since using tsd exposes some overhead, the rrwlock_t only needs to
 * keep tsd data when writers are waiting.  If no writers are waiting, then
 * a reader just bumps the anonymous read count (rr_anon_rcount) - no tsd
 * is needed.  Once a writer attempts to grab the lock, readers then
 * keep tsd data and bump the linked readers count (rr_linked_rcount).
 *
 * If there are waiting writers and there are anonymous readers, then a
 * reader doesn't know if it is a re-entrant lock. But since it may be one,
 * we allow the read to proceed (otherwise it could deadlock).  Since once
 * waiting writers are active, readers no longer bump the anonymous count,
 * the anonymous readers will eventually flush themselves out.  At this point,
 * readers will be able to tell if they are a re-entrant lock (have a
 * rrw_node_t entry for the lock) or not. If they are a re-entrant lock, then
 * we must let the proceed.  If they are not, then the reader blocks for the
 * waiting writers.  Hence, we do not starve writers.
 */

/* global key for TSD */
uint_t rrw_tsd_key;

typedef struct rrw_node {
	struct rrw_node	*rn_next;
	rrwlock_t	*rn_rrl;
} rrw_node_t;

static rrw_node_t *
rrn_find(rrwlock_t *rrl)
{
	rrw_node_t *rn;

	if (refcount_count(&rrl->rr_linked_rcount) == 0)
		return (NULL);

	for (rn = tsd_get(rrw_tsd_key); rn != NULL; rn = rn->rn_next) {
		if (rn->rn_rrl == rrl)
			return (rn);
	}
	return (NULL);
}

/*
 * Add a node to the head of the singly linked list.
 */
static void
rrn_add(rrwlock_t *rrl)
{
	rrw_node_t *rn;

	rn = kmem_alloc(sizeof (*rn), KM_SLEEP);
	rn->rn_rrl = rrl;
	rn->rn_next = tsd_get(rrw_tsd_key);
	VERIFY(tsd_set(rrw_tsd_key, rn) == 0);
}

/*
 * If a node is found for 'rrl', then remove the node from this
 * thread's list and return TRUE; otherwise return FALSE.
 */
static boolean_t
rrn_find_and_remove(rrwlock_t *rrl)
{
	rrw_node_t *rn;
	rrw_node_t *prev = NULL;

	if (refcount_count(&rrl->rr_linked_rcount) == 0)
		return (B_FALSE);

	for (rn = tsd_get(rrw_tsd_key); rn != NULL; rn = rn->rn_next) {
		if (rn->rn_rrl == rrl) {
			if (prev)
				prev->rn_next = rn->rn_next;
			else
				VERIFY(tsd_set(rrw_tsd_key, rn->rn_next) == 0);
			kmem_free(rn, sizeof (*rn));
			return (B_TRUE);
		}
		prev = rn;
	}
	return (B_FALSE);
}

void
rrw_init(rrwlock_t *rrl)
{
	mutex_init(&rrl->rr_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&rrl->rr_cv, NULL, CV_DEFAULT, NULL);
	rrl->rr_writer = NULL;
	refcount_create(&rrl->rr_anon_rcount);
	refcount_create(&rrl->rr_linked_rcount);
	rrl->rr_writer_wanted = B_FALSE;
}

void
rrw_destroy(rrwlock_t *rrl)
{
	mutex_destroy(&rrl->rr_lock);
	cv_destroy(&rrl->rr_cv);
	ASSERT(rrl->rr_writer == NULL);
	refcount_destroy(&rrl->rr_anon_rcount);
	refcount_destroy(&rrl->rr_linked_rcount);
}

static void
rrw_enter_read(rrwlock_t *rrl, void *tag)
{
	mutex_enter(&rrl->rr_lock);
#if !defined(DEBUG) && defined(_KERNEL)
	if (!rrl->rr_writer && !rrl->rr_writer_wanted) {
		rrl->rr_anon_rcount.rc_count++;
		mutex_exit(&rrl->rr_lock);
		return;
	}
	DTRACE_PROBE(zfs__rrwfastpath__rdmiss);
#endif
	ASSERT(rrl->rr_writer != curthread);
	ASSERT(refcount_count(&rrl->rr_anon_rcount) >= 0);

	while (rrl->rr_writer || (rrl->rr_writer_wanted &&
	    refcount_is_zero(&rrl->rr_anon_rcount) &&
	    rrn_find(rrl) == NULL))
		cv_wait(&rrl->rr_cv, &rrl->rr_lock);

	if (rrl->rr_writer_wanted) {
		/* may or may not be a re-entrant enter */
		rrn_add(rrl);
		(void) refcount_add(&rrl->rr_linked_rcount, tag);
	} else {
		(void) refcount_add(&rrl->rr_anon_rcount, tag);
	}
	ASSERT(rrl->rr_writer == NULL);
	mutex_exit(&rrl->rr_lock);
}

static void
rrw_enter_write(rrwlock_t *rrl)
{
	mutex_enter(&rrl->rr_lock);
	ASSERT(rrl->rr_writer != curthread);

	while (refcount_count(&rrl->rr_anon_rcount) > 0 ||
	    refcount_count(&rrl->rr_linked_rcount) > 0 ||
	    rrl->rr_writer != NULL) {
		rrl->rr_writer_wanted = B_TRUE;
		cv_wait(&rrl->rr_cv, &rrl->rr_lock);
	}
	rrl->rr_writer_wanted = B_FALSE;
	rrl->rr_writer = curthread;
	mutex_exit(&rrl->rr_lock);
}

void
rrw_enter(rrwlock_t *rrl, krw_t rw, void *tag)
{
	if (rw == RW_READER)
		rrw_enter_read(rrl, tag);
	else
		rrw_enter_write(rrl);
}

void
rrw_exit(rrwlock_t *rrl, void *tag)
{
	mutex_enter(&rrl->rr_lock);
#if !defined(DEBUG) && defined(_KERNEL)
	if (!rrl->rr_writer && rrl->rr_linked_rcount.rc_count == 0) {
		rrl->rr_anon_rcount.rc_count--;
		if (rrl->rr_anon_rcount.rc_count == 0)
			cv_broadcast(&rrl->rr_cv);
		mutex_exit(&rrl->rr_lock);
		return;
	}
	DTRACE_PROBE(zfs__rrwfastpath__exitmiss);
#endif
	ASSERT(!refcount_is_zero(&rrl->rr_anon_rcount) ||
	    !refcount_is_zero(&rrl->rr_linked_rcount) ||
	    rrl->rr_writer != NULL);

	if (rrl->rr_writer == NULL) {
		int64_t count;
		if (rrn_find_and_remove(rrl))
			count = refcount_remove(&rrl->rr_linked_rcount, tag);
		else
			count = refcount_remove(&rrl->rr_anon_rcount, tag);
		if (count == 0)
			cv_broadcast(&rrl->rr_cv);
	} else {
		ASSERT(rrl->rr_writer == curthread);
		ASSERT(refcount_is_zero(&rrl->rr_anon_rcount) &&
		    refcount_is_zero(&rrl->rr_linked_rcount));
		rrl->rr_writer = NULL;
		cv_broadcast(&rrl->rr_cv);
	}
	mutex_exit(&rrl->rr_lock);
}

boolean_t
rrw_held(rrwlock_t *rrl, krw_t rw)
{
	boolean_t held;

	mutex_enter(&rrl->rr_lock);
	if (rw == RW_WRITER) {
		held = (rrl->rr_writer == curthread);
	} else {
		held = (!refcount_is_zero(&rrl->rr_anon_rcount) ||
		    !refcount_is_zero(&rrl->rr_linked_rcount));
	}
	mutex_exit(&rrl->rr_lock);

	return (held);
}
