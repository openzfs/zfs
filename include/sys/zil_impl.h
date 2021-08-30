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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 */

/* Portions Copyright 2010 Robert Milkowski */

#ifndef	_SYS_ZIL_IMPL_H
#define	_SYS_ZIL_IMPL_H

#include <sys/zil.h>
#include <sys/dmu_objset.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Intent log transaction lists
 */
typedef struct itxs {
	list_t		i_sync_list;	/* list of synchronous itxs */
	avl_tree_t	i_async_tree;	/* tree of foids for async itxs */
} itxs_t;

typedef struct itxg {
	kmutex_t	itxg_lock;	/* lock for this structure */
	uint64_t	itxg_txg;	/* txg for this chain */
	itxs_t		*itxg_itxs;	/* sync and async itxs */
} itxg_t;

/* for async nodes we build up an AVL tree of lists of async itxs per file */
typedef struct itx_async_node {
	uint64_t	ia_foid;	/* file object id */
	list_t		ia_list;	/* list of async itxs for this foid */
	avl_node_t	ia_node;	/* AVL tree linkage */
} itx_async_node_t;



/* zil.c <=> zil_lwb.c */

extern int zil_maxblocksize;
boolean_t zilog_is_dirty(zilog_t *zilog);
void zil_get_commit_list(zilog_t *zilog);

typedef int zil_replay_func_t(void *arg1, void *arg2, boolean_t byteswap);

extern void	zillwb_init(void);
extern void	zillwb_fini(void);
extern void	zillwb_close(zilog_t *zilog);
extern void	zillwb_replay(objset_t *os, void *arg,
    zil_replay_func_t *replay_func[TX_MAX_TYPE]);
extern boolean_t zillwb_replaying(zilog_t *zilog, dmu_tx_t *tx);
extern void	zillwb_destroy(zilog_t *zilog, boolean_t keep_first);
extern void	zillwb_destroy_sync(zilog_t *zilog, dmu_tx_t *tx);
extern void	zillwb_commit(zilog_t *zilog, uint64_t oid);
extern int	zillwb_reset(const char *osname, void *txarg);
extern int	zillwb_claim(struct dsl_pool *dp,
    struct dsl_dataset *ds, void *txarg);
extern int 	zillwb_check_log_chain(struct dsl_pool *dp,
    struct dsl_dataset *ds, void *tx);
extern void	zillwb_sync(zilog_t *zilog, dmu_tx_t *tx);
extern int	zillwb_suspend(const char *osname, void **cookiep);
extern void	zillwb_resume(void *cookie);
extern void	zillwb_lwb_add_block(struct lwb *lwb, const blkptr_t *bp);
extern void	zillwb_lwb_add_txg(struct lwb *lwb, uint64_t txg);
extern int	zillwb_bp_tree_add(zilog_t *zilog, const blkptr_t *bp);
extern uint64_t	zillwb_max_copied_data(zilog_t *zilog);
extern uint64_t	zillwb_max_log_data(zilog_t *zilog);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ZIL_IMPL_H */
