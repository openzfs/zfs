/*
 * Copyright (c) 2026 <TheAlmightyOgreLord>. All rights reserved.
 */

#ifndef _ZFS_DSL_ROOT_REPARENT_H
#define _ZFS_DSL_ROOT_REPARENT_H

#include <sys/types.h>
#include <sys/dsl_pool.h>
#include <sys/dmu_tx.h>
#include <sys/dmu.h>
#include <sys/fs/zfs.h>

#ifdef __cplusplus
extern "C" {
#endif

struct dsl_dataset;
typedef struct dsl_dataset dsl_dataset_t;   

struct root_reparent_arg {
	char	rra_child_name[ZFS_MAX_DATASET_NAME_LEN];
	char            rra_pool_name[ZFS_MAX_DATASET_NAME_LEN];
	dsl_dataset_t  *rra_new_root_ds;
	uint64_t	rra_new_root_obj;
	uint64_t	rra_new_root_child_zap;
	uint64_t	rra_new_root_ds_obj;
};

void dsl_root_reparent_sync_task(void *arg, dmu_tx_t *tx);
int dsl_root_reparent_check(dsl_pool_t *dp, const char *child_name);
int dsl_root_reparent(dsl_pool_t *dp, const char *pool_name,
    const char *child_name);

#ifdef __cplusplus
}
#endif

#endif /* _ZFS_DSL_ROOT_REPARENT_H */
