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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright (c) 2013, 2017 by Delphix. All rights reserved.
 */

#ifndef _SYS_TXG_IMPL_H
#define	_SYS_TXG_IMPL_H

#include <sys/spa.h>
#include <sys/txg.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * The tx_cpu structure is a per-cpu structure that is used to track
 * the number of active transaction holds (tc_count). As transactions
 * are assigned into a transaction group the appropriate tc_count is
 * incremented to indicate that there are pending changes that have yet
 * to quiesce. Consumers eventually call txg_rele_to_sync() to decrement
 * the tc_count. A transaction group is not considered quiesced until all
 * tx_cpu structures have reached a tc_count of zero.
 *
 * This structure is a per-cpu structure by design. Updates to this structure
 * are frequent and concurrent. Having a single structure would result in
 * heavy lock contention so a per-cpu design was implemented. With the fanned
 * out mutex design, consumers only need to lock the mutex associated with
 * thread's cpu.
 *
 * The tx_cpu contains two locks, the tc_lock and tc_open_lock.
 * The tc_lock is used to protect all members of the tx_cpu structure with
 * the exception of the tc_open_lock. This lock should only be held for a
 * short period of time, typically when updating the value of tc_count.
 *
 * The tc_open_lock protects the tx_open_txg member of the tx_state structure.
 * This lock is used to ensure that transactions are only assigned into
 * the current open transaction group. In order to move the current open
 * transaction group to the quiesce phase, the txg_quiesce thread must
 * grab all tc_open_locks, increment the tx_open_txg, and drop the locks.
 * The tc_open_lock is held until the transaction is assigned into the
 * transaction group. Typically, this is a short operation but if throttling
 * is occurring it may be held for longer periods of time.
 */
struct tx_cpu {
	kmutex_t	tc_open_lock;	/* protects tx_open_txg */
	kmutex_t	tc_lock;	/* protects the rest of this struct */
	kcondvar_t	tc_cv[TXG_SIZE];
	uint64_t	tc_count[TXG_SIZE];	/* tx hold count on each txg */
	list_t		tc_callbacks[TXG_SIZE]; /* commit cb list */
} ____cacheline_aligned;

/*
 * The tx_state structure maintains the state information about the different
 * stages of the pool's transaction groups. A per pool tx_state structure
 * is used to track this information. The tx_state structure also points to
 * an array of tx_cpu structures (described above). Although the tx_sync_lock
 * is used to protect the members of this structure, it is not used to
 * protect the tx_open_txg. Instead a special lock in the tx_cpu structure
 * is used. Readers of tx_open_txg must grab the per-cpu tc_open_lock.
 * Any thread wishing to update tx_open_txg must grab the tc_open_lock on
 * every cpu (see txg_quiesce()).
 */
typedef struct tx_state {
	tx_cpu_t	*tx_cpu;	/* protects access to tx_open_txg */
	kmutex_t	tx_sync_lock;	/* protects the rest of this struct */

	uint64_t	tx_open_txg;	/* currently open txg id */
	uint64_t	tx_quiescing_txg; /* currently quiescing txg id */
	uint64_t	tx_quiesced_txg; /* quiesced txg waiting for sync */
	uint64_t	tx_syncing_txg;	/* currently syncing txg id */
	uint64_t	tx_synced_txg;	/* last synced txg id */

	hrtime_t	tx_open_time;	/* start time of tx_open_txg */

	uint64_t	tx_sync_txg_waiting; /* txg we're waiting to sync */
	uint64_t	tx_quiesce_txg_waiting; /* txg we're waiting to open */

	kcondvar_t	tx_sync_more_cv;
	kcondvar_t	tx_sync_done_cv;
	kcondvar_t	tx_quiesce_more_cv;
	kcondvar_t	tx_quiesce_done_cv;
	kcondvar_t	tx_timeout_cv;
	kcondvar_t	tx_exit_cv;	/* wait for all threads to exit */

	uint8_t		tx_threads;	/* number of threads */
	uint8_t		tx_exiting;	/* set when we're exiting */

	kthread_t	*tx_sync_thread;
	kthread_t	*tx_quiesce_thread;

	taskq_t		*tx_commit_cb_taskq; /* commit callback taskq */
} tx_state_t;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_TXG_IMPL_H */
