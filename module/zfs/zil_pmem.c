#include <sys/zil_pmem_impl.h>
#include <sys/dmu_tx.h>
#include <sys/spa_impl.h>
#include <sys/vdev_impl.h>
#include <sys/zil_pmem_prb.h>
#include <sys/zil_pmem_spa.h>

#ifdef _KERNEL
#define schedule schedule
#else
#include <sched.h>
#define schedule sched_yield
#endif

/* for easier grepping */
#define	ZIL_VFUNC(name) name

#define	ZIL_UPCAST(name) ((zilog_t *)name)


static boolean_t
_zlp_limits_check_ctor(char *fmt, ...)
{
	va_list args;
 	va_start(args, fmt);
 	char *ret = kmem_vasprintf(fmt, args);
 	va_end(args);
	zfs_dbgmsg("%s", ret);
#ifdef KERNEL
	pr_debug("%s", ret);
#endif
	panic("%s", ret);
}

ZLPLIMITCHECKFN(zlp_limits_check_ctor, boolean_t, B_TRUE,
    _zlp_limits_check_ctor);


/* FIXME make tunable (needs mutex for ctor + call check function) */
static zilog_pmem_limits_t zil_pmem_limits_tmpl = {
	.zlplim_prb_min_chunk_size = ZILPMEM_PRB_CHUNKSIZE, /* XXX hacky */
	.zlplim_max_lr_write_lr_length = 1<<14,
	.zlplim_read_maxreclen = 1<<17,
};


enum zilpmem_stat_id {
	ZILPMEM_STAT_WRITE_ENTRY_TIME,
	ZILPMEM_STAT_WRITE_ENTRY_COUNT,
	ZILPMEM_STAT_GET_DATA_TIME,
	ZILPMEM_STAT_GET_DATA_COUNT,
	ZILPMEM_STAT__COUNT,
};

struct zfs_percpu_counter_stat zilpmem_stats[ZILPMEM_STAT__COUNT] = {
	{ZILPMEM_STAT_WRITE_ENTRY_TIME, "write_entry_time"},
	{ZILPMEM_STAT_WRITE_ENTRY_COUNT, "write_entry_count"},
	{ZILPMEM_STAT_GET_DATA_TIME, "get_data_time"},
	{ZILPMEM_STAT_GET_DATA_COUNT, "get_data_count"},
};

struct zfs_percpu_counter_statset zilpmem_statset = {
	.kstat_name = "zil_pmem",
	.ncounters = ZILPMEM_STAT__COUNT,
	.counters = zilpmem_stats,
};

static void
zilpmem_init(void)
{
	zilpmem_prb_init();
	zfs_percpu_counter_statset_create(&zilpmem_statset);
}

static void
zilpmem_fini(void)
{
	zfs_percpu_counter_statset_destroy(&zilpmem_statset);
	zilpmem_prb_fini();
}

static void
ZIL_VFUNC(zilpmem_ctor)(zilog_t *super)
{
	zilog_pmem_t *zilog = zilpmem_downcast(super);

	rrm_init(&zilog->zl_stl, B_FALSE);

	if (dmu_objset_is_snapshot(ZL_OS(zilog))) {
		zilpmem_st_upd_impl(zilog, ZLP_ST_SNAPSHOT);
	} else {
		zilpmem_st_upd_impl(zilog, ZLP_ST_CLOSED);
	}

	zilog->zl_replay_cur = NULL;

	hdr_update_chan_ctor(&zilog->zl_hdr_updates);

	zilog->zl_sprbh = NULL;

	mutex_init(&zilog->zl_commit_lock, NULL, MUTEX_DEFAULT, NULL);
	zilog->zl_commit_lr_bufs.bufs = NULL;

	zilog_pmem_limits_t limits = zil_pmem_limits_tmpl;
	VERIFY(zlp_limits_check_ctor(limits));
	zilog->zl_max_wr_copied_lr_length =
	    zlp_limits_max_lr_write_lrlength_on_write(limits);
	zilog->zl_commit_lr_buf_len =
	    zlp_limits_max_lr_reclen_on_write(limits);
	zilog->zl_replay_buf_len =
	    sizeof(entry_header_t) +
	    zlp_limits_max_lr_reclen_on_read(limits);
}

static void
ZIL_VFUNC(zilpmem_dtor)(zilog_t *super)
{
	zilog_pmem_t *zl = zilpmem_downcast(super);

	zilpmem_st_enter(zl, ZLP_ST_SNAPSHOT|ZLP_ST_CLOSED|ZLP_ST_SYNCDESTROYED, FTAG);
	zilpmem_st_upd(zl, ZLP_ST_DESTRUCTED);
	zilpmem_st_exit(zl, ZLP_ST_DESTRUCTED, FTAG);
	rrm_destroy(&zl->zl_stl);

	VERIFY3P(zl->zl_sprbh, ==, NULL);

	hdr_update_chan_dtor(&zl->zl_hdr_updates);

	ASSERT(MUTEX_NOT_HELD(&zl->zl_commit_lock));
	mutex_destroy(&zl->zl_commit_lock);
	VERIFY0(zl->zl_commit_lr_bufs.bufs);
}

static uint64_t
ZIL_VFUNC(zilpmem_max_copied_data)(zilog_t *super)
{
	zilog_pmem_t *zl = zilpmem_downcast(super);
	/* not state assertions, any state is ok */
	return (zl->zl_max_wr_copied_lr_length);
}

static void
ZIL_VFUNC(zilpmem_open)(zilog_t *super)
{
	zilog_pmem_t *zl = zilpmem_downcast(super);
	zilpmem_st_enter(zl, ZLP_ST_CLOSED, FTAG);
	VERIFY(!dmu_objset_is_snapshot(ZL_OS(zl)));

	zfs_bufpool_ctor(&zl->zl_commit_lr_bufs, zl->zl_commit_lr_buf_len);

	VERIFY3P(zl->zl_sprbh, ==, NULL);
	zl->zl_sprbh = zilpmem_spa_prb_hold(zl);
	zilpmem_st_upd(zl, ZLP_ST_O_WAIT_REPLAY_OR_DESTROY);
	zilpmem_st_exit(zl, ZLP_ST_O_WAIT_REPLAY_OR_DESTROY, FTAG);
}

static void
ZIL_VFUNC(zilpmem_close)(zilog_t *super)
{
	zilog_pmem_t *zl = zilpmem_downcast(super);

	/* XXX REPLAYING once it's resumable, need to adjust logic below */
	zilpmem_st_enter(zl, ZLP_ST_O_LOGGING, FTAG);
	zilpmem_st_upd(zl, ZLP_ST_CLOSING);
	zilpmem_st_exit(zl, ZLP_ST_CLOSING, FTAG);

	zilpmem_prb_handle_t *hdl = zilpmem_spa_prb_handle_ref_inner(zl->zl_sprbh);
	VERIFY(hdl);

	/* END adapted from ZIL-LWB */

	/*
	 * XXX be more efficient about this, adapt from ZIL-LWB
	 * But I think ZIL-LWB didn't get the locking right with
	 * zilog_is_dirty ...
	 */
	txg_wait_synced(ZL_POOL(zl), 0);

	/* discard all pending commits */
	list_t commit_list;
	list_create(&commit_list, sizeof (itx_t), offsetof(itx_t, itx_node));
	zil_async_to_sync(super, 0);
	zil_fill_commit_list(super, &commit_list);
	VERIFY(list_is_empty(&commit_list));
	list_destroy(&commit_list);

	/* drop the prb log and persist the resulting state in the ZIL header */
	zil_header_pmem_t hu;
	zilpmem_prb_destroy_log(hdl, &hu);
	zilpmem_hdr_update_chan_send_from_open_txg_wait_synced(zl, hu, FTAG);

	hdl = NULL;
	zilpmem_spa_prb_rele(zl, zl->zl_sprbh);
	zl->zl_sprbh = NULL;

	zfs_bufpool_dtor(&zl->zl_commit_lr_bufs);

	zilpmem_st_enter(zl, ZLP_ST_CLOSING, FTAG);
	zilpmem_st_upd(zl, ZLP_ST_CLOSED);
	zilpmem_st_exit(zl, ZLP_ST_CLOSED, FTAG);
}

static void
ZIL_VFUNC(zilpmem_sync)(zilog_t *super, dmu_tx_t *tx)
{
	zilog_pmem_t *zilog = zilpmem_downcast(super);

	/*
	 * We are in syncing context so we cannot hold zl_stl
	 * because we'd deadlock with other ZIL methods.
	 */

	zil_header_pmem_t *zh = zilpmem_header_in_syncing_context(zilog);

	zil_header_pmem_t upd;
	boolean_t has_upd = hdr_update_chan_get_for_sync(&zilog->zl_hdr_updates,
	    dmu_tx_get_txg(tx), &upd);
	if (has_upd) {
		*zh = upd;
	}
}

static void
ZIL_VFUNC(zilpmem_destroy_sync)(zilog_t *super, dmu_tx_t *tx)
{
	zilog_pmem_t *zilog = zilpmem_downcast(super);

	/*
	 * We are in syncing context but when this method is called there
	 * should be no other code executing methods of the ZIL.
	 * => can safely grab zl_stl
	 */

	zilpmem_st_enter(zilog, ZLP_ST_CLOSED, FTAG);

	zilpmem_st_upd(zilog, ZLP_ST_SYNCDESTROYED);

	/*
	 * Since we are in syncing context we can directly modify the
	 * ZIL header and don't need to wait. This is mostly pro-forma
	 * anyways since the dataset is about to be destroyed
	 */
	zil_header_pmem_t *zh = zilpmem_header_in_syncing_context(zilog);
	zilpmem_spa_destroy_objset(ZL_OS(zilog), zh);

	zilpmem_st_exit(zilog, ZLP_ST_SYNCDESTROYED, FTAG);
}

static void
ZIL_VFUNC(zilpmem_destroy)(zilog_t *super)
{
	zilog_pmem_t *zilog = zilpmem_downcast(super);

	zilpmem_st_enter(zilog, ZLP_ST_O_WAIT_REPLAY_OR_DESTROY, FTAG);
	zilpmem_st_upd(zilog, ZLP_ST_O_DESTROYING);
	zilpmem_st_exit(zilog, ZLP_ST_O_DESTROYING, FTAG);

	zilpmem_prb_handle_t *hdl = zilpmem_spa_prb_handle_ref_inner(zilog->zl_sprbh);
	VERIFY(hdl);

	zil_header_pmem_t hu;
	zilpmem_prb_destroy_log(hdl, &hu);
	zilpmem_hdr_update_chan_send_from_open_txg_wait_synced(zilog, hu, FTAG);

	zilpmem_st_enter(zilog, ZLP_ST_O_DESTROYING, FTAG);
	zilpmem_st_upd(zilog, ZLP_ST_O_LOGGING);
	zilpmem_st_exit(zilog, ZLP_ST_O_LOGGING, FTAG);
}


static void
ZIL_VFUNC(zilpmem_commit_on_spa_not_writeable)(zilog_t *super)
{
	/* TODO assert we wouldn't be writing to PMEM */
	panic("unimpl");
}


typedef enum {
	WRNEEDCOPY_LRCHUNKER_OK,
	WRNEEDCOPY_LRCHUNKER_NOMORE,
} wrneedcopy_lr_chunker_result_what_t;

typedef struct {
	lr_write_t lr;
#ifdef ZFS_DEBUG
	uint64_t total_initial_length;
	uint64_t intial_offset;
	uint64_t total_emitted_length;
#endif
} wrneedcopy_lr_chunker_t;

static void
wrneedcopy_lr_chunker_init(const lr_write_t *lr, wrneedcopy_lr_chunker_t *st)
{
	/*
	 * Enforcing this ensures that wrneedcopy_lr_chunker_next emits
	 * at least one lr. zfs_log_write() doesn't create itxs for a write
	 * of length zero so this should be fine.
	 */
	ASSERT3S(lr->lr_length, >, 0);

	memset(st, 0, sizeof(*st));
	st->lr = *lr;
#ifdef ZFS_DEBUG
	st->total_initial_length = st->lr.lr_length;
	st->intial_offset = st->lr.lr_offset;
	st->total_emitted_length = 0;
#endif
}

/* returns B_TRUE if the iteration updated `out` */
static boolean_t
wrneedcopy_lr_chunker_next(
    wrneedcopy_lr_chunker_t *c, lr_write_t *out, size_t max_lr_length, size_t padshift)
{
	if (c->lr.lr_length == 0) {
#ifdef ZFS_DEBUG
		VERIFY3U(c->total_emitted_length, ==, c->total_initial_length);
		VERIFY3U(c->lr.lr_offset, ==, c->intial_offset + c->total_initial_length);
#endif
		return (B_FALSE);
	}

	ASSERT0(P2PHASE_TYPED(max_lr_length, (1<<padshift), uint64_t));

	ASSERT3S(max_lr_length, >, 0); /* need to make some progress */
	size_t dnow = MIN(
		P2ROUNDUP_TYPED(c->lr.lr_length, (1<<padshift), uint64_t),
		max_lr_length
	);

	ASSERT3U(c->lr.lr_common.lrc_reclen, ==, sizeof(c->lr));
	*out = c->lr;
	/*
	 * Set reclen to the correct padded size for this `out` */
	out->lr_common.lrc_reclen += dnow;
	/* set lr_length to the correct _un_-padded size for this `out` */
	if (out->lr_length > dnow) {
		/*
		 * This is the case for all but the last chunk.
		 * For the last chunk (which might also be the first)
		 * the if-condition protects us from adding the padding
		 * to the payload
		 */
		out->lr_length = dnow;
	}

	ASSERT3S(c->lr.lr_length, >=, out->lr_length); /* underflow check*/
	c->lr.lr_length -= out->lr_length;
	c->lr.lr_offset += out->lr_length;

#ifdef ZFS_DEBUG
	c->total_emitted_length += out->lr_length;
#endif

	return (B_TRUE);
}

/*
 * - staging_buffer must be of size staging_buffer_len bytes. We use the
 *   lr_write_t* to enforce alginment requirement at call site
 */
static int noinline
zilpmem_commit_itx(zilog_pmem_t *zilog, zilpmem_prb_handle_t *hdl,
     const itx_t *itx, boolean_t start_new_gen, uint64_t last_synced,
     lr_write_t *staging_buffer, size_t staging_buffer_len,
     boolean_t may_wait_for_txg_sync)
{
	int err;

	/* cf. this logic in zil_lwb.c: zillwb_process_commit_list */
	const uint64_t txg = itx->itx_lr.lrc_txg;
	const boolean_t synced = txg <= last_synced;
	const boolean_t frozen = txg > spa_freeze_txg(ZL_SPA(zilog));
	if (!(frozen || !synced)) {
		return (0);
	}

	ASSERT(!zil_lr_is_indirect_write(&itx->itx_lr));

	if (!zil_itx_is_write_need_copy(itx)) {
		/* XXX assert zil_max_wr_copied_lr_length() */
		if (itx->itx_lr.lrc_txtype == TX_WRITE) {
			/* we don't support WR_INDIRECT */
			ASSERT3S(itx->itx_wr_state, ==, WR_COPIED);

			uint64_t max_lr_length = zil_max_copied_data(ZIL_UPCAST(zilog));
			ASSERT3U(max_lr_length, ==, zilog->zl_max_wr_copied_lr_length); /* our own impl */
			lr_write_t *lrw __maybe_unused = (lr_write_t *)&itx->itx_lr;
			ASSERT3U(lrw->lr_length, <=, max_lr_length); /* the creator of the itx */
		}
		const hrtime_t pre = gethrtime();
		err = zilpmem_prb_write_entry_with_stats(
		    hdl,
		    itx->itx_lr.lrc_txg,
		    start_new_gen,
		    itx->itx_lr.lrc_reclen,
		    &itx->itx_lr,
		    may_wait_for_txg_sync,
		    NULL
		);
		const hrtime_t post = gethrtime();
		zfs_percpu_counter_statset_add(&zilpmem_statset, ZILPMEM_STAT_WRITE_ENTRY_TIME, post - pre);
		zfs_percpu_counter_statset_add(&zilpmem_statset, ZILPMEM_STAT_WRITE_ENTRY_COUNT, 1);
#ifdef _KERNEL
		if (unlikely(zfs_flags & ZFS_DEBUG_ZIL_PMEM)) {
			char buf[ZFS_MAX_DATASET_NAME_LEN];
			dmu_objset_name(ZL_OS(zilog), buf);
        		pr_debug("zilpmem_commit_itx(): %s: wrote entry txtype=%llu err=%d\n", buf, itx->itx_lr.lrc_txtype, err);
		}
#endif
		if (err != 0) {
			zfs_dbgmsg("pmem write error for non-wr_need_copy ITX: %d", err);
			goto out_err;
		}
		return (0);
	}

	// TODO shove those assertions somewhere or turn them into docs,
	// they are useful
	// /* assert wrstate matches lrw */
	// EQUIV((wrstate == WR_INDIRECT), !BP_IS_HOLE(&lrw->lr_blkptr));
	// IMPLY((wrstate == WR_NEED_COPY || wrstate == WR_INDIRECT),
	//     (lrw->lr_common.lrc_reclen, ==, sizeof(*lrw)));
	// IMPLY((wrstate == WR_COPIED), (lrw->lr_common.lrc_reclen, >, sizeof(*lrw)));

	ASSERT3S(itx->itx_lr.lrc_txtype, ==, TX_WRITE);
	ASSERT3S(itx->itx_wr_state, ==, WR_NEED_COPY);;
	const lr_write_t *lrw = (const lr_write_t *)&itx->itx_lr;
	/* assert the lr is in the state we expect from zfs_log_write */
	ASSERT3U(lrw->lr_common.lrc_reclen, ==, sizeof(*lrw));

	wrneedcopy_lr_chunker_t chunker;
	wrneedcopy_lr_chunker_init(lrw, &chunker);

	uint64_t chunks_written = 0;
	ASSERT3U(staging_buffer_len, >=, sizeof(lr_write_t));
	uint64_t max_lr_length = zilog->zl_max_wr_copied_lr_length;
	while (wrneedcopy_lr_chunker_next(&chunker, staging_buffer, max_lr_length, 0)) {

		/*
		 * Check wrneedcopy_lr_chunker_next result to prevent memory
		 * corruption through buffer overflow by zl_get_data.
		 */
		VERIFY3U(staging_buffer->lr_common.lrc_reclen, <=, staging_buffer_len);
		VERIFY3U(staging_buffer->lr_length, <=, staging_buffer_len - sizeof(*staging_buffer));

		const hrtime_t pre_get_data = gethrtime();
		err = zilog->zl_super.zl_get_data(itx->itx_private,
		    itx->itx_gen,
		    staging_buffer,
		    (char*)(staging_buffer + 1),
		    /* XXX use the wr_need_copy-specific function directly */
		    NULL, NULL);
		const hrtime_t post_get_data = gethrtime();
		zfs_percpu_counter_statset_add(&zilpmem_statset, ZILPMEM_STAT_GET_DATA_TIME, post_get_data - pre_get_data);
		zfs_percpu_counter_statset_add(&zilpmem_statset, ZILPMEM_STAT_GET_DATA_COUNT, 1);

		if (err != 0) {
			zfs_dbgmsg("error from get_data function while committing wr_need_copy itx: %d",
			    err);
			goto out_dochunk_err;
		}

		const hrtime_t pre_write_entry = gethrtime();
		err = zilpmem_prb_write_entry_with_stats(
		    hdl,
		    itx->itx_lr.lrc_txg,
		    /* only start a new gen if requested and if so only
		     * for the first chunk. This is correct because
		     * - the chunks are disjoint ranges (no overwrite)
		     * - have no logical dependency on each other because
		     *   they stem from the same itx
		     * - we don't need to guarantee atomicity for a
		     *   TX_WRITE itx (TODO REVIEW ZIL-LWB has the same
		     *   semantics because it breaks up WR_NEED_COPY chunks
		     *   at the lwb boundary, but in general the semantics
		     *   are different from WR_COPIED, which is somewhat
		     *   inconsistent...)
		     */
		    start_new_gen ? chunks_written == 0 : B_FALSE,
		    staging_buffer->lr_common.lrc_reclen,
		    staging_buffer,
		    may_wait_for_txg_sync,
		    NULL
		);
		const hrtime_t post_write_entry = gethrtime();
		zfs_percpu_counter_statset_add(&zilpmem_statset, ZILPMEM_STAT_WRITE_ENTRY_TIME, post_write_entry - pre_write_entry);
		zfs_percpu_counter_statset_add(&zilpmem_statset, ZILPMEM_STAT_WRITE_ENTRY_COUNT, 1);
		if (err != 0) {
			zfs_dbgmsg("pmem write werror while committing wr_need_copy itx: %d", err);
			goto out_dochunk_err;
		}
		chunks_written++;
	}

	return (0);

out_dochunk_err:
	/*
	 * zilpmem_commit will txg_wait_synced() and if we crash before that's
	 * done we'll replay the chunks we have written so far, which is correct
	 * because zil_commit isn't one atomic operation.
	 */
	/* however, we want to drain the iterator to verify its assertions */
	while (wrneedcopy_lr_chunker_next(&chunker, staging_buffer,
	    max_lr_length, 0)) {}

out_err:
	VERIFY(err);
	return (err);
}

static void
ZIL_VFUNC(zilpmem_commit)(zilog_t *super, uint64_t foid)
{
	zilog_pmem_t *zilog = zilpmem_downcast(super);

	zilpmem_st_enter(zilog, ZLP_ST_O_LOGGING, FTAG);

	/* We need to serialize committers because one a second committer that
	 * arrives after the first *might* logically depend on itxs in the first
	 * committer's commit list to be persisted before it returns.
	 *
	 * We use a simple mutex (as opposed to ZIL-LWB's commit-waiters) to
	 * avoid context switching completely for the case of 1 simulataneous
	 * committer per dataset.
	 */
	mutex_enter(&zilog->zl_commit_lock);

	zilpmem_prb_handle_t *hdl = zilpmem_spa_prb_handle_ref_inner(zilog->zl_sprbh);
	VERIFY(hdl);

	/*
	 * Lazily update the ZIL Header to state 'logging' the first time
	 * we actually call zil_commit().
	 */
	zil_header_pmem_t hu;
	boolean_t need_upd = zilpmem_prb_create_log_if_not_exists(
	    hdl, &hu);
	if (need_upd) {
		zilpmem_hdr_update_chan_send_from_open_txg_wait_synced(zilog, hu, FTAG);
	}

	list_t commit_list;
	list_create(&commit_list, sizeof (itx_t), offsetof(itx_t, itx_node));
	zil_async_to_sync(super, foid);
	zil_fill_commit_list(super, &commit_list);

	itx_t *itx;

#ifdef _KERNEL
	if (unlikely(zfs_flags & ZFS_DEBUG_ZIL_PMEM)) {
		char buf[ZFS_MAX_DATASET_NAME_LEN];
		dmu_objset_name(ZL_OS(zilog), buf);
		int nentries = 0;
		itx = list_head(&commit_list);
		while (itx  != NULL) {
			nentries++;
			itx = list_next(&commit_list, itx);
		}
		pr_debug("zilpmem_commit(): %s: commit list with %d entries\n", buf, nentries);
	}
#endif

	boolean_t first_itx_in_this_commit_call = B_TRUE;
	uint64_t last_synced = spa_last_synced_txg(ZL_SPA(zilog));
	while ((itx = list_remove_head(&commit_list)) != NULL) {

		boolean_t start_new_gen = first_itx_in_this_commit_call ||
		    (TX_OOO(itx->itx_lr.lrc_txtype) ? B_FALSE : B_TRUE);

		zfs_bufpool_buf_ref_t lrbuf;
		zfs_bufpool_get_ref(&zilog->zl_commit_lr_bufs, &lrbuf);

		int err = zilpmem_commit_itx(zilog, hdl, itx, start_new_gen,
		    last_synced,
		    /* we hold the zl_commit_lock so it's safe to use the shared buffer here */
		    /* FIXME check alignment */lrbuf.buf, lrbuf.size,
		    B_TRUE);

		zfs_bufpool_put(&lrbuf);

		if (err != 0) {
			txg_wait_synced(ZL_POOL(zilog), itx->itx_lr.lrc_txg);
			last_synced = spa_last_synced_txg(ZL_SPA(zilog));
		} else {
			/* XXX refresh last_synced sometimes ? */
		}

		(void) zil_itx_destroy(itx);

		first_itx_in_this_commit_call = B_FALSE;
	}

	list_destroy(&commit_list);

	mutex_exit(&zilog->zl_commit_lock);

	zilpmem_st_exit(zilog, ZLP_ST_O_LOGGING, FTAG);
}

typedef struct zilpmem_replay_arg {
	zilog_pmem_t *zilog;
	zil_replay_func_t **replay_func_vec;
	void *replay_func_arg1;
	uint8_t *buf;
	uint64_t buf_len;
	int err;
	uint64_t wait_synced_txg;
} zilpmem_replay_arg_t;

static int
zilpmem_replay_cb(void *rarg, const zilpmem_replay_node_t *rn,
    const zil_header_pmem_t *upd)
{
	int err;
	zilpmem_replay_arg_t *arg = rarg;
	zilog_pmem_t *zilog = arg->zilog;

	/* Verify we are in the expected state */
	zilpmem_st_enter(zilog, ZLP_ST_O_REPLAYING, FTAG);
	zilpmem_st_exit(zilog, ZLP_ST_O_REPLAYING, FTAG);


	if (zfs_flags & ZFS_DEBUG_ZIL_PMEM) {
		char buf[ZFS_MAX_DATASET_NAME_LEN];
		dmu_objset_name(ZL_OS(zilog), buf);
		char *hdr = zil_header_pmem_debug_string(ZL_HDR(zilog));
#ifdef _KERNEL
		pr_debug("zilpmem_replay_cb(): %s: rn.rn_addr=%px\n", buf, rn->rn_pmem_ptr);
#endif
		kmem_strfree(hdr);
	}


	zilpmem_prb_replay_read_replay_node_result_t res;
	size_t ignored;
	VERIFY3U(arg->buf_len, >=, sizeof(entry_header_t));
	res = zilpmem_prb_replay_read_replay_node(rn,
	    (entry_header_t *)arg->buf,
	    arg->buf + sizeof(entry_header_t),
	    arg->buf_len - sizeof(entry_header_t),
	    &ignored);
	if (res != READ_REPLAY_NODE_OK) {
		if (zfs_flags & ZFS_DEBUG_ZIL_PMEM) {
			char buf[ZFS_MAX_DATASET_NAME_LEN];
			dmu_objset_name(ZL_OS(zilog), buf);
			char *hdr = zil_header_pmem_debug_string(ZL_HDR(zilog));
#ifdef _KERNEL
			pr_debug("zilpmem_replay_cb(): read replay node err: %s: rn.rn_addr=%px res=%d\n", buf, rn->rn_pmem_ptr, res);
#endif
			kmem_strfree(hdr);
		}
		return (-1);
	}
	VERIFY3S(res, ==, READ_REPLAY_NODE_OK);

	const entry_header_t *entry = (const entry_header_t*)arg->buf;

	VERIFY3U(dmu_objset_id(ZL_OS(zilog)), ==, entry->eh_data.eh_objset_id);
	/* TODO bunch of assertions that checks that zil-guid matches, etc */

	/* TODO decryption */

	VERIFY3U(entry->eh_data.eh_len, >=, sizeof(lr_t)); /* XXX turn into error */
	lr_t *lr = (lr_t *)(entry+1); /* XXX make this const */
	VERIFY3U(lr->lrc_reclen, ==, entry->eh_data.eh_len);

	if (zil_lr_is_indirect_write(lr))
		return SET_ERROR(EINVAL); /* WR_INDIRECT not supported */

	/* TODO BEGIN share all of these checks with ZIL_LWB */

	uint64_t txtype = lr->lrc_txtype;

	/* Strip case-insensitive bit, still present in log record */
	txtype &= ~TX_CI;

	if (txtype == 0 || txtype >= TX_MAX_TYPE)
		return SET_ERROR(EINVAL);

	/*
	 * If this record type can be logged out of order, the object
	 * (lr_foid) may no longer exist.  That's legitimate, not an error.
	 */
	if (TX_OOO(txtype)) {
		err = dmu_object_info(ZL_OS(zilog),
		    LR_FOID_GET_OBJ(((lr_ooo_t *)lr)->lr_foid), NULL);
		if (err == ENOENT || err == EEXIST) {
#ifdef _KERNEL
			char buf[ZFS_MAX_DATASET_NAME_LEN];
			dmu_objset_name(ZL_OS(zilog), buf);
			pr_debug("zilpmem_replay_cb(): replay node is TX_OOO and lr_foid doesn't exist: %s: rn.rn_addr=%px err=%d\n", buf, rn->rn_pmem_ptr, err);
#endif
			err = 0;
			goto replaydone;
		}
		/* TODO fallthrough? ZIL-LWB does it and it seems to work but it seems plain wrong as well */
	}

	/* TODO END share all of these checks with ZIL_LWB */

	/*
	 * Now we're ready to invoke the replay function.
	 * The contract is that it _must_ call zil_replaying, and thus
	 * zilpmem_replaying, from within the tx where the updates is applied.
	 * zilpmem_replaying will then enqueue the ZIL header update for that
	 * tx's txg.
	 * zilpmem_replaying must be called if and only once it is clear
	 * that the transaction is going to commit.
	 * If the callback returns an error, we are allowed to retry.
	 * See below for why that is necessary.
	 *
	 * We enforce the contract with the callback through
	 * through NULL/non-NULLness of zl_replay_cur.
	 */
	VERIFY3P(zilog->zl_replay_cur, ==, NULL);
	zilog->zl_replay_cur = upd;
	err = arg->replay_func_vec[txtype](arg->replay_func_arg1, lr, B_FALSE);
	if (err != 0) {
#ifdef _KERNEL
        pr_debug("zilpmem_replay_cb(): replay function returned error, waiting for txg sync and retrying err=%d\n", err);
#endif
		/* XXX share this with zil_lwb */
		VERIFY3P(zilog->zl_replay_cur, !=, NULL); /* XXX grep for "zfs_create, existing zp, no truncation, replaying" */
		/*
		 * The DMU's dnode layer doesn't see removes until the txg
		 * commits, so a subsequent claim can spuriously fail with
		 * EEXIST. So if we receive any error we try syncing out
		 * any removes then retry the transaction.  Note that we
		 * specify B_FALSE for byteswap now, so we don't do it twice.
		 */
		txg_wait_synced(spa_get_dsl(ZL_SPA(zilog)), 0);
		err = arg->replay_func_vec[txtype](arg->replay_func_arg1, lr,
		    B_FALSE);
#ifdef _KERNEL
        pr_debug("zilpmem_replay_cb(): retry returned err=%d\n", err);
#endif
		VERIFY((err == 0 && zilog->zl_replay_cur == NULL) || (err != 0 && zilog->zl_replay_cur != NULL));
		/* fallthrough with error */
	} else {
#ifdef _KERNEL
        pr_debug("zilpmem_replay_cb(): replay function indicates success\n");
#endif
		VERIFY((err == 0 && zilog->zl_replay_cur == NULL) || (err != 0 && zilog->zl_replay_cur != NULL));
	}
	zilog->zl_replay_cur = NULL; /* lifetime of `upd` is only for this call */

replaydone:
	if (zfs_flags & ZFS_DEBUG_ZIL_PMEM) {
		char buf[ZFS_MAX_DATASET_NAME_LEN];
		dmu_objset_name(ZL_OS(zilog), buf);
		char *hdr = zil_header_pmem_debug_string(ZL_HDR(zilog));
#ifdef _KERNEL
		pr_debug("zilpmem_replay_cb(): replay node return: %s: rn.rn_addr=%px err=%d\n", buf, rn->rn_pmem_ptr, err);
#endif
		kmem_strfree(hdr);
	}

	return (err);
}

/* see comment in zilpmem_replay_cb */
static boolean_t
ZIL_VFUNC(zilpmem_replaying)(zilog_t *super, dmu_tx_t *tx)
{
	zilog_pmem_t *zilog = zilpmem_downcast(super);

	zilpmem_st_enter(zilog, ZLP_ST_O_LOGGING|ZLP_ST_O_REPLAYING, FTAG);

	if (zilog->zl_st == ZLP_ST_O_LOGGING) {
		zilpmem_st_exit(zilog, ZLP_ST_O_LOGGING, FTAG);
		return (B_FALSE);
	}

	/* TODO assert that the state is owned by the replayer, e.g. zilog */
	VERIFY3U(zilog->zl_st, ==, ZLP_ST_O_REPLAYING);
	VERIFY3P(zilog->zl_replay_cur, !=, NULL);

	if (zfs_flags & ZFS_DEBUG_ZIL_PMEM) {
		char buf[ZFS_MAX_DATASET_NAME_LEN];
		dmu_objset_name(ZL_OS(zilog), buf);
		char *dbg = zil_header_pmem_debug_string(zilog->zl_replay_cur);
#ifdef _KERNEL
		pr_debug("zilpmem_replay: %s: replaying entry: updating header: %s", buf, dbg);
#endif
		kmem_strfree(dbg);
	}

	(void) zilpmem_hdr_update_chan_send(zilog, *zilog->zl_replay_cur, tx,
	    FTAG);
	zilog->zl_replay_cur = NULL;

	zilpmem_st_exit(zilog, ZLP_ST_O_REPLAYING, FTAG);
	return (B_TRUE);
}

static boolean_t
ZIL_VFUNC(zilpmem_get_is_replaying_no_sideffects)(zilog_t *super)
{
	zilog_pmem_t *zilog = zilpmem_downcast(super);
	zilpmem_st_enter(zilog, ZLP_ST_ANY, FTAG);
	boolean_t replaying = zilog->zl_st == ZLP_ST_O_REPLAYING;
	zilpmem_st_exit(zilog,  ZLP_ST_ANY, FTAG);
	return (replaying);
}

static void
ZIL_VFUNC(zilpmem_replay)(zilog_t *super, objset_t *os, void *replay_func_arg1,
    zil_replay_func_t *replay_func[TX_MAX_TYPE])
{
	zilog_pmem_t *zilog = zilpmem_downcast(super);
	ASSERT3P(ZL_OS(zilog), ==, os);

	zilpmem_st_enter(zilog, ZLP_ST_O_WAIT_REPLAY_OR_DESTROY, FTAG);
	zilpmem_st_upd(zilog, ZLP_ST_O_REPLAYING);
	zilpmem_st_exit(zilog, ZLP_ST_O_REPLAYING, FTAG);


	if (zfs_flags & ZFS_DEBUG_ZIL_PMEM) {
		char buf[ZFS_MAX_DATASET_NAME_LEN];
		dmu_objset_name(ZL_OS(zilog), buf);
		char *hdr = zil_header_pmem_debug_string(ZL_HDR(zilog));
#ifdef _KERNEL
		pr_debug("zilpmem_replay(): %s: begin hdr=%s\n", buf, hdr);
#endif
		zfs_dbgmsg("%s: begin hdr=%s", buf, hdr);
		kmem_strfree(hdr);
	}

	zilpmem_prb_handle_t *hdl = zilpmem_spa_prb_handle_ref_inner(zilog->zl_sprbh);
	VERIFY(hdl);

	zilpmem_replay_arg_t arg = {
		.zilog = zilog,
		.replay_func_vec = replay_func,
		.replay_func_arg1 = replay_func_arg1,
		.buf_len = zilog->zl_replay_buf_len,
		.buf = vmem_alloc(zilog->zl_replay_buf_len, KM_SLEEP),
	};
	zilpmem_prb_replay_result_t res;
	VERIFY3P(zilog->zl_replay_cur, ==, NULL);
	res = zilpmem_prb_replay(hdl, zilpmem_replay_cb, &arg);
	VERIFY3P(zilog->zl_replay_cur, ==, NULL);
	vmem_free(arg.buf, arg.buf_len);

	dmu_tx_t *tx;
	tx = dmu_tx_create(ZL_OS(zilog));
	VERIFY0(dmu_tx_assign(tx, TXG_WAIT));

	zilog_pmem_state_t next_state;
	if (res.what == PRB_REPLAY_RES_OK) {
		zil_header_pmem_t hu;
		zilpmem_prb_replay_done(hdl, &hu);
		zilpmem_hdr_update_chan_send(zilog, hu, tx, FTAG);
		next_state = ZLP_ST_O_LOGGING;
	} else {
		/* Replay is resumable so we don't care */
		next_state = ZLP_ST_O_WAIT_REPLAY_OR_DESTROY;
	}

	/*
	 * Make sure all changes hit the disk
	 * XXX only do this if we actually replayed something. Detecting
	 * that situation requires more feedback from zilpmem_prb_replay or
	 * the callback.
	 */
	uint64_t wait_txg = dmu_tx_get_txg(tx);
	dmu_tx_commit(tx);
	txg_wait_synced(ZL_POOL(zilog), wait_txg);

	zilpmem_st_enter(zilog, ZLP_ST_O_REPLAYING, FTAG);
	zilpmem_st_upd(zilog, next_state);
	zilpmem_st_exit(zilog, ZLP_ST_O_WAIT_REPLAY_OR_DESTROY|ZLP_ST_O_LOGGING, FTAG);

	if (zfs_flags & ZFS_DEBUG_ZIL_PMEM) {
		char buf[ZFS_MAX_DATASET_NAME_LEN];
		dmu_objset_name(ZL_OS(zilog), buf);
		char *hdr = zil_header_pmem_debug_string(ZL_HDR(zilog));
#ifdef _KERNEL
		pr_debug("zilpmem_replay(): %s: end res=%d hdr=%s\n", buf, res.what, hdr);
#endif
		zfs_dbgmsg("%s: res=%d hdr=%s", buf, res.what, hdr);
		kmem_strfree(hdr);
	}

	/*
	 * XXX inconsistent error handling
	 * see https://github.com/openzfs/zfs/issues/11364
	 *
	 * We should be returning an error here, requires only sligh refactor.
	 */
	if (res.what != PRB_REPLAY_RES_OK) {
		panic("bubble replay error up");
	}
	(void) res;
}


static boolean_t
ZIL_VFUNC(zilpmem_is_claimed)(zilog_t *super)
{
	zilog_pmem_t *zilog = zilpmem_downcast(super);

	zilpmem_st_enter(zilog, ZLP_ST_CLOSED, FTAG);

	zil_header_pmem_t *zh = zilpmem_header_in_syncing_context(zilog);

	boolean_t might_claim =
	    zilpmem_prb_might_claim_during_recovery( zh);

	zilpmem_st_exit(zilog, ZLP_ST_CLOSED, FTAG);
	return (might_claim == B_FALSE);
}

static int
ZIL_VFUNC(zilpmem_check_log_chain)(zilog_t *super)
{
	zilog_pmem_t *zilog = zilpmem_downcast(super);

	zilpmem_st_enter(zilog, ZLP_ST_CLOSED, FTAG);

	spa_prb_handle_t *sprbh = zilpmem_spa_prb_hold(zilog);

	/* TODO dry-run of claim/replay + call spa_claim_notify */
	spa_t *spa = ZL_SPA(zilog);
	spa_claim_notify(spa, 1);

	zilpmem_spa_prb_rele(zilog, sprbh);

	zilpmem_st_exit(zilog, ZLP_ST_CLOSED|ZLP_ST_CLOSED, FTAG);

	return (0);
}

static int
ZIL_VFUNC(zilpmem_clear)(zilog_t *super, dmu_tx_t *tx)
{
	zilog_pmem_t *zilog = zilpmem_downcast(super);

	zilpmem_st_enter(zilog, ZLP_ST_CLOSED, FTAG);

	zil_header_pmem_t *zh = zilpmem_header_in_syncing_context(zilog);

	ASSERT3U(tx->tx_txg, ==, spa_first_txg(ZL_SPA(zilog)));
	ASSERT3S(spa_get_log_state(ZL_SPA(zilog)), ==, SPA_LOG_CLEAR);

	uint64_t first_txg = spa_min_claim_txg(ZL_SPA(zilog));
	(void) zilog;
	(void) zh;
	(void) first_txg;
	/* TODO */

	spa_prb_handle_t *sprbh = zilpmem_spa_prb_hold(zilog);
	zilpmem_prb_handle_t *hdl = zilpmem_spa_prb_handle_ref_inner(sprbh);

	/*
	 * We are in syncing context so there is no need to use
	 * zl_hdr_updates to update the header.
	 */
	zilpmem_prb_destroy_log(hdl, zh);
	/* TODO need to mark header dirty? */
	/* TODO really think about that dsl_dataset_dirt(), check zillwb, it's everywhere */

	hdl = NULL;
	zilpmem_spa_prb_rele(zilog, sprbh);

	zilpmem_st_exit(zilog, ZLP_ST_CLOSED, FTAG);

	return (0);
}

static int
zilpmem_claimstore__needs_store_claim(void *varg, const zilpmem_replay_node_t *rn,
    boolean_t *needs_to_store_claim)
{
	/* XXX assert that the entry is not WR_INDIRECT but that would
	 * require temporarily loading it from PMEM like we do during replay.
	 * In that case we should also do all the same plausibility checks
	 * that replay does (at least those that don't rely on previous
	 * entries having been applied)
	 */
	(void) varg;
	(void) rn;
	*needs_to_store_claim = B_FALSE;
	return (0);

}

static int
zilpmem_claimstore__claim(void *varg, const zilpmem_replay_node_t *rn)
{
	(void) varg;
	(void) rn;
	/* we return false unconditionally for needs_to_store_claim */
	panic("unreachable");
}

static claimstore_interface_t zilpmem_claimstore = {
	.prbcsi_needs_store_claim = zilpmem_claimstore__needs_store_claim,
	.prbcsi_claim = zilpmem_claimstore__claim,
};

static int
ZIL_VFUNC(zilpmem_claim)(zilog_t *super, dmu_tx_t *tx)
{
	zilog_pmem_t *zilog = zilpmem_downcast(super);
	int err;

	zilpmem_st_enter(zilog, ZLP_ST_CLOSED, FTAG);
	zilpmem_st_upd(zilog, ZLP_ST_CLAIMING);
	zilpmem_st_exit(zilog, ZLP_ST_CLAIMING, FTAG);

	zil_header_pmem_t *zh = zilpmem_header_in_syncing_context(zilog);

	uint64_t first_txg;
	ASSERT3U(tx->tx_txg, ==, spa_first_txg(ZL_SPA(zilog)));
	first_txg = spa_min_claim_txg(ZL_SPA(zilog));

	ASSERT3S(spa_get_log_state(ZL_SPA(zilog)), !=, SPA_LOG_CLEAR);

	/*
	 * If we are not rewinding and opening the pool normally, then
	 * the min_claim_txg should be equal to the first txg of the pool.
	 */
	ASSERT3U(first_txg, ==, spa_first_txg(ZL_SPA(zilog)));
	ASSERT3U(first_txg, ==, (spa_last_synced_txg(ZL_SPA(zilog)) + 1));

	spa_prb_handle_t *sprbh = zilpmem_spa_prb_hold(zilog);
	zilpmem_prb_handle_t *prbhdl = zilpmem_spa_prb_handle_ref_inner(sprbh);

	zilpmem_prb_claim_result_t res;
	res = zilpmem_prb_claim(prbhdl, zh, first_txg,
	    &zilpmem_claimstore, zilog);
	zilog_pmem_state_t next_state;
	if (res.what != PRB_CLAIM_RES_OK) {
		next_state = ZLP_ST_CLAIMING_FAILED;
		err = -1;
		/*
		 * XXX inconsistent error handling
		 * see https://github.com/openzfs/zfs/issues/11364
		 */
		panic("fix the error handling!");
	} else {
		err = 0;
		next_state = ZLP_ST_CLOSED;
	}

	zilpmem_spa_prb_rele(zilog, sprbh);

	zilpmem_st_enter(zilog, ZLP_ST_CLAIMING, FTAG);
	zilpmem_st_upd(zilog, next_state);
	zilpmem_st_exit(zilog, ZLP_ST_CLOSED|ZLP_ST_CLAIMING_FAILED, FTAG);

	return (err);
}



static int
zilpmem_reset_logs(spa_t *spa)
{
	if (spa->spa_zil_kind != ZIL_KIND_PMEM) {
#ifdef _KERNEL
		pr_debug("zil kind is %llu\n", spa->spa_zil_kind);
#endif
		VERIFY3P(spa->spa_zilpmem, ==, NULL);
		return (0);
	}

	/* BIG TODO
	 *
	 * The problem is that zilpmem_reset_logs() is not always called
	 * (... from spa_reset_logs()) because spa_reset_logs() is not always
	 * called. Instead, its callers assume that it's expensive to call
	 * spa_reset_logs() and only do so if they deem it's necessary.
	 * They decide this by inspecting whether the vdev that is offlined/removed
	 * actually has allocated space, i.e., the whole thing is tied to
	 * metaslab allocator, which we avoid, curtesy of ALLOC_BIAS_EXEMPT
	 * => The commit that adds this comments adds a bunch of hacky checks
	 *    that prevent spa_reset_logs() calls, and more generally, changes
	 *    to the entire SLOG sub-tree. (ZFS_ERR_ZIL_PMEM_INVALID_SLOG_CONFIG)
	 *    We probably need to abstract the entire 'is this SLOG vdev expensable'
	 *    logic behind the vtable in the future (where we also implement
	 *    the transparent ZIL kind switching)
	 */

	panic("unimpl & should be unreachable");
}



static void
ZIL_VFUNC(zilpmem_init_header)(void *zh, size_t size)
{
	VERIFY3U(size, ==, sizeof(zil_header_pmem_t));
	zil_header_pmem_init(zh);
}

static boolean_t
ZIL_VFUNC(zilpmem_validate_header_format)(const void *zh, size_t size)
{
	VERIFY3U(size, ==, sizeof(zil_header_pmem_t));
	return (zil_header_pmem_validate_format(zh));
}

const zil_vtable_t zilpmem_vtable = {
	.zlvt_alloc_size = sizeof (zilog_pmem_t),

	.zlvt_init = zilpmem_init,
	.zlvt_fini = zilpmem_fini,
	.zlvt_reset_logs = zilpmem_reset_logs,
	.zlvt_supports_wr_indirect = B_FALSE,
	.zlvt_validate_header_format = zilpmem_validate_header_format,
	.zlvt_init_header = zilpmem_init_header,

	.zlvt_ctor = zilpmem_ctor,
	.zlvt_dtor = zilpmem_dtor,

	.zlvt_max_copied_data = zilpmem_max_copied_data,

	.zlvt_open =	zilpmem_open,
	.zlvt_close =	zilpmem_close,

	.zlvt_commit =	zilpmem_commit,
	.zlvt_commit_on_spa_not_writeable = zilpmem_commit_on_spa_not_writeable,

	.zlvt_destroy =	zilpmem_destroy,
	.zlvt_destroy_sync =	zilpmem_destroy_sync,

	.zlvt_sync =	zilpmem_sync,

	.zlvt_replay =	zilpmem_replay,
	.zlvt_replaying =	zilpmem_replaying,
	.zlvt_get_is_replaying_no_sideffects = zilpmem_get_is_replaying_no_sideffects,

	.zlvt_check_log_chain =	zilpmem_check_log_chain,
	.zlvt_is_claimed = zilpmem_is_claimed,
	.zlvt_claim =	zilpmem_claim,
	.zlvt_clear =	zilpmem_clear,
};
