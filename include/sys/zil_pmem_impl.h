#ifndef _ZIL_PMEM_IMPL_H
#define _ZIL_PMEM_IMPL_H

#include <sys/zil_pmem_impl_limits.h>
#include <sys/zil_pmem_impl_bufpool.h>
#include <sys/zil_pmem_impl_hdr_update_chan.h>

#include <sys/zil_pmem.h>

#include <sys/zil_pmem_prb.h>

#include <sys/dsl_dataset.h>

typedef enum zilog_pmem_state {
	ZLP_ST_UNINIT = 0,
	ZLP_ST_WAITCLAIMORCLEAR = 1 << 0,
	ZLP_ST_CLAIMING = 1 << 1,
	ZLP_ST_CLAIMING_FAILED = 1 << 2,
	ZLP_ST_CLOSED = 1 << 3,
	ZLP_ST_CLOSING = 1 << 4,
	ZLP_ST_SNAPSHOT = 1 << 5,
	ZLP_ST_O_WAIT_REPLAY_OR_DESTROY = 1 << 6,
	ZLP_ST_O_REPLAYING = 1 << 7,
	ZLP_ST_O_DESTROYING = 1 << 8,
	ZLP_ST_O_LOGGING = 1 << 9,
	ZLP_ST_SYNCDESTROYED = 1 << 10,
	ZLP_ST_DESTRUCTED = 1 << 11,
	ZLP_ST_ANY = ZLP_ST_WAITCLAIMORCLEAR |
		     ZLP_ST_CLAIMING |
		     ZLP_ST_CLAIMING_FAILED |
		     ZLP_ST_CLOSED |
		     ZLP_ST_CLOSING |
		     ZLP_ST_SNAPSHOT |
		     ZLP_ST_O_WAIT_REPLAY_OR_DESTROY |
		     ZLP_ST_O_REPLAYING |
		     ZLP_ST_O_DESTROYING |
		     ZLP_ST_O_LOGGING |
		     ZLP_ST_SYNCDESTROYED |
		     ZLP_ST_DESTRUCTED,
	/* NB: keep changes in in sync with zilog_pmem_state_to_str */
} zilog_pmem_state_t;

typedef struct zilog_pmem {
	zilog_t zl_super;

	uint64_t zl_max_wr_copied_lr_length; /* set once in ctor */
	uint64_t zl_replay_buf_len;	     /* set once in ctor */

	hdr_update_chan_t zl_hdr_updates;

	rrmlock_t zl_stl;
	zilog_pmem_state_t zl_st;
	const zil_header_pmem_t *zl_replay_cur;

	spa_prb_handle_t *zl_sprbh; /* non-NULL and held while zil_open()ed */

	kmutex_t zl_commit_lock;
	zfs_bufpool_t zl_commit_lr_bufs; /* NULL while closed, non-NULL while open */
	uint64_t zl_commit_lr_buf_len;	 /* set once in ctor */

} zilog_pmem_t;

#define ZL_SPA(zilog) (zilog->zl_super.zl_spa)
#define ZL_HDR(zilog) (zilpmem_zil_header_const(zilog))
#define ZL_POOL(zilog) (zilog->zl_super.zl_dmu_pool)
#define ZL_OS(zilog) (zilog->zl_super.zl_os)

static inline const zil_header_pmem_t *const zil_header_pmem_from_zil_header_in_syncing_context_const(
    spa_t *spa, const zil_header_t *zh)
{
	ASSERT(spa_feature_is_active(spa, SPA_FEATURE_ZIL_KINDS));
	VERIFY3U(zh->zh_v2.zh_kind, ==, ZIL_KIND_PMEM);
	return (&zh->zh_v2.zh_pmem);
}

static inline zil_header_pmem_t *const zil_header_pmem_from_zil_header_in_syncing_context(spa_t *spa __maybe_unused, zil_header_t *zh)
{
	ASSERT(spa_feature_is_active(spa, SPA_FEATURE_ZIL_KINDS));
	VERIFY3U(zh->zh_v2.zh_kind, ==, ZIL_KIND_PMEM);
	return (&zh->zh_v2.zh_pmem);
}

static inline const zil_header_pmem_t *
zilpmem_zil_header_const(const zilog_pmem_t *zilog)
{
	return (zil_header_pmem_from_zil_header_in_syncing_context_const(
	    ZL_SPA(zilog), zilog->zl_super.zl_header));
}

static inline zil_header_pmem_t *
zilpmem_header_in_syncing_context(zilog_pmem_t *zilog)
{
	return ((zil_header_pmem_t *)(zilpmem_zil_header_const(zilog)));
}

static inline __attribute__((always_inline))
zilog_pmem_t *
zilpmem_downcast(zilog_t *zilog)
{
	VERIFY3P(zilog->zl_vtable, ==, &zilpmem_vtable);
	return ((zilog_pmem_t *)zilog);
}

static inline uint64_t
zilpmem_hdr_update_chan_send(zilog_pmem_t *zilog, zil_header_pmem_t u, dmu_tx_t *tx, void *tag)
{
	mutex_enter(&zilog->zl_hdr_updates.mtx);
	/* XXX assert tx is assigned */
	hdr_update_t hu = {
	    .txg = dmu_tx_get_txg(tx),
	    .upd = u,
	};
	_hdr_update_chan_send(&zilog->zl_hdr_updates, hu, tag);
	dsl_dataset_dirty(dmu_objset_ds(ZL_OS(zilog)), tx);
	mutex_exit(&zilog->zl_hdr_updates.mtx);
	return (hu.txg);
}

static inline void
zilpmem_hdr_update_chan_send_from_open_txg_wait_synced(zilog_pmem_t *zilog, zil_header_pmem_t u, void *tag)
{
	dmu_tx_t *tx;
	tx = dmu_tx_create(ZL_OS(zilog));
	VERIFY0(dmu_tx_assign(tx, TXG_WAIT));

	uint64_t txg = zilpmem_hdr_update_chan_send(zilog, u, tx, tag);

	txg = dmu_tx_get_txg(tx);
	dmu_tx_commit(tx);

	txg_wait_synced(ZL_POOL(zilog), txg);
}

#include <sys/zil_pmem_impl_state_tracking.h>

#endif /* _ZIL_PMEM_IMPL_H */
