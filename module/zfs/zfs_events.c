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
 * Read the little-endian record-length prefix at p. Because records are
 * variable length the prefix sits at arbitrary offsets inside the ring,
 * so a direct uint64_t load would be unaligned (and alias-unsafe); copy
 * the bytes out first.
 */
static uint64_t
zfs_events_record_reclen(const void *p)
{
	uint64_t len;

	memcpy(&len, p, sizeof (len));
	return (LE_64(len));
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

	reclen = zfs_events_record_reclen(buf);

	/*
	 * A zero or oversized length means the BOF region doesn't hold a
	 * valid record header (unwritten space after a full wrap, or
	 * corruption). Treat everything up to EOF as free space rather
	 * than spinning on invalid data. The bounds are compared in
	 * subtraction form: reclen + sizeof (reclen) can wrap for a
	 * corrupt reclen near UINT64_MAX and defeat an addition-form
	 * check.
	 */
	if (reclen == 0 ||
	    zep->zep_eof - zep->zep_bof < sizeof (reclen) ||
	    reclen > zep->zep_eof - zep->zep_bof - sizeof (reclen)) {
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

	/*
	 * Stamp the ring GUID: the identity of THIS log lifetime. It
	 * survives zfs_events_clear(); a fresh value is drawn on every
	 * create, so a destroy/recreate or zfs receive swap is visible
	 * to consumers as a ring_guid change.
	 */
	for (int i = 0; i < 8; i++) {
		uint64_t g = 1;

		(void) random_get_pseudo_bytes((void *)&g, sizeof (g));
		if (g != 0) {
			zep->zep_guid = g;
			break;
		}
	}

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
 * Seed the cached event-log object id from the master-node ZAP entry.
 *
 * zfsvfs->z_events_obj is otherwise only written when
 * zfs_events_get_obj() resolves the ring, so without an initial seed the
 * first emitting transaction after a mount (or after a reopen, which
 * resets the cache) finds a ZAP entry its own zfs_events_txhold() DID see
 * but an empty cache, takes the skip-on-mismatch path in
 * zfs_events_get_obj() and silently drops the record. Callers invoke this
 * from the events property callback right after the events properties are
 * (re)read, i.e. at mount and on resume.
 *
 * Best-effort: ENOENT (no ring yet) and any other lookup error leave the
 * cache empty, and the ring is then created/claimed lazily by the first
 * emitter exactly as before. os must be a valid dataset objset.
 */
void
zfs_events_seed_obj(objset_t *os, uint64_t *objp, kmutex_t *lockp)
{
	uint64_t obj;

	mutex_enter(lockp);
	if (*objp == 0) {
		if (zap_lookup(os, MASTER_NODE_OBJ, ZFS_EVENTS_ZAP_NAME,
		    sizeof (uint64_t), 1, &obj) == 0)
			*objp = obj;
	}
	mutex_exit(lockp);
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
	 * symmetrically when the dataset goes away. The in-memory
	 * ds_feature flag must be set alongside (as dsl_crypt.c and
	 * dmu_recv.c do) so dsl_dataset_feature_is_active() - and the
	 * deactivate guard in zfs_events_destroy_obj() - see it
	 * without a dataset reload.
	 */
	if (dsl_dataset_feature_is_active(ds, SPA_FEATURE_EVENTS))
		return;

	dsl_dataset_activate_feature(ds->ds_object, SPA_FEATURE_EVENTS,
	    (void *)B_TRUE, tx);
	ds->ds_feature[SPA_FEATURE_EVENTS] = (void *)B_TRUE;
}

static uint64_t zfs_events_get_obj(objset_t *os, dmu_tx_t *tx,
    uint64_t events_size, uint64_t *objp, kmutex_t *lockp);

#if defined(_KERNEL)
/*
 * Deferred IO-record emission (window == 0).
 *
 * Emitting a record inline in read/write syscall context costs a full
 * ad-hoc DMU transaction per record whose DMU_TX_WAIT assignment can
 * throttle-sleep for up to a txg - a per-syscall stall class. Instead
 * the syscall enqueues a fixed-size record (no allocation failure can
 * drop it silently: fallback is the old inline emission) and wakes
 * the dataset's worker, which drains the whole queue in ONE
 * transaction: N records per ring lock acquisition, one bonus dirty
 * and one ring append per batch.
 *
 * Queue state lives on the zfsvfs under z_events_lock. The worker
 * swaps out the pending list, drops the lock, and does all blocking
 * work unlocked; enqueuers never block on the ring.
 *
 * Bound: ZFS_EVQ_MAX entries. Past the bound (or when the entry
 * cannot be queued for any other reason) the caller falls back to
 * inline emission, so queue overload never silently drops records;
 * records the drain worker cannot commit are counted lost on the
 * ring header (zep_records_lost), mirroring the ring-wrap semantics.
 */
#define	ZFS_EVQ_MAX	4096

/*
 * Upper bound on one packed record: one uint16 + eight uint64
 * native-encoded pairs (incl. the optional principal) measure under
 * 320 bytes; 512 leaves headroom for future fields. Keep this in
 * sync with the drain-time runtime guard, which counts a too-large
 * record lost instead of panicking.
 */
#define	ZFS_EVQ_REC_MAX	512

typedef struct zfs_events_qent zfs_events_qent_t;

static kmem_cache_t *zfs_events_qent_cache;

static void zfs_events_drain_task(void *arg);

/*
 * Per-principal registration table (ZFS_EV_PRINCIPAL).
 *
 * An application (zeta-object: one process per S3 gateway) registers an
 * opaque uint64 tag for its thread group; every event record the
 * thread group triggers carries the tag, letting consumers attribute
 * records to an application-level principal (e.g. an S3 access key)
 * that the kernel cannot know.
 *
 * Evidentiary weight: the tag is a CLAIM supplied by userspace, not
 * evidence - any process may register any value. The record's uid/gid
 * remain the kernel-verified attribution; the principal is a
 * correlation key.
 *
 * Capture discipline: zfs_events_principal_get() must be called ONLY
 * from syscall context. Deferred emission (taskq drain, zfs_inactive)
 * runs in kernel-worker context where the calling process is not the
 * writer, so deferred records capture the tag into their queue entry
 * at defer time, exactly like uid/gid. A tag absent at defer time is
 * never fabricated later.
 *
 * Generation counter: tgids can be reused. Each registration bumps
 * the entry's generation, so a stale consumer of a previous
 * registration can always distinguish old from new.
 */
#define	ZFS_PRINCIPAL_MAX	64

/*
 * Thread-group id of the caller. getpid() is the THREAD id on both
 * platforms (Linux current->pid / FreeBSD td_tid), but registrations
 * are per application process (a multi-threaded gateway registers
 * once and every request thread must match), so key the table on the
 * thread-group leader id.
 */
#if defined(_KERNEL)
#if defined(__linux__)
#define	ZFS_EV_TGID()	(current->tgid)
#else
#define	ZFS_EV_TGID()	(curproc->p_pid)
#endif
#else
#define	ZFS_EV_TGID()	((pid_t)getpid())	/* libzpool: plain pid */
#endif

typedef struct zfs_events_principal {
	pid_t		zp_tgid;	/* 0 = slot free */
	uint64_t	zp_tag;
	uint64_t	zp_gen;		/* bumps on every register */
} zfs_events_principal_t;

static kmutex_t		zfs_events_principal_lock;
static zfs_events_principal_t	zfs_events_principals[ZFS_PRINCIPAL_MAX];

/*
 * Leaf-lock ordering: zfs_events_principal_lock guards only this
 * table and is never held across any other lock acquisition
 * (including z_events_lock); capture sites take it and release it
 * before touching dataset state.
 */
boolean_t
zfs_events_principal_get(uint64_t *tagp)
{
	pid_t tgid = ZFS_EV_TGID();
	boolean_t found = B_FALSE;
	int i;

	mutex_enter(&zfs_events_principal_lock);
	for (i = 0; i < ZFS_PRINCIPAL_MAX; i++) {
		if (zfs_events_principals[i].zp_tgid == tgid) {
			*tagp = zfs_events_principals[i].zp_tag;
			found = B_TRUE;
			break;
		}
	}
	mutex_exit(&zfs_events_principal_lock);
	return (found);
}

void
zfs_events_qent_init(void)
{
	zfs_events_qent_cache = kmem_cache_create(
	    "zfs_events_qent", sizeof (zfs_events_qent_t), 0,
	    NULL, NULL, NULL, NULL, NULL, 0);
	mutex_init(&zfs_events_principal_lock, NULL, MUTEX_DEFAULT, NULL);
	memset(zfs_events_principals, 0, sizeof (zfs_events_principals));
}

void
zfs_events_qent_fini(void)
{
	mutex_destroy(&zfs_events_principal_lock);
	kmem_cache_destroy(zfs_events_qent_cache);
	zfs_events_qent_cache = NULL;
}

/*
 * Register (have=B_TRUE) or deregister (have=B_FALSE) the calling
 * thread group's principal tag. Returns B_TRUE and the entry's
 * generation via *genp on success; B_FALSE when the table is full
 * (register) or the caller has no registration (deregister).
 */
boolean_t
zfs_events_principal_set(boolean_t have, uint64_t tag, uint64_t *genp)
{
	pid_t tgid = ZFS_EV_TGID();
	int i, free_slot = -1;

	mutex_enter(&zfs_events_principal_lock);
	for (i = 0; i < ZFS_PRINCIPAL_MAX; i++) {
		if (zfs_events_principals[i].zp_tgid == tgid)
			break;
		if (zfs_events_principals[i].zp_tgid == 0 &&
		    free_slot == -1)
			free_slot = i;
	}
	if (!have) {
		if (i == ZFS_PRINCIPAL_MAX) {
			mutex_exit(&zfs_events_principal_lock);
			return (B_FALSE);
		}
		memset(&zfs_events_principals[i], 0,
		    sizeof (zfs_events_principals[i]));
		mutex_exit(&zfs_events_principal_lock);
		return (B_TRUE);
	}
	if (i == ZFS_PRINCIPAL_MAX) {
		if (free_slot == -1) {
			mutex_exit(&zfs_events_principal_lock);
			return (B_FALSE);
		}
		i = free_slot;
		zfs_events_principals[i].zp_tgid = tgid;
		zfs_events_principals[i].zp_gen = 0;
	}
	zfs_events_principals[i].zp_tag = tag;
	zfs_events_principals[i].zp_gen++;
	*genp = zfs_events_principals[i].zp_gen;
	mutex_exit(&zfs_events_principal_lock);
	return (B_TRUE);
}

/*
 * Enqueue one record for deferred emission. Returns B_TRUE if queued,
 * B_FALSE if the caller must emit inline (cache alloc failure, queue
 * full, shutdown, or dispatch failure): queue overload falls back to
 * inline emission, so nothing is lost by the queue itself.
 */
static boolean_t
zfs_events_io_defer(zfsvfs_t *zfsvfs, uint16_t op, uint64_t object,
    uint64_t offset, uint64_t bytes, const cred_t *cr, uint64_t txg)
{
	zfs_events_qent_t *qe;
	taskq_t *tq = NULL;
	taskqid_t tid = TASKQID_INVALID;
	boolean_t queued = B_FALSE;

	mutex_enter(&zfsvfs->z_events_lock);
	if (zfsvfs->z_evq_shutdown) {
		mutex_exit(&zfsvfs->z_events_lock);
		return (B_FALSE);
	}
	mutex_exit(&zfsvfs->z_events_lock);

	qe = kmem_cache_alloc(zfs_events_qent_cache, KM_NOSLEEP);
	if (qe == NULL)
		return (B_FALSE);

	qe->qe_op = op;
	qe->qe_object = object;
	qe->qe_offset = offset;
	qe->qe_bytes = bytes;
	qe->qe_uid = crgetuid((cred_t *)(uintptr_t)cr);
	qe->qe_gid = crgetgid((cred_t *)(uintptr_t)cr);
	qe->qe_have_principal = zfs_events_principal_get(&qe->qe_principal);
	qe->qe_txg = txg;
	qe->qe_time = gethrtime();
	if (qe->qe_txg == 0 && zfsvfs->z_os != NULL) {
		dsl_pool_t *dp = dmu_objset_pool(zfsvfs->z_os);
		tx_state_t *txp = &dp->dp_tx;
		tx_cpu_t *tc = &txp->tx_cpu[CPU_SEQID_UNSTABLE];

		mutex_enter(&tc->tc_open_lock);
		qe->qe_txg = txp->tx_open_txg;
		mutex_exit(&tc->tc_open_lock);
	}

	/*
	 * Everything below - the cap check, the lazy taskq create and
	 * the insert - runs in this one critical section, so the cap
	 * check cannot race the count increment (TOCTOU) and a count
	 * the drain worker reads is always one the enqueuers
	 * incremented under the lock. On a cap hit nothing is dropped:
	 * B_FALSE makes the caller emit inline instead.
	 */
	mutex_enter(&zfsvfs->z_events_lock);
	if (zfsvfs->z_evq_shutdown ||
	    zfsvfs->z_evq_count >= ZFS_EVQ_MAX) {
		mutex_exit(&zfsvfs->z_events_lock);
		kmem_cache_free(zfs_events_qent_cache, qe);
		return (B_FALSE);
	}

	/*
	 * The taskq is created lazily on first use. If creation fails,
	 * tell the caller to emit inline: an entry must never be
	 * stranded on the queue without a dispatch pending, or it would
	 * only be collected at unmount.
	 */
	if (!zfsvfs->z_evq_scheduled) {
		if (zfsvfs->z_evq_taskq == NULL) {
			zfsvfs->z_evq_taskq = taskq_create(
			    "zfs_events_drain", 1, minclsyspri, 1,
			    INT_MAX, TASKQ_PREPOPULATE);
		}
		tq = zfsvfs->z_evq_taskq;
		if (tq != NULL) {
			zfsvfs->z_evq_scheduled = B_TRUE;
			queued = B_TRUE;
		}
	} else {
		queued = B_TRUE;
	}

	if (queued) {
		list_insert_tail(&zfsvfs->z_evq_deferred, qe);
		zfsvfs->z_evq_count++;
	}
	mutex_exit(&zfsvfs->z_events_lock);

	if (!queued) {
		kmem_cache_free(zfs_events_qent_cache, qe);
		return (B_FALSE);
	}

	if (tq != NULL) {
		tid = taskq_dispatch(tq, zfs_events_drain_task, zfsvfs,
		    TQ_SLEEP);
		if (tid == TASKQID_INVALID) {
			/*
			 * Dispatch failed: undo the enqueue completely so
			 * the entry is never stranded (it would only be
			 * collected at unmount, unreportable). Remove
			 * it from the list, drop the count, clear the
			 * schedule flag, and tell the caller to emit
			 * inline.
			 */
			mutex_enter(&zfsvfs->z_events_lock);
			list_remove(&zfsvfs->z_evq_deferred, qe);
			zfsvfs->z_evq_count--;
			zfsvfs->z_evq_scheduled = B_FALSE;
			mutex_exit(&zfsvfs->z_events_lock);
			kmem_cache_free(zfs_events_qent_cache, qe);
			return (B_FALSE);
		}
	}

	return (B_TRUE);
}

/*
 * Account records that could not be appended to the ring, on a fresh
 * ad-hoc transaction. records_lost is the ring's contract for loss, so it
 * must be bumped even when the batch transaction that failed is already
 * aborted or committed. The tx is assigned before taking the ring lock
 * (DMU_TX_WAIT may sleep for a txg; holding the ring lock across it would
 * stall every inline emitter), matching zfs_events_log_event()'s order,
 * so no lock inversion. dmu_tx_holds_obj_bonus() keeps the will_dirty off
 * an object this tx does not hold; when there is no reachable ring
 * (os or obj is 0) the loss is simply unreportable.
 */
static void
zfs_events_account_lost(objset_t *os, uint64_t obj, uint64_t lost,
    kmutex_t *lockp)
{
	dmu_tx_t *ltx;

	if (os == NULL || obj == 0 || lost == 0)
		return;

	ltx = dmu_tx_create(os);
	zfs_events_txhold(os, ltx);
	if (dmu_tx_assign(ltx, DMU_TX_WAIT) == 0) {
		dmu_buf_t *ldb;

		mutex_enter(lockp);
		if (dmu_tx_holds_obj_bonus(ltx, obj) &&
		    dmu_bonus_hold(os, obj, FTAG, &ldb) == 0) {
			zfs_events_phys_t *lzep = ldb->db_data;

			dmu_buf_will_dirty(ldb, ltx);
			lzep->zep_records_lost += lost;
			dmu_buf_rele(ldb, FTAG);
		}
		mutex_exit(lockp);
		dmu_tx_commit(ltx);
	} else {
		dmu_tx_abort(ltx);
	}
}

/*
 * Worker: drain all deferred records in one transaction. Runs with no
 * VFS locks held; all blocking work happens with the queue lock
 * dropped. zfsvfs lifetime is guaranteed by the shutdown handshake:
 * the drain taskq is waited on and destroyed (from
 * zfs_events_drain_shutdown, called while the objset is still
 * owned) before the zfsvfs is freed, and shutdown is one-way.
 */
static void
zfs_events_drain_task(void *arg)
{
	zfsvfs_t *zfsvfs = arg;
	objset_t *os = zfsvfs->z_os;
	list_t batch;
	zfs_events_qent_t *qe;
	uint64_t lost = 0;
	uint64_t nrec = 0;
	uint64_t oversize = 0;
	size_t total = 0;
	char *buf, *p;
	dmu_buf_t *dbp;
	zfs_events_phys_t *zep;
	uint64_t obj = 0;
	dmu_tx_t *tx;
	int err;

	list_create(&batch, sizeof (zfs_events_qent_t),
	    offsetof(zfs_events_qent_t, qe_node));

	mutex_enter(&zfsvfs->z_events_lock);
	list_move_tail(&batch, &zfsvfs->z_evq_deferred);
	zfsvfs->z_evq_count = 0;
	zfsvfs->z_evq_scheduled = B_FALSE;
	mutex_exit(&zfsvfs->z_events_lock);

	nrec = 0;
	for (qe = list_head(&batch); qe != NULL; qe = list_next(&batch, qe))
		nrec++;

	if (list_is_empty(&batch))
		goto out;

	tx = dmu_tx_create(os);
	zfs_events_txhold(os, tx);
	err = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (err != 0) {
		/*
		 * The batch tx could not be assigned, so the records
		 * cannot be appended. The ring's lost counter is the
		 * contract for loss: bump it on a fresh ad-hoc tx,
		 * assigned before taking the ring lock (DMU_TX_WAIT
		 * may sleep for a txg; the same order log_event uses,
		 * so no lock inversion). A concurrent clear can
		 * still land between the failed append and this
		 * commit, but only in the safe direction: the clear
		 * resets the header first, so the stale bump either
		 * commits before it (then is reset with the cleared
		 * history) or after a still-unassigned-batch failure
		 * - a queue-delivery failure independent of the
		 * clear. Re-using the dead batch tx is not possible;
		 * a bump-after-clear here overstates loss by at most
		 * one batch and only when a clear raced a tx-assign
		 * failure in the same txg.
		 */
		dmu_tx_abort(tx);
		lost = nrec;
		obj = zfsvfs->z_events_obj;
		goto drop;
	}

	obj = zfs_events_get_obj(os, tx, zfsvfs->z_events_size,
	    &zfsvfs->z_events_obj, &zfsvfs->z_events_lock);
	if (obj == 0) {
		/*
		 * No log object usable by this transaction: either
		 * none could be created (feature raced off), or a
		 * racing tx created one this tx holds nothing on
		 * (skip-on-mismatch in zfs_events_get_obj). Either
		 * way the batch is dropped; account it on a fresh ad-hoc
		 * tx when a racing ring gave us an id to account against
		 * (the drop is a no-op when obj is still 0, i.e. no ring
		 * exists at all).
		 */
		dmu_tx_commit(tx);
		lost = nrec;
		obj = zfsvfs->z_events_obj;
		goto drop;
	}

	/*
	 * The assigned tx commits with the loss bump below, so a
	 * concurrent clear cannot land between the failed append and
	 * the accounting: the bump either precedes the clear (and is
	 * reset with the rest of the header, correct - the cleared
	 * history subsumes it) or follows it in txg order. There is no
	 * separate ad-hoc accounting tx left to resurrect a stale
	 * count across a one-txg window.
	 */

	/* Size the batch buffer: length prefix + record body each. */
	total = 0;
	for (qe = list_head(&batch); qe != NULL; qe = list_next(&batch, qe))
		total += sizeof (uint64_t) + ZFS_EVQ_REC_MAX;

	buf = kmem_alloc(total, KM_SLEEP);
	p = buf;

	for (qe = list_head(&batch); qe != NULL; qe = list_next(&batch, qe)) {
		nvlist_t *nvl = fnvlist_alloc();
		char *packed = NULL;
		size_t packed_len = 0;
		uint64_t le_len;

		/*
		 * txg and time are schema always:true fields: deferred
		 * records must carry the same shape as the inline path.
		 * qe_time is the syscall's hrtime, not the drain time,
		 * so per-record ordering survives the batching delay.
		 */
		fnvlist_add_uint16(nvl, ZFS_EV_OP, qe->qe_op);
		fnvlist_add_uint64(nvl, ZFS_EV_OBJECT, qe->qe_object);
		fnvlist_add_uint64(nvl, ZFS_EV_IO_OFFSET, qe->qe_offset);
		fnvlist_add_uint64(nvl, ZFS_EV_IO_BYTES, qe->qe_bytes);
		fnvlist_add_uint64(nvl, ZFS_EV_UID, qe->qe_uid);
		fnvlist_add_uint64(nvl, ZFS_EV_GID, qe->qe_gid);
		if (qe->qe_have_principal)
			fnvlist_add_uint64(nvl, ZFS_EV_PRINCIPAL,
			    qe->qe_principal);
		fnvlist_add_uint64(nvl, ZFS_EV_TXG, qe->qe_txg);
		fnvlist_add_uint64(nvl, ZFS_EV_TIME, (uint64_t)qe->qe_time);
		VERIFY0(nvlist_pack(nvl, &packed,
		    &packed_len, NV_ENCODE_NATIVE, KM_SLEEP));
		fnvlist_free(nvl);
		if (packed_len > ZFS_EVQ_REC_MAX) {
			/*
			 * A record that cannot fit the budgeted slot
			 * is counted lost rather than panicking the
			 * kernel: the ring's records_lost counter is
			 * the contract for loss.
			 */
			fnvlist_pack_free(packed, packed_len);
			oversize++;
			continue;
		}

		/* p is not 8-byte aligned on later records: memcpy. */
		le_len = LE_64((uint64_t)packed_len);
		memcpy(p, &le_len, sizeof (le_len));
		memcpy(p + sizeof (uint64_t), packed, packed_len);
		p += sizeof (uint64_t) + packed_len;

		fnvlist_pack_free(packed, packed_len);
	}

	/*
	 * get_obj returns WITHOUT the ring lock held (it takes it
	 * internally for the lazy-create only). The ring lock
	 * serializes the header mutation and append against
	 * concurrent inline emitters (zfs_events_log_event holds the
	 * same lock across its append), matching the spa_history_lock
	 * pattern. Each packed record is written on its own: one
	 * zfs_events_write of the whole batch fails (and drops every
	 * record) when the batch is larger than the ring.
	 */
	mutex_enter(&zfsvfs->z_events_lock);
	err = dmu_bonus_hold(os, obj, FTAG, &dbp);
	if (err != 0) {
		mutex_exit(&zfsvfs->z_events_lock);
		dmu_tx_commit(tx);
		kmem_free(buf, total);
		lost = nrec;
		goto drop;
	}

	zep = dbp->db_data;
	dmu_buf_will_dirty(dbp, tx);
	if (zep->zep_version != ZFS_EVENTS_VERSION)
		zep->zep_version = ZFS_EVENTS_VERSION;
	{
		char *rp = buf;
		char *end = p;

		while (rp < end) {
			uint64_t rlen = zfs_events_record_reclen(rp);
			uint64_t rec_total = sizeof (uint64_t) + rlen;

			err = zfs_events_write(os, obj, rp, rec_total,
			    zep, tx);
			if (err != 0)
				break;
			rp += rec_total;
		}
		if (err != 0) {
			/*
			 * Count only the records that were not
			 * appended; advance_bof mutations made while
			 * freeing space persist and are themselves
			 * already counted in records_lost. The loss
			 * is bumped on this same assigned tx (see the
			 * comment at the top of the batch path), so
			 * it cannot race a concurrent clear's header
			 * reset: they commit in txg order. The tx
			 * still commits.
			 */
			lost = 0;
			while (rp < end) {
				uint64_t rlen = zfs_events_record_reclen(rp);

				lost++;
				rp += sizeof (uint64_t) + rlen;
			}
			zep->zep_records_lost += lost;
			lost = 0;
		}
	}
	zep->zep_records_lost += oversize;
	dmu_buf_rele(dbp, FTAG);
	mutex_exit(&zfsvfs->z_events_lock);
	kmem_free(buf, total);

	dmu_tx_commit(tx);
	goto out;

drop:
	/*
	 * The batch could not be appended (or its tx could not be
	 * assigned) and the batch tx is already finished, so account the
	 * loss on a fresh ad-hoc tx. A concurrent clear racing this
	 * commit can only overstate loss by at most one batch in one
	 * txg; accounting-only.
	 */
	zfs_events_account_lost(os, obj, lost, &zfsvfs->z_events_lock);

out:
	qe = list_remove_head(&batch);
	while (qe != NULL) {
		kmem_cache_free(zfs_events_qent_cache, qe);
		qe = list_remove_head(&batch);
	}
	list_destroy(&batch);
}

/*
 * Teardown: stop accepting records and drain synchronously so no
 * deferred record outlives the dataset's unmount. Called from
 * zfsvfs_free() with no VFS activity possible on the dataset.
 */
void
zfs_events_drain_shutdown(zfsvfs_t *zfsvfs)
{
	zfs_events_qent_t *qe;
	taskq_t *tq;
	boolean_t pending;
	uint64_t swept = 0;

	mutex_enter(&zfsvfs->z_events_lock);
	zfsvfs->z_evq_shutdown = B_TRUE;
	pending = !list_is_empty(&zfsvfs->z_evq_deferred);
	tq = zfsvfs->z_evq_taskq;
	zfsvfs->z_evq_taskq = NULL;
	mutex_exit(&zfsvfs->z_events_lock);

	/*
	 * A failed taskq_dispatch leaves entries queued with no
	 * worker. Dispatch one last drain before the wait so those
	 * records are written or counted lost instead of leaked.
	 * z_os must still be valid here.
	 */
	if (tq != NULL) {
		/*
		 * z_os is NULL on a failed reopen: there is no ring to
		 * drain into, so skip the worker (drain_task would
		 * dereference a NULL objset) and let the sweep below free
		 * the queued entries.
		 */
		if (pending && zfsvfs->z_os != NULL)
			(void) taskq_dispatch(tq, zfs_events_drain_task,
			    zfsvfs, TQ_SLEEP);
		taskq_wait(tq);
		taskq_destroy(tq);
	}

	/*
	 * Sweep any entries still on the deferred list. With the
	 * dispatch-failure undo and the shutdown checks above the
	 * list is normally empty here, but a racing enqueue that
	 * passed its shutdown check just before it was set could
	 * still land one (and a failed drain could leave a batch
	 * behind in principle). They were never dispatched, so count
	 * them as loss before freeing rather than leak the kmem_cache
	 * entries with the zfsvfs (list_destroy frees the list, not
	 * its entries). The accounting needs a live ring; when z_os is
	 * already gone it is skipped and the loss is unreportable.
	 */
	mutex_enter(&zfsvfs->z_events_lock);
	while ((qe = list_remove_head(&zfsvfs->z_evq_deferred)) != NULL) {
		swept++;
		kmem_cache_free(zfs_events_qent_cache, qe);
	}
	zfsvfs->z_evq_count = 0;
	mutex_exit(&zfsvfs->z_events_lock);

	zfs_events_account_lost(zfsvfs->z_os, zfsvfs->z_events_obj, swept,
	    &zfsvfs->z_events_lock);
}
#else	/* !_KERNEL (libzpool builds the record builders) */

/*
 * Userspace stubs: registration is a kernel-module feature (reached
 * via ioctl); libzpool-compiled record builders never match a
 * principal, which keeps the "absent is never fabricated" contract.
 */
boolean_t
zfs_events_principal_get(uint64_t *tagp)
{
	(void) tagp;
	return (B_FALSE);
}

boolean_t
zfs_events_principal_set(boolean_t have, uint64_t tag, uint64_t *genp)
{
	(void) have, (void) tag, (void) genp;
	return (B_FALSE);
}

#endif	/* !_KERNEL */

/*
 * Destroy a dataset's event log object, undoing everything
 * zfs_events_create_obj()/get_obj() lazy-create did:
 *   - free the log object itself,
 *   - remove the com.zfs:events master-node ZAP entry,
 *   - deactivate the per-dataset feature (removes the dataset's MOS
 *     ZAP entry and decrements the pool refcount), symmetric with
 *     dsl_dataset_activate_feature() in zfs_events_feature_sync().
 * A bare pool-level spa_feature_decr() would leak the per-dataset
 * feature ZAP entry and leave dsl_dataset_feature_is_active() true.
 *
 * The caller's tx must hold the log object (bonus + write), the
 * master-node ZAP (dmu_tx_hold_zap(MASTER_NODE_OBJ, TRUE,
 * ZFS_EVENTS_ZAP_NAME)) and the dataset's feature ZAP on the MOS.
 */
int
zfs_events_destroy_obj(objset_t *os, uint64_t obj, dmu_tx_t *tx)
{
	dsl_dataset_t *ds = dmu_objset_ds(os);
	int err;

	err = dmu_object_free(os, obj, tx);
	if (err != 0)
		return (err);

	(void) zap_remove(os, MASTER_NODE_OBJ, ZFS_EVENTS_ZAP_NAME, tx);

	if (ds != NULL && dsl_dataset_feature_is_active(ds,
	    SPA_FEATURE_EVENTS))
		dsl_dataset_deactivate_feature(ds, SPA_FEATURE_EVENTS, tx);

	return (0);
}

/*
 * Open-context clear used by the ioctl path. Mirrors the VFS event
 * logging path: prepare transaction holds with zfs_events_txhold()
 * and assign with DMU_TX_WAIT. The caller MUST NOT hold the pool
 * config lock: dmu_tx_assign(DMU_TX_WAIT) asserts it is free.
 * Returns 0, or ENOENT when the dataset has no event log.
 */
int
zfs_events_clear_task(objset_t *os, kmutex_t *lockp)
{
	dmu_tx_t *tx;
	uint64_t count = 0;
	uint64_t pre_obj = 0, obj = 0;
	int err;

	/*
	 * Assign before taking the ring lock: DMU_TX_WAIT can sleep
	 * for a txg, and holding the ring lock across it stalls every
	 * emitter. The header reset itself runs under the lock (when
	 * the caller supplied one, i.e. the dataset is mounted).
	 *
	 * Mismatch guard: zfs_events_txhold() below sizes the tx's
	 * holds from the master-node ZAP at hold time. If a racing tx
	 * lazily creates (or replaces) the ring between hold time and
	 * the reset, the assigned tx holds nothing on the object it
	 * finds, and will_dirty on it panics ("dirtying dbuf but not
	 * tx_held"). Pre-lookup the object now, re-lookup after
	 * assign, and bail with ENOENT on mismatch - dropping the
	 * clear is safe (it is idempotent and retryable) while
	 * dirtying an unheld object is not. Residual window: the
	 * re-lookup below and zfs_events_clear()'s own lookup are
	 * adjacent but not atomic; closing it fully would need the
	 * object id passed under the assigned tx's consistent view,
	 * which the fixed zfs_events_clear() signature does not carry.
	 * No shipping path can exploit it today: replacing an existing
	 * ring requires destroy (zero callers of destroy_obj) or
	 * dataset destruction, which cannot race a mounted hold.
	 */
	(void) zap_lookup(os, MASTER_NODE_OBJ, ZFS_EVENTS_ZAP_NAME,
	    sizeof (uint64_t), 1, &pre_obj);

	tx = dmu_tx_create(os);
	zfs_events_txhold(os, tx);
	err = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (err != 0) {
		dmu_tx_abort(tx);
		return (err);
	}

	if (zap_lookup(os, MASTER_NODE_OBJ, ZFS_EVENTS_ZAP_NAME,
	    sizeof (uint64_t), 1, &obj) == 0 && obj != pre_obj) {
		dmu_tx_commit(tx);
		return (SET_ERROR(ENOENT));
	}

	if (lockp != NULL)
		mutex_enter(lockp);
	err = zfs_events_clear(os, tx, &count);
	if (lockp != NULL)
		mutex_exit(lockp);
	dmu_tx_commit(tx);

	return (err);
}

/*
 * Clear a dataset's event log: reset the ring header so subsequent
 * reads return nothing. Returns ENOENT if the dataset has no event
 * log. The lost-record counter is reset along with the ring pointers;
 * clearing means discarding all history.
 * Must be called from syncing context with a transaction that holds
 * the log object's bonus (dmu_tx_hold_bonus); see
 * zfs_events_clear_task() for the open-context wrapper and its
 * racing-lazy-create mismatch guard.
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
	/*
	 * *countp is BYTES of live record data discarded
	 * (zep_eof - zep_bof), not a record count: the ring header
	 * does not track record counts.
	 */
	if (countp != NULL)
		*countp = zep->zep_eof - zep->zep_bof;

	dmu_buf_will_dirty(dbp, tx);
	zep->zep_bof = 0;
	zep->zep_eof = 0;
	zep->zep_records_lost = 0;

	/*
	 * A clear leaves a logically fresh ring containing no records
	 * from any earlier format, and every subsequent append writes
	 * this module's record format - so the format stamp must be
	 * the running module's version. Without this, a ring created
	 * by an older module (before a wire-format change) would keep
	 * reporting the stale version while serving records the
	 * current module appended, misdescribing the wire contract to
	 * consumers.
	 */
	zep->zep_version = ZFS_EVENTS_VERSION;

	dmu_buf_rele(dbp, FTAG);
	return (0);
}

/*
 * Return the dataset's event log object id, creating and wiring it on
 * first use (master-node ZAP entry plus feature activation via a
 * dsl_sync_task_nowait queued on the caller's transaction). Returns 0
 * when there is no log usable by THIS transaction: the feature is
 * disabled, creation failed, or a racing transaction created the log
 * after this tx's holds were taken (see the skip-on-mismatch comment
 * below).
 *
 * Called with no locks held; takes the ring lock around the shared
 * lazy-create and always returns WITHOUT it held. The caller must take
 * the ring lock itself around the header mutation + append that
 * follows. The caller's transaction must already hold whatever the
 * append needs (zfs_events_txhold()).
 */
static uint64_t
zfs_events_get_obj(objset_t *os, dmu_tx_t *tx, uint64_t events_size,
    uint64_t *objp, kmutex_t *lockp)
{
	uint64_t obj = *objp;
	int err;

	/*
	 * Feature activation is always a sync task on the already
	 * assigned tx, including an ad-hoc one. Calling the sync
	 * callback directly dirties MOS objects on an open tx and
	 * panics debug kernels.
	 */
	/*
	 * Fast path: trust the cached id only if this tx actually holds
	 * the object. A racing creator can install the ZAP entry after
	 * this tx's zfs_events_txhold() ran, in which case the tx holds
	 * nothing on obj and dirtying it would panic (debug builds) or
	 * corrupt tx space accounting (release). Refuse (and keep the
	 * cache) so the next tx - whose txhold sees the entry - appends
	 * normally; losing one record to the race beats dirtying an
	 * unheld object.
	 */
	if (obj != 0) {
		if (tx == NULL || dmu_tx_holds_obj_bonus(tx, obj))
			return (obj);
		return (0);
	}

	mutex_enter(lockp);
	obj = *objp;
	if (obj == 0) {
		err = zap_lookup(os, MASTER_NODE_OBJ, ZFS_EVENTS_ZAP_NAME,
		    sizeof (uint64_t), 1, &obj);
		if (err == 0) {
			/*
			 * Skip-on-mismatch: the log exists, but it was
			 * created by a racing transaction this tx's
			 * zfs_events_txhold() did not see (txhold found
			 * no ZAP entry and held DMU_NEW_OBJECT, not this
			 * object's bonus/blocks). Appending on an
			 * unheld object panics "dirtying dbuf but not
			 * tx_held" (debug) or corrupts tx space
			 * accounting (release), so cache the id for
			 * future events - whose txhold WILL see it - and
			 * drop THIS event. Losing one record to a race
			 * is preferable to a will_dirty on an unheld
			 * object.
			 */
			*objp = obj;
			mutex_exit(lockp);
			return (0);
		} else if (err == ENOENT) {
			spa_t *spa = dmu_objset_spa(os);

			/*
			 * No events feature on the pool: no log.
			 */
			if (!spa_feature_is_enabled(spa,
			    SPA_FEATURE_EVENTS)) {
				mutex_exit(lockp);
				return (0);
			}

			/*
			 * ENOENT is the only branch where creation is
			 * safe: txhold saw the same ENOENT and held
			 * DMU_NEW_OBJECT plus the master-node ZAP add
			 * for this tx.
			 */
			err = zfs_events_create_obj(os, tx, events_size,
			    &obj);
			if (err != 0) {
				mutex_exit(lockp);
				return (0);
			}

			err = zap_add(os, MASTER_NODE_OBJ,
			    ZFS_EVENTS_ZAP_NAME, sizeof (uint64_t), 1,
			    &obj, tx);
			if (err != 0) {
				(void) dmu_object_free(os, obj, tx);
				mutex_exit(lockp);
				return (0);
			}

			/*
			 * Activate the feature on this same transaction:
			 * dsl_sync_task_nowait queues the sync callback
			 * on the tx's txg and the pool holds its own
			 * dataset reference for the sync task, so no
			 * objset/dataset hold is needed across an
			 * asynchronous boundary (a taskq activation
			 * here once raced unmount -> use-after-free).
			 * Legal on any assigned open-context tx; the
			 * same call serves owned and caller-provided
			 * transactions alike. Calling the sync
			 * callback directly instead would dirty MOS
			 * objects on an open tx and panic debug
			 * kernels.
			 */
			dsl_sync_task_nowait(dmu_objset_pool(os),
			    zfs_events_feature_sync, dmu_objset_ds(os),
			    tx);
		} else {
			mutex_exit(lockp);
			return (0);
		}
		*objp = obj;
	}
	mutex_exit(lockp);
	return (obj);
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
	uint64_t pr = 0;

	/*
	 * Attribution: the tag is per-thread-group and captured in
	 * syscall context, which is where the lifecycle emitters -
	 * this function's only callers - run.  Attaching it here
	 * rather than in each emitter keeps the record shape uniform
	 * across ops: a registered writer's CREATE, SETATTR, REMOVE,
	 * RENAME, LINK, SYMLINK and TRUNCATE records carry the tag
	 * exactly like its WRITE/READ records do (those are built by
	 * zfs_events_log_write()/log_read() and never reach this
	 * function, so the key is never added twice).  Unregistered
	 * writers add no key at all, so the column is never
	 * fabricated.
	 */
	if (zfs_events_principal_get(&pr))
		fnvlist_add_uint64(nvl, ZFS_EV_PRINCIPAL, pr);

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
		err = dmu_tx_assign(atx, DMU_TX_WAIT);
		if (err != 0) {
			/*
			 * dmu_tx_assign() legitimately fails on ENOSPC /
			 * EDQUOT: a full pool must not panic the kernel.
			 * Nothing is packed yet (packing happens below),
			 * and a record cannot be appended on a tx that
			 * was never assigned, so abort and drop it. The
			 * loss is not reportable on the ring here because
			 * this caller holds no ring object to bump.
			 */
			char osname[ZFS_MAX_DATASET_NAME_LEN];

			dmu_tx_abort(atx);
			dmu_objset_name(os, osname);
			cmn_err(CE_WARN, "failed to assign event log tx "
			    "for '%s': %d", osname, err);
			return;
		}
	}
	/*
	 * Finalize and pack the record BEFORE taking the ring lock:
	 * nvlist_pack and kmem_alloc can hit allocator slow paths
	 * (KM_SLEEP), and holding the ring mutex across them would
	 * propagate allocator stalls to every other emitter on the
	 * dataset. Only the header mutation and the append need the
	 * lock. Early-bail paths inside the lazy-create below must
	 * free packed/rec.
	 */
	fnvlist_add_uint64(nvl, ZFS_EV_TXG, txg);
	fnvlist_add_uint64(nvl, ZFS_EV_TIME, gethrtime());

	VERIFY0(nvlist_pack(nvl, &packed, &packed_len, NV_ENCODE_NATIVE,
	    KM_SLEEP));

	uint64_t total = sizeof (le_len) + packed_len;
	char *rec = kmem_alloc(total, KM_SLEEP);

	le_len = LE_64((uint64_t)packed_len);
	memcpy(rec, &le_len, sizeof (le_len));
	memcpy(rec + sizeof (le_len), packed, packed_len);

	/*
	 * Resolve the log object first: get_obj takes and releases the
	 * ring lock internally (lazy-create only) and returns with no
	 * lock held. Then serialize the ring-header mutation and the
	 * append under lockp: concurrent VFS writers (and the deferred
	 * drain worker) would otherwise corrupt the shared bof/eof
	 * header and interleave records, exactly as spa_history is
	 * guarded by spa_history_lock. Packing stays outside the lock
	 * (comment above); get_obj's internal enter/exit is likewise
	 * outside this critical section.
	 */
	obj = zfs_events_get_obj(os, atx, events_size, objp, lockp);
	if (obj == 0) {
		/*
		 * No log object usable by this tx (nothing to append,
		 * or a racing tx created one this tx holds nothing
		 * on): drop this record. The record is not counted in
		 * zep_records_lost here - this tx must not dirty an
		 * object it does not hold.
		 */
		fnvlist_pack_free(packed, packed_len);
		kmem_free(rec, total);
		if (owned)
			dmu_tx_commit(atx);
		return;
	}

	/*
	 * get_obj returns with the ring lock dropped. Take it for the
	 * header update and the append. The error paths below already
	 * mutex_exit.
	 */
	mutex_enter(lockp);

	/* Get the event log header from bonus buffer */
	err = dmu_bonus_hold(os, obj, FTAG, &dbp);
	if (err != 0) {
		mutex_exit(lockp);
		fnvlist_pack_free(packed, packed_len);
		kmem_free(rec, total);
		if (owned)
			dmu_tx_commit(atx);
		return;
	}

	zep = dbp->db_data;
	dmu_buf_will_dirty(dbp, atx);

	/*
	 * Records appended by this module are in this module's record
	 * format; correct a stale stamp left by an older module so the
	 * header always describes the newest record in the ring. (The
	 * pre-stamp ring has only old-format records, still described
	 * by their original version.)
	 */
	if (zep->zep_version != ZFS_EVENTS_VERSION)
		zep->zep_version = ZFS_EVENTS_VERSION;

	/*
	 * Append length + record as one atomic write. Splitting them into
	 * two zfs_events_write() calls would leave a header without a
	 * record in the log if the second call failed, and a later read
	 * would consume the next record's header as this one's payload.
	 */
	err = zfs_events_write(os, obj, rec, total, zep, atx);
	if (err != 0) {
		/*
		 * The event could not be appended. zfs_events_write()
		 * does NOT roll anything back: any advance_bof()
		 * mutations already made to free space persist (and
		 * their consumed records were counted in
		 * zep_records_lost there); the failed append itself
		 * is accounted here so the loss is not silent. The
		 * dirty bonus still commits with the tx.
		 */
		char osname[ZFS_MAX_DATASET_NAME_LEN];

		zep->zep_records_lost++;
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
 *
 * The _attr variant omits the uid/gid fields: ZIL replay re-runs the
 * vnops under kcred, so attribution would be wrong and the original
 * owner is not recoverable at replay time. Consumers treat the absent
 * fields as unknown rather than root-owned.
 */
void
zfs_events_log_create_attr(objset_t *os, dmu_tx_t *tx,
    uint64_t object, uint64_t parent, const char *name, uint64_t mode,
    uint64_t events_size, uint64_t *objp, kmutex_t *lockp)
{
	nvlist_t *nvl;

	nvl = fnvlist_alloc();
	fnvlist_add_uint16(nvl, ZFS_EV_OP, ZFS_EV_CREATE);
	fnvlist_add_uint64(nvl, ZFS_EV_OBJECT, object);
	fnvlist_add_uint64(nvl, ZFS_EV_PARENT, parent);
	fnvlist_add_string(nvl, ZFS_EV_NAME, name);
	fnvlist_add_uint64(nvl, ZFS_EV_MODE, mode);

	zfs_events_log_event(os, tx, nvl, events_size, objp, lockp,
	    dmu_tx_get_txg(tx));
	fnvlist_free(nvl);
}

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
	uint64_t pr = 0;

	nvl = fnvlist_alloc();
	fnvlist_add_uint16(nvl, ZFS_EV_OP, ZFS_EV_WRITE);
	fnvlist_add_uint64(nvl, ZFS_EV_OBJECT, object);
	fnvlist_add_uint64(nvl, ZFS_EV_IO_OFFSET, offset);
	fnvlist_add_uint64(nvl, ZFS_EV_IO_BYTES, bytes);
	fnvlist_add_uint64(nvl, ZFS_EV_UID, crgetuid((cred_t *)(uintptr_t)cr));
	fnvlist_add_uint64(nvl, ZFS_EV_GID, crgetgid((cred_t *)(uintptr_t)cr));
	if (zfs_events_principal_get(&pr))
		fnvlist_add_uint64(nvl, ZFS_EV_PRINCIPAL, pr);

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
	uint64_t pr = 0;

	nvl = fnvlist_alloc();
	fnvlist_add_uint16(nvl, ZFS_EV_OP, ZFS_EV_READ);
	fnvlist_add_uint64(nvl, ZFS_EV_OBJECT, object);
	fnvlist_add_uint64(nvl, ZFS_EV_IO_OFFSET, offset);
	fnvlist_add_uint64(nvl, ZFS_EV_IO_BYTES, bytes);
	fnvlist_add_uint64(nvl, ZFS_EV_UID, crgetuid((cred_t *)(uintptr_t)cr));
	fnvlist_add_uint64(nvl, ZFS_EV_GID, crgetgid((cred_t *)(uintptr_t)cr));
	if (zfs_events_principal_get(&pr))
		fnvlist_add_uint64(nvl, ZFS_EV_PRINCIPAL, pr);

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
    hrtime_t start, uint64_t offset, uint64_t bytes, uint64_t uid,
    uint64_t gid, uint64_t principal, boolean_t have_principal,
    uint64_t events_size, uint64_t *objp, kmutex_t *lockp,
    uint64_t txg, kmutex_t *zlk)
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
	fnvlist_add_uint64(nvl, ZFS_EV_UID, uid);
	fnvlist_add_uint64(nvl, ZFS_EV_GID, gid);
	if (have_principal)
		fnvlist_add_uint64(nvl, ZFS_EV_PRINCIPAL, principal);

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
	uint64_t offset, bytes, uid, gid;
	uint64_t principal = 0;
	boolean_t have_principal = B_FALSE;
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
		uid = zp->z_ev_io_wuid;
		gid = zp->z_ev_io_wgid;
		principal = zp->z_ev_io_wprincipal;
		have_principal = zp->z_ev_io_whaveprincipal;
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
		uid = zp->z_ev_io_ruid;
		gid = zp->z_ev_io_rgid;
		principal = zp->z_ev_io_rprincipal;
		have_principal = zp->z_ev_io_rhaveprincipal;
	}
	if (locked) {
		/*
		 * We acquired the lock ourselves (close(2)): hand it to
		 * the emitter, which drops it across the (sleeping)
		 * emission and returns with it unlocked.
		 */
		zfs_events_io_emit(zp, os, is_write, start, offset, bytes,
		    uid, gid, principal, have_principal,
		    zfsvfs->z_events_size, &zfsvfs->z_events_obj,
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
	    uid, gid, principal, have_principal,
	    zfsvfs->z_events_size, &zfsvfs->z_events_obj,
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
	uint64_t uid = 0, gid = 0;
	uint64_t principal = 0;
	boolean_t have_principal = B_FALSE;
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

	if (window_ns == 0) {
		/*
		 * Fence disabled: one record per syscall, deferred.
		 * The syscall only enqueues a fixed-size entry and
		 * wakes the dataset's drain worker - it never builds
		 * an nvlist, never assigns a transaction, never
		 * sleeps (KM_NOSLEEP allocation; inline fallback if
		 * the entry cannot be queued). The worker batches
		 * everything queued into one transaction, so bulk IO
		 * also amortizes ring-lock traffic and ring block
		 * rewrites across whole batches instead of paying
		 * per record.
		 */
		uint16_t op = is_write ? ZFS_EV_WRITE : ZFS_EV_READ;

		if (!zfs_events_io_defer(zfsvfs, op, zp->z_id, offset,
		    bytes, cr, txg)) {
			/*
			 * Queue full or taskq unavailable: emit
			 * inline as before. Never silently drop.
			 *
			 * Accepted semantics: the inline record is
			 * appended to the ring immediately, so it
			 * can overtake older records still sitting
			 * on the deferred queue - per-file IO
			 * records may reorder under overload. Each
			 * record carries its own txg/time stamp, so
			 * consumers can restore true order; the ring
			 * order itself is only an emission hint.
			 */
			if (is_write) {
				zfs_events_log_write(zfsvfs->z_os,
				    NULL, zp->z_id, offset, bytes, cr,
				    zfsvfs->z_events_size,
				    &zfsvfs->z_events_obj,
				    &zfsvfs->z_events_lock, txg);
			} else {
				zfs_events_log_read(zfsvfs->z_os,
				    zp->z_id, offset, bytes, cr,
				    zfsvfs->z_events_size,
				    &zfsvfs->z_events_obj,
				    &zfsvfs->z_events_lock);
			}
		}
		return;
	}

	now = gethrtime();

	mutex_enter(zlk);

	if (is_write) {
		start = zp->z_ev_io_wstart;
		if (start != 0) {
			pend_off = zp->z_ev_io_wpend_off;
			pend_bytes = zp->z_ev_io_wpend_bytes;
			uid = zp->z_ev_io_wuid;
			gid = zp->z_ev_io_wgid;
			principal = zp->z_ev_io_wprincipal;
			have_principal = zp->z_ev_io_whaveprincipal;
		}
	} else {
		start = zp->z_ev_io_rstart;
		if (start != 0) {
			pend_off = zp->z_ev_io_rpend_off;
			pend_bytes = zp->z_ev_io_rpend_bytes;
			uid = zp->z_ev_io_ruid;
			gid = zp->z_ev_io_rgid;
			principal = zp->z_ev_io_rprincipal;
			have_principal = zp->z_ev_io_rhaveprincipal;
		}
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
		/*
		 * Expiry must attribute the merged record to the
		 * window's captured owner (sampled above with the
		 * pending fields), not to whichever thread happened
		 * to touch the file last.
		 */
		zfs_events_io_emit(zp, zfsvfs->z_os, is_write, start,
		    pend_off, pend_bytes, uid, gid, principal,
		    have_principal, zfsvfs->z_events_size,
		    &zfsvfs->z_events_obj, &zfsvfs->z_events_lock,
		    txg, zlk);

		mutex_enter(zlk);
		if (is_write) {
			if (zp->z_ev_io_wstart == 0) {
				zp->z_ev_io_wstart = now;
				zp->z_ev_io_wpend_off = offset;
				zp->z_ev_io_wpend_bytes = bytes;
				zp->z_ev_io_wuid =
				    crgetuid((cred_t *)(uintptr_t)cr);
				zp->z_ev_io_wgid =
				    crgetgid((cred_t *)(uintptr_t)cr);
				zp->z_ev_io_whaveprincipal =
				    zfs_events_principal_get(
				    &zp->z_ev_io_wprincipal);
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
				zp->z_ev_io_ruid =
				    crgetuid((cred_t *)(uintptr_t)cr);
				zp->z_ev_io_rgid =
				    crgetgid((cred_t *)(uintptr_t)cr);
				zp->z_ev_io_rhaveprincipal =
				    zfs_events_principal_get(
				    &zp->z_ev_io_rprincipal);
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
		zp->z_ev_io_wuid = crgetuid((cred_t *)(uintptr_t)cr);
		zp->z_ev_io_wgid = crgetgid((cred_t *)(uintptr_t)cr);
		zp->z_ev_io_whaveprincipal =
		    zfs_events_principal_get(&zp->z_ev_io_wprincipal);
	} else {
		zp->z_ev_io_rstart = now;
		zp->z_ev_io_rpend_off = offset;
		zp->z_ev_io_rpend_bytes = bytes;
		zp->z_ev_io_ruid = crgetuid((cred_t *)(uintptr_t)cr);
		zp->z_ev_io_rgid = crgetgid((cred_t *)(uintptr_t)cr);
		zp->z_ev_io_rhaveprincipal =
		    zfs_events_principal_get(&zp->z_ev_io_rprincipal);
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
	uint64_t phys_read_off, phys_eof;
	uint64_t leftover = 0;
	zfs_events_phys_t *zep;
	int err;

	/* Look up the event log object */
	err = zap_lookup(os, MASTER_NODE_OBJ, ZFS_EVENTS_ZAP_NAME,
	    sizeof (uint64_t), 1, &obj);
	if (err != 0)	/* ENOENT means "no ring"; propagate the rest */
		return (err);

	err = dmu_bonus_hold(os, obj, FTAG, &dbp);
	if (err != 0)
		return (err);

	/*
	 * Sample the ring window under the ring lock, then release it
	 * for the (blocking, possibly disk-backed) dmu_read()s and
	 * re-validate afterwards. Holding the lock across reads of up
	 * to the ioctl buffer size would stall every emitter on the
	 * dataset for I/O-latency durations. The data read is valid
	 * iff the ring window did not move underneath it: bof must be
	 * unchanged (a wrap would have overwritten our region, and a
	 * clear resets bof to 0) and eof must not have retreated.
	 * Appends only advance eof, so "eof unchanged or grown" is
	 * exact. On violation the sample is simply stale - retry with
	 * a fresh sample; three attempts is far beyond what any real
	 * contention requires.
	 */
	for (int attempt = 0; ; attempt++) {
		uint64_t samp_bof, samp_eof, samp_off, samp_len;
		uint64_t samp_lost, samp_guid;

		mutex_enter(lockp);
		zep = dbp->db_data;

		samp_bof = zep->zep_bof;
		samp_eof = zep->zep_eof;
		/*
		 * Sample the loss counter and ring GUID too: both move
		 * only when records were dropped (a wrap) or the header
		 * was reset, so they catch a wrap/clear that leaves bof
		 * and eof looking unchanged.
		 */
		samp_lost = zep->zep_records_lost;
		samp_guid = zep->zep_guid;

		/* Validate and clamp the read offset */
		if (*offp < samp_bof)
			samp_off = samp_bof;
		else if (*offp > samp_eof)
			samp_off = samp_eof;
		else
			samp_off = *offp;

		/* Calculate how much we can read */
		samp_len = MIN(*lenp, samp_eof - samp_off);

		if (samp_len == 0) {
			mutex_exit(lockp);
			dmu_buf_rele(dbp, FTAG);
			*offp = samp_off;
			*lenp = 0;
			return (0);
		}

		/* Convert to physical offset and handle wrap-around */
		phys_read_off = zfs_events_log_to_phys(samp_off, zep);
		phys_eof = zfs_events_log_to_phys(samp_eof, zep);

		mutex_exit(lockp);

		if (phys_read_off < phys_eof) {
			/* No wrap, simple read */
			err = dmu_read(os, obj, phys_read_off, samp_len,
			    buf, DMU_READ_PREFETCH);
		} else {
			/* Handle wrap-around read */
			uint64_t first = zep->zep_phys_max_off -
			    phys_read_off;
			if (first > samp_len)
				first = samp_len;

			err = dmu_read(os, obj, phys_read_off, first,
			    buf, DMU_READ_PREFETCH);
			if (err == 0 && samp_len > first) {
				leftover = samp_len - first;
				err = dmu_read(os, obj, 0, leftover,
				    buf + first, DMU_READ_PREFETCH);
			}
		}

		mutex_enter(lockp);
		zep = dbp->db_data;
		/*
		 * Residual limitation: a clear that lands while samp_bof
		 * is 0 preserves the GUID and zeroes records_lost, so a
		 * refill that grows eof past samp_eof can still satisfy
		 * this check. Header sampling alone cannot tell a cleared
		 * ring from a fresh one with the same GUID.
		 */
		boolean_t valid = (zep->zep_bof == samp_bof) &&
		    (zep->zep_eof >= samp_eof) &&
		    (zep->zep_records_lost == samp_lost) &&
		    (zep->zep_guid == samp_guid);
		mutex_exit(lockp);

		if (err == 0 && valid) {
			*offp = samp_off + samp_len;
			*lenp = samp_len;
			dmu_buf_rele(dbp, FTAG);
			return (0);
		}

		if (err != 0 || attempt >= 2) {
			dmu_buf_rele(dbp, FTAG);
			return (err != 0 ? err : SET_ERROR(EAGAIN));
		}
		/*
		 * Ring moved under the read: loop for a fresh sample.
		 * *offp is left untouched so the next attempt reclamps
		 * against the new window.
		 */
	}
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
	if (err != 0)	/* ENOENT means "no ring"; propagate the rest */
		return (err);

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
	if (err != 0)	/* ENOENT means "no ring"; propagate the rest */
		return (err);

	err = dmu_bonus_hold(os, obj, FTAG, &dbp);
	if (err != 0)
		return (err);

	zep = dbp->db_data;
	*verp = zep->zep_version;
	dmu_buf_rele(dbp, FTAG);
	return (0);
}

/*
 * Return the GUID stamped on the event ring at creation (its log
 * lifetime identity). Returns ENOENT if the dataset has no event log.
 */
int
zfs_events_get_guid(objset_t *os, uint64_t *guidp)
{
	dmu_buf_t *dbp;
	zfs_events_phys_t *zep;
	uint64_t obj;
	int err;

	err = zap_lookup(os, MASTER_NODE_OBJ, ZFS_EVENTS_ZAP_NAME,
	    sizeof (uint64_t), 1, &obj);
	if (err != 0)	/* ENOENT means "no ring"; propagate the rest */
		return (err);

	err = dmu_bonus_hold(os, obj, FTAG, &dbp);
	if (err != 0)
		return (err);

	zep = dbp->db_data;
	*guidp = zep->zep_guid;
	dmu_buf_rele(dbp, FTAG);
	return (0);
}

/*
 * Logical eof of the event ring. A consumer whose stored cursor is
 * past this value is looking at a cleared ring (clear resets eof to
 * 0 and does not rotate the ring GUID). Returns ENOENT if there is
 * no event log.
 */
int
zfs_events_get_eof(objset_t *os, uint64_t *eofp)
{
	dmu_buf_t *dbp;
	zfs_events_phys_t *zep;
	uint64_t obj;
	int err;

	err = zap_lookup(os, MASTER_NODE_OBJ, ZFS_EVENTS_ZAP_NAME,
	    sizeof (uint64_t), 1, &obj);
	if (err != 0)	/* ENOENT means "no ring"; propagate the rest */
		return (err);

	err = dmu_bonus_hold(os, obj, FTAG, &dbp);
	if (err != 0)
		return (err);

	zep = dbp->db_data;
	*eofp = zep->zep_eof;
	dmu_buf_rele(dbp, FTAG);
	return (0);
}

#if defined(_KERNEL)
EXPORT_SYMBOL(zfs_events_create_obj);
EXPORT_SYMBOL(zfs_events_txhold);
EXPORT_SYMBOL(zfs_events_seed_obj);
EXPORT_SYMBOL(zfs_events_get_lost);
EXPORT_SYMBOL(zfs_events_get_schema_version);
EXPORT_SYMBOL(zfs_events_get_guid);
EXPORT_SYMBOL(zfs_events_get_eof);
EXPORT_SYMBOL(zfs_events_destroy_obj);
EXPORT_SYMBOL(zfs_events_log_create);
EXPORT_SYMBOL(zfs_events_log_create_attr);
EXPORT_SYMBOL(zfs_events_log_remove);
EXPORT_SYMBOL(zfs_events_log_rename);
EXPORT_SYMBOL(zfs_events_log_link);
EXPORT_SYMBOL(zfs_events_log_symlink);
EXPORT_SYMBOL(zfs_events_log_truncate);
EXPORT_SYMBOL(zfs_events_log_setattr);
EXPORT_SYMBOL(zfs_events_get);
#endif
