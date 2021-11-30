#ifndef _ZIL_PMEM_H_
#define _ZIL_PMEM_H_

#include <sys/zfs_context.h>
#include <sys/zil_impl.h>

#define ZILPMEM_PRB_CHUNKSIZE	((size_t)(1<<27))

typedef struct spa_prb_handle spa_prb_handle_t;
typedef struct zilog_pmem zilog_pmem_t;

spa_prb_handle_t *
zilpmem_spa_prb_hold(zilog_pmem_t *zilog);
void
zilpmem_spa_prb_rele(zilog_pmem_t *zilog, spa_prb_handle_t *sprbh);

#endif /* _ZIL_PMEM_H_ */
