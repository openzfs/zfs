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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SYS_TXG_IMPL_H
#define	_SYS_TXG_IMPL_H

#include <sys/spa.h>
#include <sys/txg.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct tx_cpu {
	kmutex_t	tc_lock;
	kcondvar_t	tc_cv[TXG_SIZE];
	uint64_t	tc_count[TXG_SIZE];
	char		tc_pad[16];
};

typedef struct tx_state {
	tx_cpu_t	*tx_cpu;	/* protects right to enter txg	*/
	kmutex_t	tx_sync_lock;	/* protects tx_state_t */
	krwlock_t	tx_suspend;
	uint64_t	tx_open_txg;	/* currently open txg id */
	uint64_t	tx_quiesced_txg; /* quiesced txg waiting for sync */
	uint64_t	tx_syncing_txg;	/* currently syncing txg id */
	uint64_t	tx_synced_txg;	/* last synced txg id */

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
} tx_state_t;

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_TXG_IMPL_H */
