// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * Copyright (c) 2026 by [contributors]
 * File-level history tracking for ZFS datasets.
 */

#include <sys/spa.h>
#include <sys/zap.h>
#include <sys/dmu_tx.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_synctask.h>
#include <sys/cmn_err.h>
#include <sys/sunddi.h>
#include <sys/cred.h>
#if defined(_KERNEL)
#include <sys/taskq.h>
#endif
#include <sys/zfs_events.h>
#include <sys/zfs_znode.h>
#include <sys/byteorder.h>
#include <sys/zfeature.h>

/*
 * ZFS Events - File-level history tracking
 *
 * This module implements persistent event logging for file operations at the
 * dataset level. The design follows spa_history.c as a reference, using a
 * ring buffer of packed nvlists.
 *
 * Storage format:
 * The event log is stored as a DMU object containing
 * <packed record length (uint64_t LE), record nvlist> tuples.
 *
 * The header (zfs_events_phys_t) is stored in the bonus buffer and tracks:
 * - zep_phys_max_off: physical size of the log (max offset)
 * - zep_bof: logical beginning of file (oldest record)
 * - zep_eof: logical end of file (write position)
 * - zep_records_lost: count of overwritten records
 *
 * Ring buffer operation:
 * The log wraps around when zep_eof reaches zep_phys_max_off. When
 * a new record would overwrite old data, zep_bof is advanced and
 * zep_records_lost is incremented.
 */

/*
 * Name of the ZAP entry in the dataset's master node that points to the
 * event log object.
 */
#define	ZFS_EVENTS_ZAP_NAME	"com.zfs:events"

/*
 * Convert logical offset to physical offset in the ring buffer.
 */
static uint64_t
zfs_events_log_to_phys(uint64_t log_off, zfs_events_phys_t *zep)
{
	return (log_off % zep->zep_phys_max_off);
}

/*
 * Advance zep_bof to skip the oldest record, freeing space for new records.
 */
static int
zfs_events_advance_bof(objset_t *os, uint64_t obj, zfs_events_phys_t *zep)
{
	uint64_t firstread, reclen, phys_bof;
	char buf[sizeof (reclen)];
	int err;

	/*
	 * If BOF has caught up with EOF, the log is logically empty: every
	 * remaining byte is free space. Rewind BOF so the free-space check
	 * in zfs_events_write() succeeds instead of looping forever.
	 */
	if (zep->zep_bof >= zep->zep_eof) {
		zep->zep_bof = zep->zep_eof;
		return (0);
	}

	phys_bof = zfs_events_log_to_phys(zep->zep_bof, zep);
	firstread = MIN(sizeof (reclen), zep->zep_phys_max_off - phys_bof);

	if ((err = dmu_read(os, obj, phys_bof, firstread, buf,
	    DMU_READ_PREFETCH)) != 0)
		return (err);

	if (firstread != sizeof (reclen)) {
		if ((err = dmu_read(os, obj, 0,
		    sizeof (reclen) - firstread, buf + firstread,
		    DMU_READ_PREFETCH)) != 0)
			return (err);
	}

	reclen = LE_64(*((uint64_t *)buf));

	/*
	 * A zero or oversized length means the BOF region doesn't hold a
	 * valid record header (unwritten space after a full wrap, or
	 * corruption). Treat everything up to EOF as free space rather
	 * than spinning on invalid data.
	 */
	if (reclen == 0 || reclen + sizeof (reclen) >
	    zep->zep_eof - zep->zep_bof) {
		zep->zep_records_lost++;
		zep->zep_bof = zep->zep_eof;
		return (0);
	}

	zep->zep_bof += reclen + sizeof (reclen);
	zep->zep_records_lost++;
	return (0);
}

/*
 * Write data to the event log at the current EOF position, wrapping if needed.
 */
static int
zfs_events_write(objset_t *os, uint64_t obj, void *buf, uint64_t len,
    zfs_events_phys_t *zep, dmu_tx_t *tx)
{
	uint64_t firstwrite, phys_eof;
	int err;

	/*
	 * Advance BOF if we need to make room. The theoretical maximum
	 * iterations is phys_max_off / sizeof (reclen); cap well above that
	 * so a corrupt header can never spin forever.
	 */
	uint64_t spin = 0;
	while (zep->zep_phys_max_off - (zep->zep_eof - zep->zep_bof) <= len) {
		if (++spin > (zep->zep_phys_max_off >> 3) + 1)
			return (SET_ERROR(EIO));
		if ((err = zfs_events_advance_bof(os, obj, zep)) != 0)
			return (err);
	}

	phys_eof = zfs_events_log_to_phys(zep->zep_eof, zep);
	firstwrite = MIN(len, zep->zep_phys_max_off - phys_eof);
	zep->zep_eof += len;

	dmu_write(os, obj, phys_eof, firstwrite, buf, tx,
	    DMU_READ_NO_PREFETCH);

	len -= firstwrite;
	if (len > 0) {
		/* wrap around to beginning of buffer */
		dmu_write(os, obj, 0, len, (char *)buf + firstwrite, tx,
		    DMU_READ_NO_PREFETCH);
	}

	return (0);
}

/*
 * Create the event log object for a dataset.
 * max_size specifies the ring buffer size in bytes.
 */
int
zfs_events_create_obj(objset_t *os, dmu_tx_t *tx, uint64_t max_size,
    uint64_t *objp)
{
	dmu_buf_t *dbp;
	zfs_events_phys_t *zep;
	uint64_t obj;

	/* Clamp size to valid range */
	if (max_size < ZFS_EVENTS_MIN_SIZE)
		max_size = ZFS_EVENTS_MIN_SIZE;
	if (max_size > ZFS_EVENTS_MAX_SIZE)
		max_size = ZFS_EVENTS_MAX_SIZE;

	/* Use DMU_OTN_UINT8_METADATA for the event log data */
	obj = dmu_object_alloc(os, DMU_OTN_UINT8_METADATA,
	    SPA_OLD_MAXBLOCKSIZE, DMU_OTN_UINT64_METADATA,
	    sizeof (zfs_events_phys_t), tx);

	VERIFY0(dmu_bonus_hold(os, obj, FTAG, &dbp));
	ASSERT3U(dbp->db_size, >=, sizeof (zfs_events_phys_t));

	zep = dbp->db_data;
	dmu_buf_will_dirty(dbp, tx);

	/*
	 * Initialize the event log header with the specified size.
	 */
	memset(zep, 0, sizeof (zfs_events_phys_t));
	zep->zep_phys_max_off = max_size;
	zep->zep_version = ZFS_EVENTS_VERSION;

	dmu_buf_rele(dbp, FTAG);

	*objp = obj;
	return (0);
}

/*
 * Add transaction holds for event logging to the caller's transaction.
 *
 * Must be called before dmu_tx_assign() on any VFS path that may log
 * events. Mirrors zfs_fuid_txhold(): when the log object doesn't exist
 * yet we hold DMU_NEW_OBJECT plus a ZAP add on the master node; once it
 * exists we hold its bonus and enough write space for one maximum
 * record.
 */
void
zfs_events_txhold(objset_t *os, dmu_tx_t *tx)
{
	uint64_t obj = 0;

	(void) zap_lookup(os, MASTER_NODE_OBJ, ZFS_EVENTS_ZAP_NAME,
	    sizeof (uint64_t), 1, &obj);

	if (obj == 0) {
		/*
		 * The log object doesn't exist yet: it will be created in
		 * this tx. Its first records land in the first blocks, so
		 * a single maximum-sized write hold covers them.
		 */
		dmu_tx_hold_bonus(tx, DMU_NEW_OBJECT);
		dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0,
		    MIN(ZFS_EVENTS_MAX_SIZE, DMU_MAX_ACCESS));
		dmu_tx_hold_zap(tx, MASTER_NODE_OBJ, TRUE,
		    ZFS_EVENTS_ZAP_NAME);
	} else {
		/*
		 * Hold the log's actual physical range, from its bonus
		 * header (zep_phys_max_off), not the current events_size:
		 * the two diverge once events_size is changed, since that
		 * does not resize an existing log. A hold that only covers
		 * the property range panics with "dirtying dbuf ... but
		 * not tx_held" once eof crosses into blocks beyond it.
		 * Write holds are capped at DMU_MAX_ACCESS, so cover the
		 * range in chunks.
		 */
		dmu_buf_t *dbp;
		uint64_t max_off = ZFS_EVENTS_MAX_SIZE;

		if (dmu_bonus_hold(os, obj, FTAG, &dbp) == 0) {
			zfs_events_phys_t *zep = dbp->db_data;

			if (zep->zep_phys_max_off != 0)
				max_off = zep->zep_phys_max_off;
			dmu_buf_rele(dbp, FTAG);
		}

		dmu_tx_hold_bonus(tx, obj);
		for (uint64_t off = 0; off < max_off;
		    off += DMU_MAX_ACCESS) {
			uint64_t len = MIN(DMU_MAX_ACCESS, max_off - off);

			dmu_tx_hold_write(tx, obj, off, len);
		}
	}
}

/*
 * Sync-context callback to activate the events feature. spa_feature_incr()
 * requires a syncing transaction, so it must never be called from open
 * context; queue it on the caller's transaction group instead.
 */
static void
zfs_events_feature_sync(void *arg, dmu_tx_t *tx)
{
	dsl_dataset_t *ds = arg;

	/*
	 * Activate as a per-dataset feature: this records the feature
	 * in the dataset's MOS zap and increments the pool-wide
	 * refcount. dsl_destroy_head_sync_impl() deactivates all
	 * per-dataset features on destroy, so the refcount is released
	 * symmetrically when the dataset goes away.
	 */
	dsl_dataset_activate_feature(ds->ds_object, SPA_FEATURE_EVENTS,
	    (void *)B_TRUE, tx);
}

#if defined(_KERNEL)
/*
 * Task argument for the owned-tx activation path: the taskq context
 * must not reference a znode or a transaction; the objset hold the
 * emitter held is still live when the task runs (the emitter only
 * creates this work item while inside the log's own hold), but the
 * task re-holds nothing - it opens its own transaction immediately.
 */
typedef struct zfs_events_activate_arg {
	objset_t	*aoa_os;
	dsl_dataset_t	*aoa_ds;
} zfs_events_activate_arg_t;

/*
 * Activate the events feature from open context without a caller
 * transaction: open an ad-hoc tx here (no VFS locks are held in
 * taskq context, so DMU_TX_WAIT is safe) and queue the feature-sync
 * sync task on it. Idempotent: skips when the feature is already
 * active on the dataset.
 */
static void
zfs_events_activate_task(void *arg)
{
	zfs_events_activate_arg_t *a = arg;
	dsl_dataset_t *ds = a->aoa_ds;
	objset_t *os = a->aoa_os;
	dmu_tx_t *tx;

	kmem_free(a, sizeof (*a));

	if (dsl_dataset_feature_is_active(ds, SPA_FEATURE_EVENTS))
		return;

	tx = dmu_tx_create(os);
	VERIFY0(dmu_tx_assign(tx, DMU_TX_WAIT));
	zfs_events_feature_sync(ds, tx);
	dmu_tx_commit(tx);
}
#endif	/* _KERNEL */

/*
 * Destroy the event log object for a dataset.
 * Also decrements the events feature counter.
 */
int
zfs_events_destroy_obj(objset_t *os, uint64_t obj, dmu_tx_t *tx)
{
	spa_t *spa = dmu_objset_spa(os);
	int err;

	err = dmu_object_free(os, obj, tx);
	if (err == 0 && spa_feature_is_active(spa, SPA_FEATURE_EVENTS)) {
		spa_feature_decr(spa, SPA_FEATURE_EVENTS, tx);
	}

	return (err);
}

/*
 * dsl_sync_task callback for the ioctl clear path: runs in syncing
 * context where tx assignment is legal.
 */
/*
 * Open-context clear used by the ioctl path. Mirrors the VFS event
 * logging path: prepare transaction holds with zfs_events_txhold()
 * and assign with DMU_TX_WAIT. The caller MUST NOT hold the pool
 * config lock: dmu_tx_assign(DMU_TX_WAIT) asserts it is free.
 * Returns 0, or ENOENT when the dataset has no event log.
 */
int
zfs_events_clear_task(objset_t *os)
{
	dmu_tx_t *tx;
	uint64_t count = 0;
	int err;

	/*
	 * The caller must guarantee the pool config lock is not held by
	 * this thread (DMU_TX_WAIT asserts it) and, when the dataset is
	 * mounted, must hold the zfsvfs ring lock so the reset cannot
	 * interleave with concurrent VFS loggers.
	 */
	tx = dmu_tx_create(os);
	zfs_events_txhold(os, tx);
	err = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (err != 0) {
		dmu_tx_abort(tx);
		return (err);
	}

	err = zfs_events_clear(os, tx, &count);
	dmu_tx_commit(tx);

	return (err);
}

/*
 * Clear a dataset's event log: reset the ring header so subsequent
 * reads return nothing. Returns ENOENT if the dataset has no event
 * log. The lost-record counter is reset along with the ring pointers;
 * clearing means discarding all history.
 * Must be called from syncing context with a transaction that holds
 * the log object's bonus (dmu_tx_hold_bonus).
 */
int
zfs_events_clear(objset_t *os, dmu_tx_t *tx, uint64_t *countp)
{
	dmu_buf_t *dbp;
	zfs_events_phys_t *zep;
	uint64_t obj = 0;
	int err;

	err = zap_lookup(os, MASTER_NODE_OBJ, ZFS_EVENTS_ZAP_NAME,
	    sizeof (uint64_t), 1, &obj);
	if (err != 0)
		return (err);

	err = dmu_bonus_hold(os, obj, FTAG, &dbp);
	if (err != 0)
		return (err);

	zep = dbp->db_data;
	if (countp != NULL)
		*countp = zep->zep_eof - zep->zep_bof;

	dmu_buf_will_dirty(dbp, tx);
	zep->zep_bof = 0;
	zep->zep_eof = 0;
	zep->zep_records_lost = 0;

	dmu_buf_rele(dbp, FTAG);
	return (0);
}

/*
 * Internal helper to log an event to the event log.
 * Creates the event log object lazily if it doesn't exist.
 *
 * Note: This function assumes that the caller has already verified
 * that events are enabled (via zfsvfs->z_events), has registered
 * transaction holds with zfs_events_txhold() before dmu_tx_assign(),
 * and passes the dataset's cached events_size (zfsvfs->z_events_size).
 * We must not look up the events_size property here: VFS write paths
 * do not hold the pool config lock that dsl_prop_get_int_ds() requires.
 *
 * The record's txg label is passed separately (txg): emitters with a
 * live transaction pass dmu_tx_get_txg(tx); transaction-less callers
 * (READ accounting, deferred fence flushes) pass 0, in which case the
 * pool's open txg is sampled under txg_hold_open() - a commit-timeline
 * anchor, not a transaction that carried the IO. When tx is NULL an
 * ad-hoc transaction is opened here for the ring append itself: the
 * existing append path (dmu_buf_will_dirty etc.) requires one.
 */
static void
zfs_events_log_event(objset_t *os, dmu_tx_t *tx, nvlist_t *nvl,
    uint64_t events_size, uint64_t *objp, kmutex_t *lockp, uint64_t txg)
{
	dmu_buf_t *dbp;
	zfs_events_phys_t *zep;
	uint64_t obj;
	char *packed = NULL;
	size_t packed_len;
	uint64_t le_len;
	dsl_pool_t *dp;
	dmu_tx_t *atx;
	boolean_t owned;
	int err;

	/*
	 * Transaction-less callers (READ accounting, deferred fence
	 * flushes) get an ad-hoc transaction for the ring append and
	 * attribute the record to the pool's open txg, sampled under
	 * the txg open-lock (txg_hold_open() is not usable here: its
	 * count is only lowered by txg_rele_to_sync(), so a
	 * sample-only hold would leak a count and stall quiescing -
	 * see the sampling block below). Callers with a live
	 * transaction keep their own tx and txg.
	 */
	owned = (tx == NULL);
	atx = tx;
	if (txg == 0) {
		/*
		 * Sample the pool's open txg under the open-lock.
		 * txg_hold_open() is deliberately NOT used here: it
		 * raises txg's tc_count and only txg_rele_to_sync()
		 * lowers it again, so a sample-only hold would leak a
		 * count and stall txg quiescing. The ad-hoc tx below
		 * performs its own properly paired hold via
		 * dmu_tx_assign().
		 */
		dp = dmu_objset_pool(os);
		{
			tx_state_t *txp = &dp->dp_tx;
			tx_cpu_t *tc = &txp->tx_cpu[CPU_SEQID_UNSTABLE];

			mutex_enter(&tc->tc_open_lock);
			txg = txp->tx_open_txg;
			mutex_exit(&tc->tc_open_lock);
		}
	}
	if (tx == NULL) {
		atx = dmu_tx_create(os);
		zfs_events_txhold(os, atx);
		VERIFY0(dmu_tx_assign(atx, DMU_TX_WAIT));
	}
	/*
	 * Serialize ring-buffer mutation. Concurrent VFS writers would
	 * otherwise corrupt the shared bof/eof header and interleave
	 * records, exactly as spa_history is guarded by
	 * spa_history_lock.
	 */
	mutex_enter(lockp);

	/* Use the caller's cached object id, or look it up once */
	obj = *objp;
	if (obj == 0) {
		err = zap_lookup(os, MASTER_NODE_OBJ,
		    ZFS_EVENTS_ZAP_NAME, sizeof (uint64_t), 1, &obj);
		if (err == ENOENT) {
			spa_t *spa = dmu_objset_spa(os);

			/*
			 * Check if the events feature is enabled
			 * on the pool. If not, silently skip event
			 * logging.
			 */
			if (!spa_feature_is_enabled(spa,
			    SPA_FEATURE_EVENTS)) {
				mutex_exit(lockp);
				if (owned)
					dmu_tx_commit(atx);
				return;
			}

			err = zfs_events_create_obj(os, atx,
			    events_size, &obj);
			if (err != 0) {
				mutex_exit(lockp);
				if (owned)
					dmu_tx_commit(atx);
				return;
			}

			/* Add the object to the master node ZAP */
			err = zap_add(os, MASTER_NODE_OBJ,
			    ZFS_EVENTS_ZAP_NAME, sizeof (uint64_t), 1,
			    &obj, atx);
			if (err != 0) {
				(void) dmu_object_free(os, obj, atx);
				mutex_exit(lockp);
				if (owned)
					dmu_tx_commit(atx);
				return;
			}


			/*
			 * Activate the events feature on first use.
			 * dsl_dataset_activate_feature() requires a
			 * syncing transaction, so defer it to this
			 * txg's sync pass (see
			 * zfs_events_feature_sync).
			 *
			 * dsl_sync_task_nowait() queues on the passed
			 * tx's txg and runs at its sync, so the tx must
			 * still be uncommitted when it runs: only the
			 * CALLER's tx qualifies. With an ad-hoc tx
			 * (owned), dispatch a taskq work item that opens
			 * its own transaction and activates there -
			 * otherwise a dataset whose very first event is
			 * an IO record would create the log without
			 * ever activating the feature, breaking the
			 * refcount/destroy symmetry. (Userspace builds
			 * keep the old defer-to-caller-tx behavior.)
			 */
#if defined(_KERNEL)
			if (owned) {
				zfs_events_activate_arg_t *arg =
				    kmem_alloc(sizeof (*arg), KM_SLEEP);
				arg->aoa_os = os;
				arg->aoa_ds = dmu_objset_ds(os);
				(void) taskq_dispatch(system_taskq,
				    zfs_events_activate_task,
				    arg, TQ_SLEEP);
			} else
#endif
			{
				dsl_sync_task_nowait(
				    dmu_objset_pool(os),
				    zfs_events_feature_sync,
				    dmu_objset_ds(os), atx);
			}
		} else if (err != 0) {
			/* Some other error, bail out */
			mutex_exit(lockp);
			if (owned)
				dmu_tx_commit(atx);
			return;
		}
		*objp = obj;
	}

	/* Add transaction group and timestamp */
	fnvlist_add_uint64(nvl, ZFS_EV_TXG, txg);
	fnvlist_add_uint64(nvl, ZFS_EV_TIME, gethrtime());

	/* Pack the nvlist */
	VERIFY0(nvlist_pack(nvl, &packed, &packed_len, NV_ENCODE_NATIVE,
	    KM_SLEEP));

	/* Get the event log header from bonus buffer */
	err = dmu_bonus_hold(os, obj, FTAG, &dbp);
	if (err != 0) {
		mutex_exit(lockp);
		fnvlist_pack_free(packed, packed_len);
		if (owned)
			dmu_tx_commit(atx);
		return;
	}

	zep = dbp->db_data;
	dmu_buf_will_dirty(dbp, atx);

	/*
	 * Append length + record as one atomic write. Splitting them into
	 * two zfs_events_write() calls would leave a header without a
	 * record in the log if the second call failed, and a later read
	 * would consume the next record's header as this one's payload.
	 */
	uint64_t total = sizeof (le_len) + packed_len;
	char *rec = kmem_alloc(total, KM_SLEEP);

	le_len = LE_64((uint64_t)packed_len);
	memcpy(rec, &le_len, sizeof (le_len));
	memcpy(rec + sizeof (le_len), packed, packed_len);

	err = zfs_events_write(os, obj, rec, total, zep, atx);
	if (err != 0) {
		/*
		 * The event could not be appended (the write path rolls
		 * back the ring header on failure). Surface it rather
		 * than dropping records silently.
		 */
		char osname[ZFS_MAX_DATASET_NAME_LEN];

		dmu_objset_name(os, osname);
		cmn_err(CE_WARN, "failed to append event to the log of "
		    "'%s': %d", osname, err);
	}

	kmem_free(rec, total);

	dmu_buf_rele(dbp, FTAG);
	mutex_exit(lockp);
	fnvlist_pack_free(packed, packed_len);
	if (owned)
		dmu_tx_commit(atx);
}

/*
 * Log a file/directory creation event.
 */
void
zfs_events_log_create(objset_t *os, dmu_tx_t *tx,
    uint64_t object, uint64_t parent, const char *name, uint64_t mode,
    uint64_t uid, uint64_t gid, uint64_t events_size, uint64_t *objp,
    kmutex_t *lockp)
{
	nvlist_t *nvl;

	nvl = fnvlist_alloc();
	fnvlist_add_uint16(nvl, ZFS_EV_OP, ZFS_EV_CREATE);
	fnvlist_add_uint64(nvl, ZFS_EV_OBJECT, object);
	fnvlist_add_uint64(nvl, ZFS_EV_PARENT, parent);
	fnvlist_add_string(nvl, ZFS_EV_NAME, name);
	fnvlist_add_uint64(nvl, ZFS_EV_MODE, mode);
	fnvlist_add_uint64(nvl, ZFS_EV_UID, uid);
	fnvlist_add_uint64(nvl, ZFS_EV_GID, gid);

	zfs_events_log_event(os, tx, nvl, events_size, objp, lockp,
	    dmu_tx_get_txg(tx));
	fnvlist_free(nvl);
}

/*
 * Log a file/directory removal event.
 */
void
zfs_events_log_remove(objset_t *os, dmu_tx_t *tx,
    uint64_t object, uint64_t parent, const char *name,
    uint64_t events_size, uint64_t *objp, kmutex_t *lockp)
{
	nvlist_t *nvl;

	nvl = fnvlist_alloc();
	fnvlist_add_uint16(nvl, ZFS_EV_OP, ZFS_EV_REMOVE);
	fnvlist_add_uint64(nvl, ZFS_EV_OBJECT, object);
	fnvlist_add_uint64(nvl, ZFS_EV_PARENT, parent);
	fnvlist_add_string(nvl, ZFS_EV_NAME, name);

	zfs_events_log_event(os, tx, nvl, events_size, objp, lockp,
	    dmu_tx_get_txg(tx));
	fnvlist_free(nvl);
}

/*
 * Log a rename event.
 */
void
zfs_events_log_rename(objset_t *os, dmu_tx_t *tx,
    uint64_t object, uint64_t old_parent, const char *old_name,
    uint64_t new_parent, const char *new_name, uint64_t events_size,
    uint64_t *objp, kmutex_t *lockp)
{
	nvlist_t *nvl;

	nvl = fnvlist_alloc();
	fnvlist_add_uint16(nvl, ZFS_EV_OP, ZFS_EV_RENAME);
	fnvlist_add_uint64(nvl, ZFS_EV_OBJECT, object);
	fnvlist_add_uint64(nvl, ZFS_EV_OLD_PARENT, old_parent);
	fnvlist_add_string(nvl, ZFS_EV_OLD_NAME, old_name);
	fnvlist_add_uint64(nvl, ZFS_EV_PARENT, new_parent);
	fnvlist_add_string(nvl, ZFS_EV_NAME, new_name);

	zfs_events_log_event(os, tx, nvl, events_size, objp, lockp,
	    dmu_tx_get_txg(tx));
	fnvlist_free(nvl);
}

/*
 * Log a hard link creation event.
 */
void
zfs_events_log_link(objset_t *os, dmu_tx_t *tx,
    uint64_t object, uint64_t parent, const char *name,
    uint64_t events_size, uint64_t *objp, kmutex_t *lockp)
{
	nvlist_t *nvl;

	nvl = fnvlist_alloc();
	fnvlist_add_uint16(nvl, ZFS_EV_OP, ZFS_EV_LINK);
	fnvlist_add_uint64(nvl, ZFS_EV_OBJECT, object);
	fnvlist_add_uint64(nvl, ZFS_EV_PARENT, parent);
	fnvlist_add_string(nvl, ZFS_EV_NAME, name);

	zfs_events_log_event(os, tx, nvl, events_size, objp, lockp,
	    dmu_tx_get_txg(tx));
	fnvlist_free(nvl);
}

/*
 * Log a symlink creation event.
 */
void
zfs_events_log_symlink(objset_t *os, dmu_tx_t *tx,
    uint64_t object, uint64_t parent, const char *name, const char *target,
    uint64_t events_size, uint64_t *objp, kmutex_t *lockp)
{
	nvlist_t *nvl;

	nvl = fnvlist_alloc();
	fnvlist_add_uint16(nvl, ZFS_EV_OP, ZFS_EV_SYMLINK);
	fnvlist_add_uint64(nvl, ZFS_EV_OBJECT, object);
	fnvlist_add_uint64(nvl, ZFS_EV_PARENT, parent);
	fnvlist_add_string(nvl, ZFS_EV_NAME, name);
	fnvlist_add_string(nvl, ZFS_EV_TARGET, target);

	zfs_events_log_event(os, tx, nvl, events_size, objp, lockp,
	    dmu_tx_get_txg(tx));
	fnvlist_free(nvl);
}

/*
 * Log a file truncation event.
 */
void
zfs_events_log_truncate(objset_t *os, dmu_tx_t *tx,
    uint64_t object, uint64_t old_size, uint64_t new_size,
    uint64_t events_size, uint64_t *objp, kmutex_t *lockp)
{
	nvlist_t *nvl;

	nvl = fnvlist_alloc();
	fnvlist_add_uint16(nvl, ZFS_EV_OP, ZFS_EV_TRUNCATE);
	fnvlist_add_uint64(nvl, ZFS_EV_OBJECT, object);
	fnvlist_add_uint64(nvl, ZFS_EV_OLD_SIZE, old_size);
	fnvlist_add_uint64(nvl, ZFS_EV_NEW_SIZE, new_size);

	zfs_events_log_event(os, tx, nvl, events_size, objp, lockp,
	    dmu_tx_get_txg(tx));
	fnvlist_free(nvl);
}

/*
 * Log an attribute change event.
 */
void
zfs_events_log_setattr(objset_t *os, dmu_tx_t *tx,
    uint64_t object, uint64_t attr_mask, uint64_t events_size,
    uint64_t *objp, kmutex_t *lockp)
{
	nvlist_t *nvl;

	nvl = fnvlist_alloc();
	fnvlist_add_uint16(nvl, ZFS_EV_OP, ZFS_EV_SETATTR);
	fnvlist_add_uint64(nvl, ZFS_EV_OBJECT, object);
	fnvlist_add_uint64(nvl, ZFS_EV_ATTRS, attr_mask);

	zfs_events_log_event(os, tx, nvl, events_size, objp, lockp,
	    dmu_tx_get_txg(tx));
	fnvlist_free(nvl);
}

/*
 * Log a data write event. The write path loops over chunks, each with
 * its own transaction; callers pass the last committed chunk's txg so
 * the record lands on the commit timeline position of the write.
 */
void
zfs_events_log_write(objset_t *os, dmu_tx_t *tx,
    uint64_t object, uint64_t offset, uint64_t bytes, const cred_t *cr,
    uint64_t events_size, uint64_t *objp, kmutex_t *lockp, uint64_t txg)
{
	nvlist_t *nvl;

	nvl = fnvlist_alloc();
	fnvlist_add_uint16(nvl, ZFS_EV_OP, ZFS_EV_WRITE);
	fnvlist_add_uint64(nvl, ZFS_EV_OBJECT, object);
	fnvlist_add_uint64(nvl, ZFS_EV_IO_OFFSET, offset);
	fnvlist_add_uint64(nvl, ZFS_EV_IO_BYTES, bytes);
	fnvlist_add_uint64(nvl, ZFS_EV_UID, crgetuid((cred_t *)(uintptr_t)cr));
	fnvlist_add_uint64(nvl, ZFS_EV_GID, crgetgid((cred_t *)(uintptr_t)cr));

	zfs_events_log_event(os, tx, nvl, events_size, objp, lockp, txg);
	fnvlist_free(nvl);
}

/*
 * Log a data read event. Reads carry no transaction, so the record is
 * attributed to the pool's open txg at read time (sampled inside
 * zfs_events_log_event): a commit-timeline anchor, not a transaction
 * that carried the read.
 */
void
zfs_events_log_read(objset_t *os, uint64_t object,
    uint64_t offset, uint64_t bytes, const cred_t *cr,
    uint64_t events_size, uint64_t *objp, kmutex_t *lockp)
{
	nvlist_t *nvl;

	nvl = fnvlist_alloc();
	fnvlist_add_uint16(nvl, ZFS_EV_OP, ZFS_EV_READ);
	fnvlist_add_uint64(nvl, ZFS_EV_OBJECT, object);
	fnvlist_add_uint64(nvl, ZFS_EV_IO_OFFSET, offset);
	fnvlist_add_uint64(nvl, ZFS_EV_IO_BYTES, bytes);
	fnvlist_add_uint64(nvl, ZFS_EV_UID, crgetuid((cred_t *)(uintptr_t)cr));
	fnvlist_add_uint64(nvl, ZFS_EV_GID, crgetgid((cred_t *)(uintptr_t)cr));

	zfs_events_log_event(os, NULL, nvl, events_size, objp, lockp, 0);
	fnvlist_free(nvl);
}

/*
 * Emit a merged IO window record.
 *
 * Called with the fence state sampled (start != 0) and z_lock held;
 * drops z_lock around the emission itself, which may sleep (ad-hoc
 * transaction assignment) and may nest back into z_lock through the
 * account path's own mutex - so it must not be entered holding the
 * lock, and it re-checks the pending state after re-acquiring it in
 * case another thread opened a new window meanwhile.
 *
 * The txg label: callers with a live transaction pass its txg (same-tx
 * ordering with a following op record is then exact); transaction-less
 * callers pass 0 and the record is attributed to the pool's open txg.
 * Merged windows may span multiple callers; uid/gid are those of the
 * caller that triggered the emission (documented semantics - per-caller
 * attribution would need per-uid pending state).
 */
#if defined(_KERNEL)
static void
zfs_events_io_emit(znode_t *zp, objset_t *os, boolean_t is_write,
    hrtime_t start, uint64_t offset, uint64_t bytes, const cred_t *cr,
    uint64_t events_size, uint64_t *objp, kmutex_t *lockp, uint64_t txg,
    kmutex_t *zlk)
{
	uint16_t op = is_write ? ZFS_EV_WRITE : ZFS_EV_READ;
	nvlist_t *nvl;

	/* zlk may be NULL when the caller already dropped z_lock. */
	if (zlk != NULL)
		mutex_exit(zlk);

	nvl = fnvlist_alloc();
	fnvlist_add_uint16(nvl, ZFS_EV_OP, op);
	fnvlist_add_uint64(nvl, ZFS_EV_OBJECT, zp->z_id);
	fnvlist_add_uint64(nvl, ZFS_EV_IO_OFFSET, offset);
	fnvlist_add_uint64(nvl, ZFS_EV_IO_BYTES, bytes);
	fnvlist_add_uint64(nvl, ZFS_EV_UID, crgetuid((cred_t *)(uintptr_t)cr));
	fnvlist_add_uint64(nvl, ZFS_EV_GID, crgetgid((cred_t *)(uintptr_t)cr));

	/*
	 * Deferred emission carries no transaction: the fence may merge
	 * IO from several transactions, so per-record tx attribution is
	 * not meaningful; the record is labeled with the caller's txg
	 * when one exists (same-tx ordering with op records), otherwise
	 * the pool's open txg.
	 */
	zfs_events_log_event(os, NULL, nvl, events_size, objp, lockp, txg);
	fnvlist_free(nvl);
}

/*
 * Flush a file's pending IO window early, so pending IO records precede
 * an op record (CREATE/REMOVE/RENAME/...) about to be emitted on the
 * same transaction. tx may be NULL (open-txg attribution). A no-op when
 * no window is pending.
 *
 * Scope note: Linux-only. The FreeBSD vnops flush sites are deferred;
 * pending windows there close via the fence timer or close(2) instead.
 */
void
zfs_events_io_flush(znode_t *zp, objset_t *os, dmu_tx_t *tx,
    boolean_t is_write)
{
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	uint64_t txg = 0;
	hrtime_t start;
	uint64_t offset, bytes;
	boolean_t locked;

	if (!zfsvfs->z_events || !zfsvfs->z_events_io || zfsvfs->z_replay)
		return;

	if (tx != NULL)
		txg = dmu_tx_get_txg(tx);

	/*
	 * Some callers (zfs_setattr, zfs_symlink, zfs_link) already
	 * hold z_lock across their emitter block; others (zfs_close)
	 * do not. Detect the recursive case and leave the lock held
	 * on return: the emission path never takes z_lock again.
	 */
	locked = (mutex_owner(&zp->z_lock) != curthread);
	if (locked)
		mutex_enter(&zp->z_lock);
	if (is_write) {
		start = zp->z_ev_io_wstart;
		if (start == 0) {
			if (locked)
				mutex_exit(&zp->z_lock);
			return;
		}
		zp->z_ev_io_wstart = 0;
		offset = zp->z_ev_io_wpend_off;
		bytes = zp->z_ev_io_wpend_bytes;
	} else {
		start = zp->z_ev_io_rstart;
		if (start == 0) {
			if (locked)
				mutex_exit(&zp->z_lock);
			return;
		}
		zp->z_ev_io_rstart = 0;
		offset = zp->z_ev_io_rpend_off;
		bytes = zp->z_ev_io_rpend_bytes;
	}
	if (locked) {
		/*
		 * We acquired the lock ourselves (close(2)): hand it to
		 * the emitter, which drops it across the (sleeping)
		 * emission and returns with it unlocked.
		 */
		zfs_events_io_emit(zp, os, is_write, start, offset, bytes,
		    kcred, zfsvfs->z_events_size, &zfsvfs->z_events_obj,
		    &zfsvfs->z_events_lock, txg, &zp->z_lock);
		return;
	}
	/*
	 * Recursive case: the caller owns z_lock and must still own it
	 * when we return, but the emission can sleep (ad-hoc tx assign),
	 * so drop it across the emission and re-acquire after. emit()'s
	 * own drop is skipped via zlk == NULL.
	 */
	mutex_exit(&zp->z_lock);
	zfs_events_io_emit(zp, os, is_write, start, offset, bytes,
	    kcred, zfsvfs->z_events_size, &zfsvfs->z_events_obj,
	    &zfsvfs->z_events_lock, txg, NULL);
	mutex_enter(&zp->z_lock);
}

/*
 * Feed a completed read/write into the per-file TIME fence.
 *
 * Algorithm (per file, per direction, state protected by z_lock):
 *
 *   window == 0 (fence disabled):
 *	Every IO emits immediately; no pending state is touched.
 *
 *   window > 0:
 *	IO landing within `window` of a pending window's open time is
 *	absorbed: bytes are summed, the offset stays at the window's
 *	first IO offset. IO landing past the window closes it (emits one
 *	merged record) and opens a new window with the current IO.
 *
 * Bytes are never dropped - they are only coalesced. Absorbed windows
 * are also emitted by zfs_events_io_flush() before op records and at
 * close(2); turning events/events_io off discards pending windows
 * (accepted loss).
 *
 * txg: callers with a live transaction pass dmu_tx_get_txg(tx);
 * transaction-less callers pass 0 (= open-txg attribution inside).
 */
void
zfs_events_io_account(struct znode *zp, boolean_t is_write,
    uint64_t offset, uint64_t bytes, const cred_t *cr, uint64_t txg)
{
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	uint64_t window_ns;
	hrtime_t now;
	hrtime_t start = 0;
	uint64_t pend_off = 0, pend_bytes = 0;
	kmutex_t *zlk = &zp->z_lock;

	/*
	 * Hot-path gate: two loads, no locks, when IO events are off.
	 */
	if (!zfsvfs->z_events || !zfsvfs->z_events_io)
		return;

	/* No IO events during ZIL replay. */
	if (zfsvfs->z_replay)
		return;

	ASSERT3U(bytes, !=, 0);

	/*
	 * The window is stored in milliseconds and converted to
	 * nanoseconds here, at the single use site.
	 */
	window_ns = (hrtime_t)zfsvfs->z_events_io_window *
	    (NANOSEC / MILLISEC);
	now = gethrtime();

	mutex_enter(zlk);

	if (is_write) {
		start = zp->z_ev_io_wstart;
		if (start != 0) {
			pend_off = zp->z_ev_io_wpend_off;
			pend_bytes = zp->z_ev_io_wpend_bytes;
		}
	} else {
		start = zp->z_ev_io_rstart;
		if (start != 0) {
			pend_off = zp->z_ev_io_rpend_off;
			pend_bytes = zp->z_ev_io_rpend_bytes;
		}
	}

	if (window_ns == 0) {
		/*
		 * Fence disabled: emit every syscall immediately. The
		 * emission can sleep (ad-hoc tx assign), so z_lock is
		 * released first - no caller state is touched, and the
		 * ring lock (z_events_lock) nests above nothing here.
		 */
		mutex_exit(zlk);
		if (is_write) {
			zfs_events_log_write(zfsvfs->z_os,
			    NULL, zp->z_id, offset, bytes, cr,
			    zfsvfs->z_events_size, &zfsvfs->z_events_obj,
			    &zfsvfs->z_events_lock, txg);
		} else {
			zfs_events_log_read(zfsvfs->z_os, zp->z_id,
			    offset, bytes, cr, zfsvfs->z_events_size,
			    &zfsvfs->z_events_obj, &zfsvfs->z_events_lock);
		}
		return;
	}

	if (start != 0 && now - start < window_ns) {
		/*
		 * Absorb into the open window: bytes summed, offset
		 * stays at the window's first IO.
		 */
		if (is_write)
			zp->z_ev_io_wpend_bytes = pend_bytes + bytes;
		else
			zp->z_ev_io_rpend_bytes = pend_bytes + bytes;
		mutex_exit(zlk);
		return;
	}

	if (start != 0) {
		/*
		 * Window expired: emit the merged record, then open the
		 * new window below. zfs_events_io_emit drops z_lock
		 * across the (sleeping) emission and re-acquires it;
		 * re-check whether another thread opened a window in
		 * between so state stays consistent.
		 */
		zfs_events_io_emit(zp, zfsvfs->z_os, is_write, start,
		    pend_off, pend_bytes, cr, zfsvfs->z_events_size,
		    &zfsvfs->z_events_obj, &zfsvfs->z_events_lock, txg, zlk);

		mutex_enter(zlk);
		if (is_write) {
			if (zp->z_ev_io_wstart == 0) {
				zp->z_ev_io_wstart = now;
				zp->z_ev_io_wpend_off = offset;
				zp->z_ev_io_wpend_bytes = bytes;
			} else {
				/*
				 * Another thread already opened a new
				 * window; absorb into it rather than
				 * resetting its start time.
				 */
				zp->z_ev_io_wpend_bytes += bytes;
			}
		} else {
			if (zp->z_ev_io_rstart == 0) {
				zp->z_ev_io_rstart = now;
				zp->z_ev_io_rpend_off = offset;
				zp->z_ev_io_rpend_bytes = bytes;
			} else {
				zp->z_ev_io_rpend_bytes += bytes;
			}
		}
		mutex_exit(zlk);
		return;
	}

	/* No pending window: open one with this IO. */
	if (is_write) {
		zp->z_ev_io_wstart = now;
		zp->z_ev_io_wpend_off = offset;
		zp->z_ev_io_wpend_bytes = bytes;
	} else {
		zp->z_ev_io_rstart = now;
		zp->z_ev_io_rpend_off = offset;
		zp->z_ev_io_rpend_bytes = bytes;
	}
	mutex_exit(zlk);
}

#endif	/* _KERNEL */

/*
 * Read events from the event log.
 *
 * offp: in/out - logical offset to start reading, updated on return
 * lenp: in/out - buffer length on input, bytes read on return
 * buf: buffer to read events into
 */
int
zfs_events_get(objset_t *os, kmutex_t *lockp, uint64_t *offp,
    uint64_t *lenp, char *buf)
{
	dmu_buf_t *dbp;
	uint64_t obj;
	uint64_t read_len, phys_read_off, phys_eof;
	uint64_t leftover = 0;
	zfs_events_phys_t *zep;
	int err;

	/* Look up the event log object */
	err = zap_lookup(os, MASTER_NODE_OBJ, ZFS_EVENTS_ZAP_NAME,
	    sizeof (uint64_t), 1, &obj);
	if (err != 0)
		return (SET_ERROR(ENOENT));

	err = dmu_bonus_hold(os, obj, FTAG, &dbp);
	if (err != 0)
		return (err);

	/*
	 * Serialize against concurrent appenders (and the clear path):
	 * without this, an append that wraps the ring between the
	 * header sample and the dmu_read()s below overwrites exactly
	 * the region being read and the caller parses garbage record
	 * headers. The ring lock nests above nothing on the read
	 * path, so holding it across the (blocking) reads is safe.
	 */
	mutex_enter(lockp);

	zep = dbp->db_data;

	/* Validate and clamp the read offset */
	if (*offp < zep->zep_bof)
		*offp = zep->zep_bof;
	else if (*offp > zep->zep_eof)
		*offp = zep->zep_eof;

	/* Calculate how much we can read */
	read_len = MIN(*lenp, zep->zep_eof - *offp);

	if (read_len == 0) {
		mutex_exit(lockp);
		dmu_buf_rele(dbp, FTAG);
		*lenp = 0;
		return (0);
	}

	/* Convert to physical offset and handle wrap-around */
	phys_read_off = zfs_events_log_to_phys(*offp, zep);
	phys_eof = zfs_events_log_to_phys(zep->zep_eof, zep);

	if (phys_read_off < phys_eof) {
		/* No wrap, simple read */
		err = dmu_read(os, obj, phys_read_off, read_len, buf,
		    DMU_READ_PREFETCH);
	} else {
		/* Handle wrap-around read */
		uint64_t first = zep->zep_phys_max_off - phys_read_off;
		if (first > read_len)
			first = read_len;

		err = dmu_read(os, obj, phys_read_off, first, buf,
		    DMU_READ_PREFETCH);
		if (err == 0 && read_len > first) {
			leftover = read_len - first;
			err = dmu_read(os, obj, 0, leftover, buf + first,
			    DMU_READ_PREFETCH);
		}
	}

	mutex_exit(lockp);
	dmu_buf_rele(dbp, FTAG);

	if (err == 0) {
		*offp += read_len;
		*lenp = read_len;
	}

	return (err);
}

/*
 * Return the count of records overwritten due to ring wraparound.
 * Returns ENOENT if the dataset has no event log.
 */
int
zfs_events_get_lost(objset_t *os, uint64_t *lostp)
{
	dmu_buf_t *dbp;
	zfs_events_phys_t *zep;
	uint64_t obj;
	int err;

	err = zap_lookup(os, MASTER_NODE_OBJ, ZFS_EVENTS_ZAP_NAME,
	    sizeof (uint64_t), 1, &obj);
	if (err != 0)
		return (SET_ERROR(ENOENT));

	err = dmu_bonus_hold(os, obj, FTAG, &dbp);
	if (err != 0)
		return (err);

	zep = dbp->db_data;
	*lostp = zep->zep_records_lost;
	dmu_buf_rele(dbp, FTAG);
	return (0);
}

/*
 * Return the format version of the event ring on-disk structure.
 * Returns ENOENT if the dataset has no event log.
 */
int
zfs_events_get_schema_version(objset_t *os, uint64_t *verp)
{
	dmu_buf_t *dbp;
	zfs_events_phys_t *zep;
	uint64_t obj;
	int err;

	err = zap_lookup(os, MASTER_NODE_OBJ, ZFS_EVENTS_ZAP_NAME,
	    sizeof (uint64_t), 1, &obj);
	if (err != 0)
		return (SET_ERROR(ENOENT));

	err = dmu_bonus_hold(os, obj, FTAG, &dbp);
	if (err != 0)
		return (err);

	zep = dbp->db_data;
	*verp = zep->zep_version;
	dmu_buf_rele(dbp, FTAG);
	return (0);
}

#if defined(_KERNEL)
EXPORT_SYMBOL(zfs_events_create_obj);
EXPORT_SYMBOL(zfs_events_txhold);
EXPORT_SYMBOL(zfs_events_get_lost);
EXPORT_SYMBOL(zfs_events_get_schema_version);
EXPORT_SYMBOL(zfs_events_destroy_obj);
EXPORT_SYMBOL(zfs_events_log_create);
EXPORT_SYMBOL(zfs_events_log_remove);
EXPORT_SYMBOL(zfs_events_log_rename);
EXPORT_SYMBOL(zfs_events_log_link);
EXPORT_SYMBOL(zfs_events_log_symlink);
EXPORT_SYMBOL(zfs_events_log_truncate);
EXPORT_SYMBOL(zfs_events_log_setattr);
EXPORT_SYMBOL(zfs_events_get);
#endif
