#include <sys/spa.h>
#include <sys/dmu_objset.h>
#include <sys/zvol.h>
#include <sys/zvol_impl.h>
#include <sys/zil_impl.h>
#include <sys/zil_lwb.h>
#include <sys/dmu_tx.h>

/*
 * zvol_log_write() handles synchronous writes using TX_WRITE ZIL transactions.
 *
 * We store data in the log buffers if it's small enough.
 * Otherwise we will later flush the data out via dmu_sync().
 */
ssize_t zvol_immediate_write_sz = 32768;

static inline itx_t *
zvol_log_write_itx_create(size_t copied_len,
    itx_wr_state_t write_state,
    offset_t off, ssize_t len, boolean_t sync, zvol_state_t *zv)
{
        itx_t *itx;
        lr_write_t *lr;

        itx = zil_itx_create(TX_WRITE, sizeof (*lr) + copied_len);
        lr = (lr_write_t *)&itx->itx_lr;
        itx->itx_wr_state = write_state;
        lr->lr_foid = ZVOL_OBJ;
        lr->lr_offset = off;
        lr->lr_length = len;
        lr->lr_blkoff = 0;
        BP_ZERO(&lr->lr_blkptr);

        itx->itx_private = zv;
        itx->itx_sync = sync;

        itx->itx_callback = NULL;
        itx->itx_callback_data = NULL;

        return (itx);
}

void
zvol_log_write_begin(zilog_t *zilog, dmu_tx_t *tx,
    zvol_state_t *zv,
    uint32_t blocksize,
    offset_t off, ssize_t nbytes, boolean_t sync,
     zvol_log_write_t *pc)
{
	pc->zilog = zilog;
	pc->tx = tx;
	pc->off = off;
	pc->nbytes = nbytes;
	pc->sync = sync;
	pc->zv = zv;
	pc->blocksize = blocksize;

	itx_wr_state_t write_state;

	if (zilog->zl_logbias == ZFS_LOGBIAS_THROUGHPUT)
		write_state = WR_INDIRECT;
	else if (!spa_has_slogs(zilog->zl_spa) &&
	    nbytes >= blocksize && blocksize > zvol_immediate_write_sz)
		write_state = WR_INDIRECT;
	else if (sync)
		write_state = WR_COPIED;
	else
		write_state = WR_NEED_COPY;

	if (write_state == WR_INDIRECT && !zil_supports_wr_indirect(zilog))
		write_state = WR_NEED_COPY;

	uint64_t max_wr_copied_lr_length = zil_max_copied_data(zilog);
	if (write_state == WR_COPIED && nbytes > max_wr_copied_lr_length)
		write_state = WR_NEED_COPY;

	switch (write_state) {
	case WR_COPIED:
		pc->u.precopy = zvol_log_write_itx_create(
		    pc->nbytes,
		    write_state,
		    pc->off,
		    pc->nbytes,
		    pc->sync,
		    pc->zv);

		pc->st = ZVOL_LOG_WRITE_PRECOPY_WAITING_TO_FILL;
		return;

	case WR_NEED_COPY:
	case WR_INDIRECT:
		pc->u.noprecopy = write_state;
		pc->st = ZVOL_LOG_WRITE_NOPRECOPY;
		return;

	default:
		panic("unexpected itx_wr_state_t %d", write_state);
	}
}

void
zvol_log_write_cancel(zvol_log_write_t *pc)
{
	switch (pc->st) {
	case ZVOL_LOG_WRITE_UNLINKED:
		/* fallthrough */
	case ZVOL_LOG_WRITE_NOPRECOPY:
		/* fallthrough */
	case ZVOL_LOG_WRITE_CANCELLED:
		pc->st = ZVOL_LOG_WRITE_CANCELLED;
		return;

	case ZVOL_LOG_WRITE_PRECOPY_WAITING_TO_FILL:
		/* fallthrough */
	case ZVOL_LOG_WRITE_PRECOPY_FILLED:
		zil_itx_free_do_not_run_callback(pc->u.precopy);
		pc->st = ZVOL_LOG_WRITE_CANCELLED;
		return;

	case ZVOL_LOG_WRITE_FINISHED:
		/* fallthrough */
	default:
		panic("unexpected zvol_log_write state %d", pc->st);
	}
}

uint8_t *
zvol_log_write_get_prefill_buf(zvol_log_write_t *pc, size_t *buf_size)
{
	switch (pc->st) {
	case ZVOL_LOG_WRITE_UNLINKED:
		/* fallthrough */
	case ZVOL_LOG_WRITE_NOPRECOPY:
		*buf_size = 0;
		return NULL;

	case ZVOL_LOG_WRITE_PRECOPY_WAITING_TO_FILL:
		*buf_size = pc->nbytes;
		return ((void *)(&pc->u.precopy->itx_lr)) + sizeof(lr_write_t);

	case ZVOL_LOG_WRITE_CANCELLED:
		/* fallthrough */
	case ZVOL_LOG_WRITE_PRECOPY_FILLED:
		/* fallthrough */
	case ZVOL_LOG_WRITE_FINISHED:
		/* fallthrough */
	default:
		panic("unexpected zvol_log_write state %d", pc->st);
	}
}

void
zvol_log_write_prefilled(zvol_log_write_t *pc, uint64_t tx_bytes)
{
	switch (pc->st) {
	case ZVOL_LOG_WRITE_PRECOPY_WAITING_TO_FILL:
		break;
	case ZVOL_LOG_WRITE_UNLINKED:
		/* fallthrough */
	case ZVOL_LOG_WRITE_NOPRECOPY:
		/* fallthrough */
	case ZVOL_LOG_WRITE_CANCELLED:
		/* fallthrough */
	case ZVOL_LOG_WRITE_PRECOPY_FILLED:
		/* fallthrough */
	case ZVOL_LOG_WRITE_FINISHED:
		/* fallthrough */
	default:
		panic("unexpected zvol_log_write state %d", pc->st);
	}
	ASSERT3S(pc->st, ==, ZVOL_LOG_WRITE_PRECOPY_WAITING_TO_FILL);

	if (tx_bytes != pc->nbytes) {
#ifdef __KERNEL__
		pr_debug("zvol_log_write_prefilled: discarding pre-filled state due to tx_bytes=%llu != %zu=pc->nbytes\n", tx_bytes, pc->nbytes);
#endif
		/* XXX keep code in sync with zvol_log_write_finished() */
		ASSERT3S(pc->u.precopy->itx_wr_state, ==, WR_COPIED);
		zil_itx_free_do_not_run_callback(pc->u.precopy);
		pc->u.precopy = NULL;
		pc->st = ZVOL_LOG_WRITE_NOPRECOPY;
		pc->u.noprecopy = WR_COPIED;
	} else {
		pc->st = ZVOL_LOG_WRITE_PRECOPY_FILLED;
	}
}

void
zvol_log_write_finish(zvol_log_write_t *pc, uint64_t tx_bytes)
{
	VERIFY3U(tx_bytes, ==, pc->nbytes); /* if this holds we can avoid the need to fill late using dmu_read_by_dnode if we require filling before finish */

	/*
	 * zil_replaying() is side-effectful: it indicates to the ZIL that the
	 * replay of a log entry has been done => cannot call it earlier.
	 */
	boolean_t replaying = zil_replaying(pc->zilog, pc->tx);

	if (replaying) {
		switch (pc->st) {
		case ZVOL_LOG_WRITE_PRECOPY_WAITING_TO_FILL:
			/* fallthrough */
		case ZVOL_LOG_WRITE_PRECOPY_FILLED:
			zil_itx_free_do_not_run_callback(pc->u.precopy);
			/* fallthrough */
		case ZVOL_LOG_WRITE_UNLINKED:
			/* fallthrough */
		case ZVOL_LOG_WRITE_NOPRECOPY:
			/* fallthrough */
			pc->st = ZVOL_LOG_WRITE_FINISHED;
			goto out;

		case ZVOL_LOG_WRITE_CANCELLED:
			/* fallthrough */
		case ZVOL_LOG_WRITE_FINISHED:
			/* fallthrough */
		default:
			panic("unexpected zvol_log_write state %d", pc->st);
		}
	}
	ASSERT(!replaying);


examine:
	switch (pc->st) {
	case ZVOL_LOG_WRITE_UNLINKED:
		goto out;

	case ZVOL_LOG_WRITE_PRECOPY_FILLED:
		if (tx_bytes == pc->nbytes) {
			itx_t *itx = pc->u.precopy;
			ASSERT3S(itx->itx_wr_state, ==, WR_COPIED);
			zil_itx_assign(pc->zilog, itx, pc->tx);
			goto out_wrlog_count;
		}
		/* fallthrough */
	case ZVOL_LOG_WRITE_PRECOPY_WAITING_TO_FILL:
		/* XXX keep code in sync with zvol_log_write_prefill() */
		ASSERT3S(pc->u.precopy->itx_wr_state, ==, WR_COPIED);
		zil_itx_free_do_not_run_callback(pc->u.precopy);
		pc->u.precopy = NULL;
		pc->st = ZVOL_LOG_WRITE_NOPRECOPY;
		pc->u.noprecopy = WR_COPIED;
		goto examine;

	case ZVOL_LOG_WRITE_NOPRECOPY:
		break;

	case ZVOL_LOG_WRITE_CANCELLED:
		/* fallthrough */
	case ZVOL_LOG_WRITE_FINISHED:
		/* fallthrough */
	default:
		panic("unexpected zvol_log_write state %d", pc->st);
	}

	VERIFY3S(pc->st, ==, ZVOL_LOG_WRITE_NOPRECOPY);
	itx_wr_state_t write_state = pc->u.noprecopy;
	itx_t *itx;

	if (write_state == WR_NEED_COPY) {
		itx = zvol_log_write_itx_create(
		    0,
		    write_state,
		    pc->off,
		    pc->nbytes,
		    pc->sync,
		    pc->zv);
		zil_itx_assign(pc->zilog, itx, pc->tx);
	} else if (write_state == WR_INDIRECT) {
		const uint32_t blocksize = pc->blocksize;
		uint64_t resid = pc->nbytes;
		uint64_t off = pc->off;
		while (resid) {
			ssize_t len =
			    MIN(blocksize - P2PHASE(off, blocksize), resid);
			itx = zvol_log_write_itx_create(
			    0,
			    write_state,
			    off,
			    len,
			    pc->sync,
			    pc->zv);

			zil_itx_assign(pc->zilog, itx, pc->tx);

			off += len;
			resid -= len;
		}
	} else {
		ASSERT3S(write_state, ==, WR_COPIED);
		panic("unreachable, zvol can always prefill");
	}

out_wrlog_count:
	if (write_state == WR_COPIED || write_state == WR_NEED_COPY) {
		dsl_pool_wrlog_count(pc->zilog->zl_dmu_pool, pc->nbytes, pc->tx->tx_txg);
	}
out:
	return;
}

/*
 * Log a DKIOCFREE/free-long-range to the ZIL with TX_TRUNCATE.
 */
void
zvol_log_truncate(zvol_state_t *zv, dmu_tx_t *tx, uint64_t off, uint64_t len,
    boolean_t sync)
{
	itx_t *itx;
	lr_truncate_t *lr;
	zilog_t *zilog = zv->zv_zilog;

	if (zil_replaying(zilog, tx))
		return;

	itx = zil_itx_create(TX_TRUNCATE, sizeof (*lr));
	lr = (lr_truncate_t *)&itx->itx_lr;
	lr->lr_foid = ZVOL_OBJ;
	lr->lr_offset = off;
	lr->lr_length = len;

	itx->itx_sync = sync;
	zil_itx_assign(zilog, itx, tx);
}

static int
zvol_get_data_wr_need_copy(void *arg, lr_write_t *lr, char *buf, size_t buf_len)
{
	zvol_state_t *zv = arg;
	uint64_t offset = lr->lr_offset;
	uint64_t size = lr->lr_length;
	int error;

	ASSERT3U(size, ==, buf_len);

	zfs_locked_range_t *rl_lr;
	rl_lr = zfs_rangelock_enter(&zv->zv_rangelock, offset,
	    size, RL_READER);
	error = dmu_read_by_dnode(zv->zv_dn, offset, size, buf,
	    DMU_READ_NO_PREFETCH);
	zfs_rangelock_exit(rl_lr);

	return (SET_ERROR(error));
}

/* ARGSUSED */
static void
zvol_get_data_wr_indirect_done(zgd_t *zgd, int error)
{
	if (zgd->zgd_db)
		dmu_buf_rele(zgd->zgd_db, zgd);

	zfs_rangelock_exit(zgd->zgd_lr);

	kmem_free(zgd, sizeof (zgd_t));
}


static int
zvol_get_data_wr_indirect(void *arg, lr_write_t *lr, struct lwb *lwb, zio_t *zio)
{
	zvol_state_t *zv = arg;
	uint64_t offset = lr->lr_offset;
	uint64_t size = lr->lr_length;
	dmu_buf_t *db;
	zgd_t *zgd;
	int error;

	ASSERT3P(lwb, !=, NULL);
	ASSERT3P(zio, !=, NULL);
	ASSERT3U(size, !=, 0);

	zgd = (zgd_t *)kmem_zalloc(sizeof (zgd_t), KM_SLEEP);
	zgd->zgd_lwb = lwb;

	/*
	 * Have to lock the whole block to ensure when it's written out
	 * and its checksum is being calculated that no one can change
	 * the data. Contrarily to zfs_get_data we need not re-check
	 * blocksize after we get the lock because it cannot be changed.
	 */
	size = zv->zv_volblocksize;
	offset = P2ALIGN_TYPED(offset, size, uint64_t);
	zgd->zgd_lr = zfs_rangelock_enter(&zv->zv_rangelock, offset,
	    size, RL_READER);
	error = dmu_buf_hold_by_dnode(zv->zv_dn, offset, zgd, &db,
		DMU_READ_NO_PREFETCH);
	if (error == 0) {
		blkptr_t *bp = &lr->lr_blkptr;

		zgd->zgd_db = db;
		zgd->zgd_bp = bp;

		ASSERT(db != NULL);
		ASSERT(db->db_offset == offset);
		ASSERT(db->db_size == size);

		error = dmu_sync(zio, lr->lr_common.lrc_txg,
		    zvol_get_data_wr_indirect_done, zgd);

		if (error == 0)
			return (0);
	}

	zvol_get_data_wr_indirect_done(zgd, error);

	return (SET_ERROR(error));
}


/*
 * Get data to generate a TX_WRITE intent log record.
 */
int
zvol_get_data(void *arg, uint64_t arg2, lr_write_t *lr, char *buf,
    struct lwb *lwb, zio_t *zio)
{
	if (buf != NULL) {
		return (zvol_get_data_wr_need_copy(arg, lr, buf, lr->lr_length));
	} else {
		return (zvol_get_data_wr_indirect(arg, lr, lwb, zio));
	}
}
