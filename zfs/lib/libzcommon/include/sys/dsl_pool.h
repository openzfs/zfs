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

#ifndef	_SYS_DSL_POOL_H
#define	_SYS_DSL_POOL_H

#include <sys/spa.h>
#include <sys/txg.h>
#include <sys/txg_impl.h>
#include <sys/zfs_context.h>

#ifdef	__cplusplus
extern "C" {
#endif

struct objset;
struct dsl_dir;

typedef struct dsl_pool {
	/* Immutable */
	spa_t *dp_spa;
	struct objset *dp_meta_objset;
	struct dsl_dir *dp_root_dir;
	struct dsl_dir *dp_mos_dir;
	uint64_t dp_root_dir_obj;

	/* No lock needed - sync context only */
	blkptr_t dp_meta_rootbp;
	list_t dp_synced_datasets;
	uint64_t dp_write_limit;

	/* Uses dp_lock */
	kmutex_t dp_lock;
	uint64_t dp_space_towrite[TXG_SIZE];
	uint64_t dp_tempreserved[TXG_SIZE];

	/* Has its own locking */
	tx_state_t dp_tx;
	txg_list_t dp_dirty_datasets;
	txg_list_t dp_dirty_dirs;
	txg_list_t dp_sync_tasks;

	/*
	 * Protects administrative changes (properties, namespace)
	 * It is only held for write in syncing context.  Therefore
	 * syncing context does not need to ever have it for read, since
	 * nobody else could possibly have it for write.
	 */
	krwlock_t dp_config_rwlock;
} dsl_pool_t;

int dsl_pool_open(spa_t *spa, uint64_t txg, dsl_pool_t **dpp);
void dsl_pool_close(dsl_pool_t *dp);
dsl_pool_t *dsl_pool_create(spa_t *spa, uint64_t txg);
void dsl_pool_sync(dsl_pool_t *dp, uint64_t txg);
void dsl_pool_zil_clean(dsl_pool_t *dp);
int dsl_pool_sync_context(dsl_pool_t *dp);
uint64_t dsl_pool_adjustedsize(dsl_pool_t *dp, boolean_t netfree);
int dsl_pool_tempreserve_space(dsl_pool_t *dp, uint64_t space, dmu_tx_t *tx);
void dsl_pool_tempreserve_clear(dsl_pool_t *dp, int64_t space, dmu_tx_t *tx);
void dsl_pool_memory_pressure(dsl_pool_t *dp);
void dsl_pool_willuse_space(dsl_pool_t *dp, int64_t space, dmu_tx_t *tx);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_DSL_POOL_H */
