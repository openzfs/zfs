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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_DSL_SYNCTASK_H
#define	_SYS_DSL_SYNCTASK_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/txg.h>
#include <sys/zfs_context.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct dsl_pool;

typedef int (dsl_checkfunc_t)(void *, void *, dmu_tx_t *);
typedef void (dsl_syncfunc_t)(void *, void *, cred_t *, dmu_tx_t *);

typedef struct dsl_sync_task {
	list_node_t dst_node;
	dsl_checkfunc_t *dst_checkfunc;
	dsl_syncfunc_t *dst_syncfunc;
	void *dst_arg1;
	void *dst_arg2;
	int dst_err;
} dsl_sync_task_t;

typedef struct dsl_sync_task_group {
	txg_node_t dstg_node;
	list_t dstg_tasks;
	struct dsl_pool *dstg_pool;
	cred_t *dstg_cr;
	uint64_t dstg_txg;
	int dstg_err;
	int dstg_space;
	boolean_t dstg_nowaiter;
} dsl_sync_task_group_t;

dsl_sync_task_group_t *dsl_sync_task_group_create(struct dsl_pool *dp);
void dsl_sync_task_create(dsl_sync_task_group_t *dstg,
    dsl_checkfunc_t *, dsl_syncfunc_t *,
    void *arg1, void *arg2, int blocks_modified);
int dsl_sync_task_group_wait(dsl_sync_task_group_t *dstg);
void dsl_sync_task_group_nowait(dsl_sync_task_group_t *dstg, dmu_tx_t *tx);
void dsl_sync_task_group_destroy(dsl_sync_task_group_t *dstg);
void dsl_sync_task_group_sync(dsl_sync_task_group_t *dstg, dmu_tx_t *tx);

int dsl_sync_task_do(struct dsl_pool *dp,
    dsl_checkfunc_t *checkfunc, dsl_syncfunc_t *syncfunc,
    void *arg1, void *arg2, int blocks_modified);
void dsl_sync_task_do_nowait(struct dsl_pool *dp,
    dsl_checkfunc_t *checkfunc, dsl_syncfunc_t *syncfunc,
    void *arg1, void *arg2, int blocks_modified, dmu_tx_t *tx);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_DSL_SYNCTASK_H */
