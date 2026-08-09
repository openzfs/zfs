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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2018, 2019 by Delphix. All rights reserved.
 */

#ifndef	_SYS_DSL_DEADLIST_H
#define	_SYS_DSL_DEADLIST_H

#include <sys/bpobj.h>
#include <sys/zfs_context.h>
#include <sys/zthr.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct dmu_buf;
struct dsl_pool;
struct dsl_dataset;

typedef struct dsl_deadlist_phys {
	uint64_t dl_used;
	uint64_t dl_comp;
	uint64_t dl_uncomp;
	uint64_t dl_pad[37]; /* pad out to 320b for future expansion */
} dsl_deadlist_phys_t;

typedef struct dsl_deadlist {
	objset_t *dl_os;
	uint64_t dl_object;
	avl_tree_t dl_tree; /* contains dsl_deadlist_entry_t */
	avl_tree_t dl_cache; /* contains dsl_deadlist_cache_entry_t */
	boolean_t dl_havetree;
	boolean_t dl_havecache;
	struct dmu_buf *dl_dbuf;
	dsl_deadlist_phys_t *dl_phys;
	kmutex_t dl_lock;

	/* if it's the old on-disk format: */
	bpobj_t dl_bpobj;
	boolean_t dl_oldfmt;
} dsl_deadlist_t;

typedef struct dsl_deadlist_cache_entry {
	avl_node_t dlce_node;
	uint64_t dlce_mintxg;
	uint64_t dlce_bpobj;
	uint64_t dlce_bytes;
	uint64_t dlce_comp;
	uint64_t dlce_uncomp;
} dsl_deadlist_cache_entry_t;

typedef struct dsl_deadlist_entry {
	avl_node_t dle_node;
	uint64_t dle_mintxg;
	bpobj_t dle_bpobj;
} dsl_deadlist_entry_t;

typedef struct livelist_condense_entry {
	struct dsl_dataset *ds;
	dsl_deadlist_entry_t *first;
	dsl_deadlist_entry_t *next;
	boolean_t syncing;
	boolean_t cancelled;
} livelist_condense_entry_t;

extern uint64_t zfs_livelist_max_entries;
extern int zfs_livelist_min_percent_shared;

typedef int deadlist_iter_t(void *args, dsl_deadlist_entry_t *dle);

int dsl_deadlist_open(dsl_deadlist_t *dl, objset_t *os, uint64_t object);
void dsl_deadlist_close(dsl_deadlist_t *dl);
void dsl_deadlist_iterate(dsl_deadlist_t *dl, deadlist_iter_t func, void *arg);
uint64_t dsl_deadlist_alloc(objset_t *os, dmu_tx_t *tx);
void dsl_deadlist_free(objset_t *os, uint64_t dlobj, dmu_tx_t *tx);
void dsl_deadlist_insert(dsl_deadlist_t *dl, const blkptr_t *bp,
    boolean_t free, dmu_tx_t *tx);
int dsl_deadlist_insert_alloc_cb(void *arg, const blkptr_t *bp, dmu_tx_t *tx);
int dsl_deadlist_insert_free_cb(void *arg, const blkptr_t *bp, dmu_tx_t *tx);
void dsl_deadlist_add_key(dsl_deadlist_t *dl, uint64_t mintxg, dmu_tx_t *tx);
void dsl_deadlist_remove_key(dsl_deadlist_t *dl, uint64_t mintxg, dmu_tx_t *tx);
void dsl_deadlist_remove_entry(dsl_deadlist_t *dl, uint64_t mintxg,
dmu_tx_t *tx);
dsl_deadlist_entry_t *dsl_deadlist_first(dsl_deadlist_t *dl);
dsl_deadlist_entry_t *dsl_deadlist_last(dsl_deadlist_t *dl);
uint64_t dsl_deadlist_clone(dsl_deadlist_t *dl, uint64_t maxtxg,
    uint64_t mrs_obj, dmu_tx_t *tx);
void dsl_deadlist_space(dsl_deadlist_t *dl,
    uint64_t *usedp, uint64_t *compp, uint64_t *uncompp);
void dsl_deadlist_space_range(dsl_deadlist_t *dl,
    uint64_t mintxg, uint64_t maxtxg,
    uint64_t *usedp, uint64_t *compp, uint64_t *uncompp);
void dsl_deadlist_merge(dsl_deadlist_t *dl, uint64_t obj, dmu_tx_t *tx);
void dsl_deadlist_move_bpobj(dsl_deadlist_t *dl, bpobj_t *bpo, uint64_t mintxg,
    dmu_tx_t *tx);
boolean_t dsl_deadlist_is_open(dsl_deadlist_t *dl);
int dsl_process_sub_livelist(bpobj_t *bpobj, struct bplist *to_free,
    zthr_t *t, uint64_t *size);
void dsl_deadlist_clear_entry(dsl_deadlist_entry_t *dle, dsl_deadlist_t *dl,
    dmu_tx_t *tx);
void dsl_deadlist_discard_tree(dsl_deadlist_t *dl);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_DSL_DEADLIST_H */
