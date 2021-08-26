#ifndef _ZIL_PMEM_SPA_H_
#define _ZIL_PMEM_SPA_H_

#include <sys/zil_pmem_prb.h>

#ifndef _ZIL_PMEM_SPA_IMPL

typedef struct spa_prb_handle spa_prb_handle_t;

#else

typedef enum
{
	SPA_ZILPMEM_UNINIT,
	SPA_ZILPMEM_LOADCREATING,
	SPA_ZILPMEM_LOADCREATE_FAILED,
	SPA_ZILPMEM_LOADED,
	SPA_ZILPMEM_UNLOADING,
	SPA_ZILPMEM_UNLOADED,
} spa_zilpmem_state_t;

typedef struct spa_prb
{
	list_node_t sprb_list_node;
	zfs_refcount_t sprb_rc;
	zilpmem_prb_t *sprb_prb;
} spa_prb_t;

typedef struct spa_prb_handle
{
	avl_node_t sprbh_avl_node;
	uint64_t sprbh_objset_id;
	spa_prb_t *sprbh_sprb;
	zfs_refcount_t sprbh_rc;
	zilpmem_prb_handle_t *sprbh_hdl;
} spa_prb_handle_t;

typedef struct spa_zilpmem
{
	rrmlock_t szlp_rwl;

	spa_zilpmem_state_t szlp_state;
	list_t szlp_prbs; /* spa_prb_t */

	avl_tree_t szlp_handles;
} spa_zilpmem_t;

#endif /* _ZIL_PMEM_SPA_IMPL */

zilpmem_prb_handle_t *
zilpmem_spa_prb_handle_ref_inner(spa_prb_handle_t *sprbh);

int zilpmem_spa_create(spa_t *spa);
int zilpmem_spa_load(spa_t *spa);
void zilpmem_spa_unload(spa_t *spa);

void zilpmem_spa_create_objset(spa_t *spa, objset_t *os, dmu_tx_t *tx);
void zilpmem_spa_txg_synced(spa_t *spa, uint64_t synced_txg);
void zilpmem_spa_destroy_objset(objset_t *os, zil_header_pmem_t *zh_sync);

#endif /* _ZIL_PMEM_SPA_H_ */
