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
 *
 * Copyright (c) 2026 by HeonJe Lee
 */
/*
 * zfs changelog - per-dataset metadata change stream.
 *
 * Metadata operations append a record to an on-disk log in the same
 * dmu_tx that performs the change, so a record exists if and only if the
 * change committed.  Consumers register by name, read from a cursor and
 * clear what they have processed; cleared records are trimmed.  The log
 * never blocks the data path: past a retention cap the oldest chunk is
 * dropped and the generation counter is bumped so consumers learn that
 * they must rescan instead of silently missing changes.
 *
 * Layout (all inside the dataset's own objset):
 *
 *	master node ZAP "CHANGELOG" -> header object
 *	header object   : changelog_phys_t at offset 0
 *	catalog ZAP     : chunk index -> chunk object
 *	chunk object    : clp_recs_per_chunk fixed-size slots
 *	consumer ZAP    : consumer name -> changelog_consumer_phys_t
 *
 * A record's seq is its slot index, so a cursor is just a number and
 * seeking is arithmetic.  seq is assigned while holding cl_lock, and the
 * slot is written before the head advances, so a reader that observes
 * head N knows every slot below N is written: the stream has no holes.
 * That invariant is also what makes recovery cheap - the head is found
 * by binary searching the newest chunk at open time, which is why an
 * append writes one block (the record) and never the header.
 *
 * Space for the append is reserved before dmu_tx_assign() via
 * zfs_changelog_tx_hold(); a reservation that is never used writes
 * nothing, so failed operations need no unwind.
 */

#include <sys/zfs_context.h>
#include <sys/zfs_changelog.h>
#include <sys/zfs_znode.h>
#include <sys/dmu.h>
#include <sys/dmu_tx.h>
#include <sys/dmu_objset.h>
#include <sys/zap.h>

/* Records kept before the oldest are force-dropped (2M x 256B = 512MB). */
static uint64_t changelog_retention_recs = 2ULL * 1024 * 1024;
/* Slots per chunk object (64Ki x 256B = 16MB per chunk). */
static uint64_t changelog_chunk_recs = 64ULL * 1024;
/* Perf attribution only: keep the tx holds but skip the record write. */
static uint_t changelog_test_nowrite = 0;
/* Async: the op path only queues records, the txg sync path flushes. */
uint_t changelog_async = 1;

/* Queued record; the txg sync path writes it with its own transaction. */
typedef struct changelog_qent {
	list_node_t	ql_link;
	changelog_rec_t	ql_rec;
} changelog_qent_t;

#define	CL_HDR_BLKSZ		512
#define	CL_CHUNK_BLKSZ		4096
#define	CL_READ_BATCH		256	/* records per dmu_read */

typedef struct changelog {
	char		cl_dsname[256];	/* for the flush task's objset hold */
	kmutex_t	cl_lock;	/* protects cl_phys and chunk cache */
	kmutex_t	cl_open_lock;	/* serializes first-use setup */
	kcondvar_t	cl_cv;		/* readers waiting for an append */
	uint_t		cl_waiters;
	kmutex_t	cl_qlock;	/* guards the async record queue */
	list_t		cl_queue;	/* pending records (changelog_qent_t) */
	uint64_t	cl_qlen;
	boolean_t	cl_flushing;	/* one flusher at a time */
	boolean_t	cl_ready;
	uint64_t	cl_hdr_obj;
	uint64_t	cl_head;	/* next seq; authoritative in core */
	changelog_phys_t	cl_phys;
	boolean_t	cl_phys_dirty;	/* header needs a flush */
	uint64_t	cl_chunk_idx;	/* cached current chunk */
	uint64_t	cl_chunk_obj;
	uint64_t	cl_next_obj;	/* the chunk after cl_chunk_idx */
} changelog_t;

/* Global counters, exported through /proc/spl/kstat/zfs/changelog. */
static kstat_t		*cl_ksp;
typedef struct changelog_kstat {
	kstat_named_t	dlk_records;	/* records appended */
	kstat_named_t	dlk_dropped;	/* records lost to forced trim */
	kstat_named_t	dlk_gaps;	/* forced trim events */
	kstat_named_t	dlk_trims;	/* chunks freed */
	kstat_named_t	dlk_errors;	/* appends skipped on error */
	kstat_named_t	dlk_flushes;	/* async: txg sync batch flushes */
} changelog_kstat_t;
static changelog_kstat_t cl_kstat = {
	{ "records",	KSTAT_DATA_UINT64 },
	{ "dropped",	KSTAT_DATA_UINT64 },
	{ "gaps",	KSTAT_DATA_UINT64 },
	{ "trims",	KSTAT_DATA_UINT64 },
	{ "errors",	KSTAT_DATA_UINT64 },
	{ "flushes",	KSTAT_DATA_UINT64 },
};

static kmutex_t		cl_state_lock;	/* guards os_changelog_state setup */
static kmem_cache_t	*cl_rec_cache;
static kmem_cache_t	*cl_qent_cache;

#define	CL_STAT_BUMP(f)	\
	atomic_inc_64(&cl_kstat.f.value.ui64)
#define	CL_STAT_ADD(f, n)	\
	atomic_add_64(&cl_kstat.f.value.ui64, (n))

static int
cl_kstat_update(kstat_t *ksp, int rw)
{
	(void) ksp;

	if (rw == KSTAT_WRITE)
		return (SET_ERROR(EACCES));
	return (0);
}

void
zfs_changelog_init(void)
{
	/* The on-disk layout contract: one record per 128-byte slot. */
	_Static_assert(sizeof (changelog_rec_t) == CL_REC_SIZE,
	    "changelog record must fill its slot exactly");

	mutex_init(&cl_state_lock, NULL, MUTEX_DEFAULT, NULL);
	cl_rec_cache = kmem_cache_create("changelog_rec",
	    sizeof (changelog_rec_t), 0, NULL, NULL, NULL, NULL, NULL, 0);
	cl_qent_cache = kmem_cache_create("changelog_qent",
	    sizeof (changelog_qent_t), 0, NULL, NULL, NULL, NULL, NULL, 0);

	cl_ksp = kstat_create("zfs", 0, "changelog", "misc", KSTAT_TYPE_NAMED,
	    sizeof (changelog_kstat_t) / sizeof (kstat_named_t),
	    KSTAT_FLAG_VIRTUAL);
	if (cl_ksp != NULL) {
		cl_ksp->ks_data = &cl_kstat;
		cl_ksp->ks_update = cl_kstat_update;
		kstat_install(cl_ksp);
	}
}

void
zfs_changelog_fini(void)
{
	if (cl_ksp != NULL) {
		kstat_delete(cl_ksp);
		cl_ksp = NULL;
	}
	if (cl_rec_cache != NULL) {
		kmem_cache_destroy(cl_rec_cache);
		cl_rec_cache = NULL;
	}
	if (cl_qent_cache != NULL) {
		kmem_cache_destroy(cl_qent_cache);
		cl_qent_cache = NULL;
	}
	mutex_destroy(&cl_state_lock);
}

/* In-core state, attached to the objset on first use. */
static changelog_t *
cl_state(objset_t *os)
{
	changelog_t *dl = os->os_changelog_state;

	if (dl != NULL)
		return (dl);

	mutex_enter(&cl_state_lock);
	dl = os->os_changelog_state;
	if (dl == NULL) {
		dl = kmem_zalloc(sizeof (changelog_t), KM_SLEEP);
		(void) dmu_objset_name(os, dl->cl_dsname);
		mutex_init(&dl->cl_lock, NULL, MUTEX_DEFAULT, NULL);
		mutex_init(&dl->cl_open_lock, NULL, MUTEX_DEFAULT, NULL);
		mutex_init(&dl->cl_qlock, NULL, MUTEX_DEFAULT, NULL);
		list_create(&dl->cl_queue, sizeof (changelog_qent_t),
		    offsetof(changelog_qent_t, ql_link));
		cv_init(&dl->cl_cv, NULL, CV_DEFAULT, NULL);
		dl->cl_chunk_idx = UINT64_MAX;
		membar_producer();
		os->os_changelog_state = dl;
	}
	mutex_exit(&cl_state_lock);
	return (dl);
}

void
zfs_changelog_objset_fini(objset_t *os)
{
	changelog_t *dl = os->os_changelog_state;

	if (dl == NULL)
		return;

	os->os_changelog_state = NULL;
	/* Records queued without a sync (should not happen) are dropped. */
	mutex_enter(&dl->cl_qlock);
	changelog_qent_t *qe;
	while ((qe = list_remove_head(&dl->cl_queue)) != NULL)
		kmem_cache_free(cl_rec_cache, qe);
	mutex_exit(&dl->cl_qlock);
	list_destroy(&dl->cl_queue);
	cv_destroy(&dl->cl_cv);
	mutex_destroy(&dl->cl_qlock);
	mutex_destroy(&dl->cl_open_lock);
	mutex_destroy(&dl->cl_lock);
	kmem_free(dl, sizeof (changelog_t));
}

/* Create the on-disk structures.  Runs in its own transaction. */
static int
cl_create(objset_t *os, uint64_t *hdrp)
{
	dmu_tx_t *tx;
	changelog_phys_t *phys;
	uint64_t hdr, cat, con;
	int err;

	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, MASTER_NODE_OBJ, B_TRUE, CHANGELOG_MASTER_KEY);
	dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0, sizeof (changelog_phys_t));
	dmu_tx_hold_zap(tx, DMU_NEW_OBJECT, B_TRUE, NULL);
	dmu_tx_hold_zap(tx, DMU_NEW_OBJECT, B_TRUE, NULL);
	err = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (err != 0) {
		zfs_dbgmsg("changelog: create tx assign failed err=%d", err);
		dmu_tx_abort(tx);
		return (err);
	}

	hdr = dmu_object_alloc(os, DMU_OTN_UINT8_METADATA, CL_HDR_BLKSZ,
	    DMU_OT_NONE, 0, tx);
	cat = zap_create(os, DMU_OTN_ZAP_METADATA, DMU_OT_NONE, 0, tx);
	con = zap_create(os, DMU_OTN_ZAP_METADATA, DMU_OT_NONE, 0, tx);

	phys = kmem_zalloc(sizeof (changelog_phys_t), KM_SLEEP);
	phys->clp_magic = CL_PHYS_MAGIC;
	phys->clp_version = CL_VERSION;
	phys->clp_catalog_obj = cat;
	phys->clp_consumer_obj = con;
	phys->clp_recs_per_chunk = changelog_chunk_recs;
	phys->clp_rec_size = CL_REC_SIZE;
	phys->clp_enq_txg = 0;
	phys->clp_covered_txg = 0;
	dmu_write(os, hdr, 0, sizeof (changelog_phys_t), phys, tx,
	    DMU_READ_NO_PREFETCH);
	kmem_free(phys, sizeof (changelog_phys_t));

	err = zap_add(os, MASTER_NODE_OBJ, CHANGELOG_MASTER_KEY,
	    sizeof (uint64_t), 1, &hdr, tx);
	if (err != 0) {
		zfs_dbgmsg("changelog: master zap_add failed err=%d", err);
		(void) dmu_object_free(os, hdr, tx);
		(void) dmu_object_free(os, cat, tx);
		(void) dmu_object_free(os, con, tx);
	}
	dmu_tx_commit(tx);

	if (err == EEXIST) {
		/* Lost a race with another opener; use the winner's log. */
		err = zap_lookup(os, MASTER_NODE_OBJ, CHANGELOG_MASTER_KEY,
		    sizeof (uint64_t), 1, &hdr);
	}
	if (err == 0)
		*hdrp = hdr;
	return (err);
}

/*
 * Count valid slots from the start of a chunk.  Slot i holds a record iff
 * i < head, so the predicate is monotonic and a binary search finds the
 * boundary exactly.
 */
static int
cl_chunk_count_valid(objset_t *os, uint64_t obj, uint64_t idx, uint64_t cpc,
    changelog_rec_t *rec, uint64_t *countp)
{
	uint64_t a = 0, b = cpc;
	int err = 0;

	while (a < b) {
		uint64_t mid = a + (b - a) / 2;

		err = dmu_read(os, obj, mid * sizeof (changelog_rec_t),
		    sizeof (changelog_rec_t), rec, DMU_READ_NO_PREFETCH);
		if (err != 0)
			break;
		if (rec->clr_magic == CL_REC_MAGIC &&
		    rec->clr_seq == idx * cpc + mid)
			a = mid + 1;
		else
			b = mid;
	}
	if (err != 0)
		return (err);
	*countp = a;
	return (0);
}

/*
 * Recover the head by scanning chunks: the max_chunk hint can point at
 * an eagerly created empty chunk, so walk backward over empty chunks
 * until the last non-empty one.
 */
static int
cl_recover_head(objset_t *os, const changelog_phys_t *phys, uint64_t *headp)
{
	changelog_rec_t *rec;
	uint64_t cpc = phys->clp_recs_per_chunk;
	uint64_t idx = phys->clp_max_chunk;
	uint64_t lo = 0;
	int err = 0;

	if (phys->clp_tail / cpc > idx)
		idx = phys->clp_tail / cpc;

	rec = kmem_alloc(sizeof (changelog_rec_t), KM_SLEEP);

	for (;;) {
		uint64_t obj;

		err = zap_lookup_int_key(os, phys->clp_catalog_obj, idx, &obj);
		if (err == ENOENT) {
			/* No chunk here: the log ends below this index. */
			*headp = idx * cpc;
			err = 0;
			break;
		}
		if (err != 0)
			break;

		err = cl_chunk_count_valid(os, obj, idx, cpc, rec, &lo);
		if (err != 0)
			break;
		if (lo > 0) {
			/* Records in this chunk: the head is inside it. */
			*headp = idx * cpc + lo;
			break;
		}
		/* Empty chunk: step back (it may be an eager creation). */
		if (idx == 0) {
			*headp = 0;
			break;
		}
		idx--;
	}

	kmem_free(rec, sizeof (changelog_rec_t));
	return (err);
}

static int
cl_open(objset_t *os, changelog_t *dl)
{
	changelog_phys_t *phys;
	uint64_t hdr = 0;
	int err;

	if (dl->cl_ready)
		return (0);

	mutex_enter(&dl->cl_open_lock);
	if (dl->cl_ready) {
		mutex_exit(&dl->cl_open_lock);
		return (0);
	}

	err = zap_lookup(os, MASTER_NODE_OBJ, CHANGELOG_MASTER_KEY,
	    sizeof (uint64_t), 1, &hdr);
	if (err == ENOENT)
		err = cl_create(os, &hdr);
	if (err != 0) {
		zfs_dbgmsg("changelog: open/create failed err=%d", err);
		mutex_exit(&dl->cl_open_lock);
		return (err);
	}

	phys = kmem_zalloc(sizeof (changelog_phys_t), KM_SLEEP);
	err = dmu_read(os, hdr, 0, sizeof (changelog_phys_t), phys,
	    DMU_READ_PREFETCH);
	if (err != 0)
		zfs_dbgmsg("changelog: header read failed err=%d", err);
	if (err == 0 && (phys->clp_magic != CL_PHYS_MAGIC ||
	    phys->clp_version != CL_VERSION ||
	    phys->clp_recs_per_chunk == 0 ||
	    phys->clp_rec_size != CL_REC_SIZE)) {
		zfs_dbgmsg("changelog: bad header magic=%llx ver=%llu rpc=%llu "
		    "rsize=%llu", (u_longlong_t)phys->clp_magic,
		    (u_longlong_t)phys->clp_version,
		    (u_longlong_t)phys->clp_recs_per_chunk,
		    (u_longlong_t)phys->clp_rec_size);
		err = SET_ERROR(ENOTSUP);
	}
	if (err == 0) {
		uint64_t head = 0;

		err = cl_recover_head(os, phys, &head);
		if (err != 0) {
			zfs_dbgmsg("changelog: head recovery failed err=%d",
			    err);
		} else {
			if (head < phys->clp_tail)
				head = phys->clp_tail;
			/*
			 * Crash-gap detection: enq_txg lands in the same
			 * txg as the changes, covered_txg with the flush.
			 * enq > covered means the txg sync that would have
			 * flushed the tail never completed - declare the
			 * gap so consumers rescan instead of silently
			 * missing changes.
			 */
			if (head > 0 &&
			    phys->clp_enq_txg > phys->clp_covered_txg) {
				phys->clp_generation++;
				zfs_dbgmsg("changelog: crash gap detected "
				    "enq=%llu covered=%llu",
				    (u_longlong_t)phys->clp_enq_txg,
				    (u_longlong_t)phys->clp_covered_txg);
			}
			mutex_enter(&dl->cl_lock);
			dl->cl_phys = *phys;
			dl->cl_head = head;
			dl->cl_hdr_obj = hdr;
			dl->cl_chunk_idx = UINT64_MAX;
			dl->cl_chunk_obj = 0;
			dl->cl_next_obj = 0;
			dl->cl_ready = B_TRUE;
			mutex_exit(&dl->cl_lock);
		}
	}
	kmem_free(phys, sizeof (changelog_phys_t));
	mutex_exit(&dl->cl_open_lock);
	return (err);
}

/* Write the cached header back.  Caller holds cl_lock. */
static void
cl_phys_sync(objset_t *os, changelog_t *dl, dmu_tx_t *tx)
{
	ASSERT(MUTEX_HELD(&dl->cl_lock));
	dl->cl_phys.clp_head_hint = dl->cl_head;
	dmu_write(os, dl->cl_hdr_obj, 0, sizeof (changelog_phys_t),
	    &dl->cl_phys, tx, DMU_READ_NO_PREFETCH);
}

static int
cl_chunk_lookup(objset_t *os, changelog_t *dl, uint64_t idx, uint64_t *objp)
{
	return (zap_lookup_int_key(os, dl->cl_phys.clp_catalog_obj, idx, objp));
}

/* Create chunk idx if it does not exist yet.  Uses its own transaction. */
static int
cl_chunk_create(objset_t *os, changelog_t *dl, uint64_t idx, uint64_t *objp)
{
	dmu_tx_t *tx;
	uint64_t obj;
	int err;

	err = cl_chunk_lookup(os, dl, idx, objp);
	if (err != ENOENT)
		return (err);

	tx = dmu_tx_create(os);
	dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0, CL_REC_SIZE);
	dmu_tx_hold_zap(tx, dl->cl_phys.clp_catalog_obj, B_TRUE, NULL);
	dmu_tx_hold_write(tx, dl->cl_hdr_obj, 0, sizeof (changelog_phys_t));
	err = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (err != 0) {
		zfs_dbgmsg("changelog: chunk tx assign failed err=%d", err);
		dmu_tx_abort(tx);
		return (err);
	}
	obj = dmu_object_alloc(os, DMU_OTN_UINT8_METADATA, CL_CHUNK_BLKSZ,
	    DMU_OT_NONE, 0, tx);
	err = zap_add_int_key(os, dl->cl_phys.clp_catalog_obj, idx, obj, tx);
	if (err != 0) {
		(void) dmu_object_free(os, obj, tx);
	} else {
		/*
		 * Persist the high water mark in the same transaction that
		 * creates the chunk, so head recovery always knows which
		 * chunk to scan.
		 */
		mutex_enter(&dl->cl_lock);
		if (idx > dl->cl_phys.clp_max_chunk)
			dl->cl_phys.clp_max_chunk = idx;
		cl_phys_sync(os, dl, tx);
		mutex_exit(&dl->cl_lock);
	}
	dmu_tx_commit(tx);

	if (err == EEXIST)
		err = cl_chunk_lookup(os, dl, idx, objp);
	else if (err == 0)
		*objp = obj;
	return (err);
}

/*
 * Make sure the chunk holding seq and the one after it exist, and cache
 * them.  Called before dmu_tx_assign(), so creating objects here is safe.
 */
static int
cl_chunks_ready(objset_t *os, changelog_t *dl, uint64_t idx, uint64_t *objp,
    uint64_t *nextp)
{
	uint64_t obj, next;
	int err;

	mutex_enter(&dl->cl_lock);
	if (dl->cl_chunk_idx == idx && dl->cl_chunk_obj != 0 &&
	    dl->cl_next_obj != 0) {
		*objp = dl->cl_chunk_obj;
		*nextp = dl->cl_next_obj;
		mutex_exit(&dl->cl_lock);
		return (0);
	}
	mutex_exit(&dl->cl_lock);

	err = cl_chunk_create(os, dl, idx, &obj);
	if (err == 0)
		err = cl_chunk_create(os, dl, idx + 1, &next);
	if (err != 0)
		return (err);

	mutex_enter(&dl->cl_lock);
	dl->cl_chunk_idx = idx;
	dl->cl_chunk_obj = obj;
	dl->cl_next_obj = next;
	mutex_exit(&dl->cl_lock);

	*objp = obj;
	*nextp = next;
	return (0);
}

/*
 * Advance the tail to newtail, freeing whole chunks that fall below it.
 * gap says the records are being dropped without being consumed, which
 * bumps the generation so consumers rescan instead of missing changes.
 * maxchunks bounds the work so the caller never stalls.
 */
static void
cl_trim(objset_t *os, changelog_t *dl, uint64_t newtail, boolean_t gap,
    uint_t maxchunks)
{
	uint64_t cpc = dl->cl_phys.clp_recs_per_chunk;

	for (uint_t done = 0; done < maxchunks; done++) {
		dmu_tx_t *tx;
		uint64_t tail, idx, obj, upto;
		boolean_t free_chunk;
		int err;

		mutex_enter(&dl->cl_lock);
		tail = dl->cl_phys.clp_tail;
		mutex_exit(&dl->cl_lock);

		if (newtail <= tail)
			return;

		idx = tail / cpc;
		upto = (idx + 1) * cpc;
		free_chunk = (upto <= newtail);
		if (!free_chunk)
			upto = newtail;

		obj = 0;
		if (free_chunk) {
			err = cl_chunk_lookup(os, dl, idx, &obj);
			if (err != 0 && err != ENOENT)
				return;
		}

		tx = dmu_tx_create(os);
		if (obj != 0) {
			dmu_tx_hold_free(tx, obj, 0, DMU_OBJECT_END);
			dmu_tx_hold_zap(tx, dl->cl_phys.clp_catalog_obj,
			    B_FALSE, NULL);
		}
		dmu_tx_hold_write(tx, dl->cl_hdr_obj, 0,
		    sizeof (changelog_phys_t));
		err = dmu_tx_assign(tx, DMU_TX_WAIT);
		if (err != 0) {
			dmu_tx_abort(tx);
			return;
		}

		if (obj != 0) {
			(void) dmu_object_free(os, obj, tx);
			(void) zap_remove_int(os,
			    dl->cl_phys.clp_catalog_obj, idx, tx);
			CL_STAT_BUMP(dlk_trims);
		}

		mutex_enter(&dl->cl_lock);
		if (upto > dl->cl_phys.clp_tail) {
			if (gap) {
				dl->cl_phys.clp_dropped +=
				    upto - dl->cl_phys.clp_tail;
				CL_STAT_ADD(dlk_dropped,
				    upto - dl->cl_phys.clp_tail);
			}
			dl->cl_phys.clp_tail = upto;
		}
		if (dl->cl_chunk_idx == idx) {
			dl->cl_chunk_idx = UINT64_MAX;
			dl->cl_chunk_obj = 0;
			dl->cl_next_obj = 0;
		}
		cl_phys_sync(os, dl, tx);
		mutex_exit(&dl->cl_lock);
		dmu_tx_commit(tx);

		if (!free_chunk)
			return;
	}
}

/*
 * Enforce the retention cap.  Called from the hold path, which runs
 * before the operation's own transaction is assigned; work is bounded to
 * one chunk so a metadata operation is never held up by GC.
 */
static void
cl_trim_overflow(objset_t *os, changelog_t *dl)
{
	uint64_t head, tail, newtail, cpc;

	mutex_enter(&dl->cl_lock);
	head = dl->cl_head;
	tail = dl->cl_phys.clp_tail;
	cpc = dl->cl_phys.clp_recs_per_chunk;
	mutex_exit(&dl->cl_lock);

	/*
	 * Allow one chunk of slack past the cap: the tail then moves a
	 * whole chunk at a time, so a saturated log costs one transaction
	 * per chunk rather than one per record.
	 */
	if (head - tail <= changelog_retention_recs + cpc)
		return;

	newtail = (tail / cpc + 1) * cpc;
	if (newtail > head)
		return;

	CL_STAT_BUMP(dlk_gaps);
	mutex_enter(&dl->cl_lock);
	dl->cl_phys.clp_generation++;
	mutex_exit(&dl->cl_lock);
	cl_trim(os, dl, newtail, B_TRUE, 1);
}

/* Write the header out from a transaction of our own. */
static void
cl_phys_flush(objset_t *os, changelog_t *dl)
{
	dmu_tx_t *tx;
	int err;

	if (!dl->cl_phys_dirty)
		return;

	tx = dmu_tx_create(os);
	dmu_tx_hold_write(tx, dl->cl_hdr_obj, 0, sizeof (changelog_phys_t));
	err = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (err != 0) {
		dmu_tx_abort(tx);
		return;
	}
	mutex_enter(&dl->cl_lock);
	dl->cl_phys_dirty = B_FALSE;
	cl_phys_sync(os, dl, tx);
	mutex_exit(&dl->cl_lock);
	dmu_tx_commit(tx);
}

/* Initialize the fields every record carries. */
static void
cl_rec_init(changelog_rec_t *rec, uint8_t op, dmu_tx_t *tx, uint64_t object,
    uint64_t parent)
{
	memset(rec, 0, sizeof (*rec));
	rec->clr_magic = CL_REC_MAGIC;
	rec->clr_op = op;
	rec->clr_txg = dmu_tx_get_txg(tx);
	rec->clr_time = gethrestime_sec();
	rec->clr_object = object;
	rec->clr_parent = parent;
}

/* Copy a name into the record, truncating past CL_NAME_MAX. */
static void
cl_rec_name(changelog_rec_t *rec, const char *name)
{
	size_t nlen;

	if (name == NULL)
		return;
	nlen = strlen(name);
	if (nlen >= CL_NAME_MAX) {
		nlen = CL_NAME_MAX - 1;
		rec->clr_flags |= CL_F_NAME_TRUNC;
	}
	memcpy(rec->clr_name, name, nlen);
	rec->clr_namelen = (uint16_t)nlen;
}

/*
 * Pack "old\0new" into the record's name field, truncating to fit and
 * keeping the old name first.  clr_namelen is the old name's length.
 */
static void
cl_rename_pack_name(changelog_rec_t *rec, const char *oldname,
    const char *newname)
{
	size_t olen = 0, nlen = 0;
	char *nm = rec->clr_name;

	if (oldname != NULL)
		olen = strlen(oldname);
	if (newname != NULL)
		nlen = strlen(newname);
	if (olen + nlen + 2 > CL_NAME_MAX) {
		if (olen > CL_NAME_MAX / 2 - 1)
			olen = CL_NAME_MAX / 2 - 1;
		if (olen + nlen + 2 > CL_NAME_MAX)
			nlen = CL_NAME_MAX - olen - 2;
	}
	if (oldname != NULL)
		memcpy(nm, oldname, olen);
	if (newname != NULL)
		memcpy(nm + olen + 1, newname, nlen);
	rec->clr_namelen = (uint16_t)olen;
}

/*
 * Queue a record for the txg sync flusher.  Stamps enq_txg once per txg,
 * in the change's own txg, so mount can detect a crash that lost the
 * queued tail by comparing it with the flusher's covered stamp.
 */
static void
cl_enqueue(objset_t *os, changelog_t *dl, changelog_qent_t *qe, dmu_tx_t *tx)
{
	changelog_rec_t *rec = &qe->ql_rec;

	mutex_enter(&dl->cl_qlock);
	list_insert_tail(&dl->cl_queue, qe);
	dl->cl_qlen++;
	mutex_exit(&dl->cl_qlock);

	if (dl->cl_phys.clp_enq_txg != rec->clr_txg) {
		mutex_enter(&dl->cl_lock);
		dl->cl_phys.clp_enq_txg = rec->clr_txg;
		cl_phys_sync(os, dl, tx);
		mutex_exit(&dl->cl_lock);
	}
}

/*
 * Append a built record at the head, honoring the reservation taken at
 * hold time.  On rollover past the reservation the record is dropped and
 * a generation gap declared instead of writing outside what was held.
 */
static void
cl_append_sync(objset_t *os, dmu_tx_t *tx, changelog_t *dl,
    changelog_res_t *res, changelog_rec_t *rec)
{
	uint64_t seq, idx, off, obj, cpc;

	cpc = dl->cl_phys.clp_recs_per_chunk;

	mutex_enter(&dl->cl_lock);
	seq = dl->cl_head;
	idx = seq / cpc;
	off = (seq % cpc) * CL_REC_SIZE;
	if (idx == res->clr_chunk_idx)
		obj = res->clr_chunk_obj;
	else if (idx == res->clr_chunk_idx + 1)
		obj = res->clr_next_obj;
	else
		obj = 0;

	if (obj == 0) {
		/* Rolled over past the reservation: declare a gap. */
		dl->cl_phys.clp_generation++;
		dl->cl_phys.clp_dropped++;
		dl->cl_phys_dirty = B_TRUE;
		mutex_exit(&dl->cl_lock);
		kmem_cache_free(cl_rec_cache, rec);
		CL_STAT_BUMP(dlk_errors);
		CL_STAT_BUMP(dlk_gaps);
		return;
	}

	rec->clr_seq = seq;
	dmu_write(os, obj, off, CL_REC_SIZE, rec, tx, DMU_READ_NO_PREFETCH);
	dl->cl_head = seq + 1;
	if (dl->cl_waiters > 0)
		cv_broadcast(&dl->cl_cv);

	/*
	 * Sync records are durable in the change's own txg: watermark and
	 * covered stamp advance together, once per txg.
	 */
	if (dl->cl_phys.clp_enq_txg != rec->clr_txg) {
		dl->cl_phys.clp_enq_txg = rec->clr_txg;
		dl->cl_phys.clp_covered_txg = rec->clr_txg;
		cl_phys_sync(os, dl, tx);
	}
	mutex_exit(&dl->cl_lock);

	kmem_cache_free(cl_rec_cache, rec);
	CL_STAT_BUMP(dlk_records);
}

void
zfs_changelog_tx_hold(objset_t *os, dmu_tx_t *tx, changelog_res_t *res)
{
	changelog_t *dl;
	uint64_t head, idx, obj, next;
	int err;

	res->clr_held = B_FALSE;
	res->clr_chunk_idx = 0;
	res->clr_chunk_obj = 0;
	res->clr_next_obj = 0;

	if (os == NULL || os->os_changelog == 0)
		return;
	if (changelog_async)	/* no reservation needed on the op path */
		return;
	if (dmu_objset_type(os) != DMU_OST_ZFS)
		return;

	dl = cl_state(os);
	if ((err = cl_open(os, dl)) != 0) {
		zfs_dbgmsg("changelog: cl_open failed err=%d", err);
		CL_STAT_BUMP(dlk_errors);
		return;
	}

	cl_trim_overflow(os, dl);
	if (dl->cl_phys_dirty)
		cl_phys_flush(os, dl);

	mutex_enter(&dl->cl_lock);
	head = dl->cl_head;
	mutex_exit(&dl->cl_lock);

	idx = head / dl->cl_phys.clp_recs_per_chunk;
	if ((err = cl_chunks_ready(os, dl, idx, &obj, &next)) != 0) {
		zfs_dbgmsg("changelog: chunks_ready idx=%llu failed err=%d",
		    (u_longlong_t)idx, err);
		CL_STAT_BUMP(dlk_errors);
		return;
	}

	/*
	 * The exact slot is picked while appending, so reserve an append
	 * on the chunk that is current now and on the one after it: by
	 * then concurrent writers may have rolled the log over.
	 */
	dmu_tx_hold_append(tx, obj, 0, CL_REC_SIZE);
	dmu_tx_hold_append(tx, next, 0, CL_REC_SIZE);

	res->clr_held = B_TRUE;
	res->clr_chunk_idx = idx;
	res->clr_chunk_obj = obj;
	res->clr_next_obj = next;
}

void
zfs_changelog_record(objset_t *os, dmu_tx_t *tx, changelog_res_t *res,
    uint8_t op, uint64_t object, uint64_t parent, const char *name)
{
	changelog_t *dl;
	changelog_rec_t *rec;

	if (os == NULL || os->os_changelog == 0)
		return;
	dl = os->os_changelog_state;

	if (changelog_async) {
		changelog_qent_t *qe;

		if (changelog_test_nowrite)
			return;
		if (dl == NULL)
			dl = cl_state(os);
		if (!dl->cl_ready && cl_open(os, dl) != 0) {
			CL_STAT_BUMP(dlk_errors);
			return;
		}
		qe = kmem_cache_alloc(cl_qent_cache, KM_SLEEP);
		cl_rec_init(&qe->ql_rec, op, tx, object, parent);
		cl_rec_name(&qe->ql_rec, name);
		cl_enqueue(os, dl, qe, tx);
		return;
	}
	if (res == NULL || !res->clr_held)
		return;
	res->clr_held = B_FALSE;

	if (changelog_test_nowrite)	/* perf attribution knob, not API */
		return;

	if (dl == NULL || !dl->cl_ready)
		return;

	rec = kmem_cache_alloc(cl_rec_cache, KM_SLEEP);
	cl_rec_init(rec, op, tx, object, parent);
	cl_rec_name(rec, name);
	cl_append_sync(os, tx, dl, res, rec);
}

/* Forward decls for the async flusher. */
#ifdef _KERNEL
static void cl_flush_task(void *);
static void cl_flush_objset(objset_t *);
#endif

#ifdef _KERNEL
/*
 * Called from the txg sync path with NO side effects: it only checks
 * the queue and dispatches.  The write itself runs in open context
 * with its own transaction - dirtying dataset dnodes here, in syncing
 * context, corrupts the dirty-dnodes multilist (the kernel panic the
 * first version of this hook caused).
 */
void
zfs_changelog_flush_notify(objset_t *os)
{
	char *name;
	changelog_t *dl = os->os_changelog_state;

	if (dl == NULL || !dl->cl_ready)
		return;

	mutex_enter(&dl->cl_qlock);
	boolean_t have = (dl->cl_qlen > 0);
	mutex_exit(&dl->cl_qlock);
	if (!have)
		return;

	name = kmem_strdup(dl->cl_dsname);
	if (name == NULL)
		return;
	if (taskq_dispatch(system_taskq, cl_flush_task, name,
	    TQ_SLEEP) == TASKQID_INVALID)
		kmem_strfree(name);
}

/*
 * The flusher runs in open context with its own transaction - the same
 * safety model as the ioctl writers.  Records become durable with the
 * flush tx's txg, which may be one past the change's txg: the
 * documented async contract.
 */
static void
cl_flush_task(void *arg)
{
	char *dsname = arg;
	objset_t *os;
	zfsvfs_t *zfsvfs;

	if (zfs_changelog_os_hold(dsname, &zfsvfs, &os) != 0) {
		kmem_strfree(dsname);
		return;
	}
	cl_flush_objset(os);
	zfs_changelog_os_rele(zfsvfs, os);
	kmem_strfree(dsname);
}

/*
 * Splice the queue and write the batch with one transaction.  On chunk
 * rollover past the reservation, the rest stays queued for the next
 * dispatch (one per txg, so progress is guaranteed).
 */
static void
cl_flush_objset(objset_t *os)
{
	changelog_t *dl = os->os_changelog_state;
	changelog_qent_t *qe;
	dmu_tx_t *tx;
	list_t batch;
	uint64_t seq, idx, off, obj, next, cpc, wrote = 0, maxtxg = 0;
	boolean_t retry = B_TRUE;

	if (dl == NULL || !dl->cl_ready)
		return;

	mutex_enter(&dl->cl_qlock);
	if (dl->cl_qlen == 0 || dl->cl_flushing) {
		mutex_exit(&dl->cl_qlock);
		return;
	}
	dl->cl_flushing = B_TRUE;
	list_create(&batch, sizeof (changelog_qent_t),
	    offsetof(changelog_qent_t, ql_link));
	list_move_tail(&batch, &dl->cl_queue);
	mutex_exit(&dl->cl_qlock);

	cpc = dl->cl_phys.clp_recs_per_chunk;
retry:
	mutex_enter(&dl->cl_lock);
	seq = dl->cl_head;
	mutex_exit(&dl->cl_lock);

	idx = seq / cpc;
	if (cl_chunks_ready(os, dl, idx, &obj, &next) != 0)
		goto out;

	tx = dmu_tx_create(os);
	dmu_tx_hold_append(tx, obj, 0, CL_REC_SIZE);
	dmu_tx_hold_append(tx, next, 0, CL_REC_SIZE);
	dmu_tx_hold_write(tx, dl->cl_hdr_obj, 0, sizeof (changelog_phys_t));
	if (dmu_tx_assign(tx, DMU_TX_WAIT) != 0) {
		dmu_tx_abort(tx);
		goto out;
	}

	mutex_enter(&dl->cl_lock);
	if (dl->cl_head != seq) {
		mutex_exit(&dl->cl_lock);
		dmu_tx_commit(tx);
		if (retry) {
			retry = B_FALSE;
			goto retry;
		}
		goto out;
	}

	while ((qe = list_remove_head(&batch)) != NULL) {
		if (seq / cpc != idx) {
			/* Past the reservation: leave the rest queued. */
			mutex_enter(&dl->cl_qlock);
			list_move_tail(&dl->cl_queue, &batch);
			mutex_exit(&dl->cl_qlock);
			mutex_exit(&dl->cl_lock);
			dmu_tx_commit(tx);
			goto done;
		}
		off = (seq % cpc) * CL_REC_SIZE;
		qe->ql_rec.clr_seq = seq++;
		if (qe->ql_rec.clr_txg > maxtxg)
			maxtxg = qe->ql_rec.clr_txg;
		dmu_write(os, obj, off, CL_REC_SIZE, &qe->ql_rec, tx,
		    DMU_READ_NO_PREFETCH);
		wrote++;
	}

	dl->cl_head = seq;
	if (wrote > 0)
		dl->cl_phys.clp_covered_txg = maxtxg;
	cl_phys_sync(os, dl, tx);
	if (dl->cl_waiters > 0)
		cv_broadcast(&dl->cl_cv);
	mutex_exit(&dl->cl_lock);
	dmu_tx_commit(tx);
	CL_STAT_ADD(dlk_records, wrote);
	CL_STAT_BUMP(dlk_flushes);
	goto done;

out:
	/* Put everything back so the next dispatch retries. */
	mutex_enter(&dl->cl_qlock);
	list_move_tail(&dl->cl_queue, &batch);
	mutex_exit(&dl->cl_qlock);
done:
	mutex_enter(&dl->cl_qlock);
	dl->cl_flushing = B_FALSE;
	mutex_exit(&dl->cl_qlock);
	list_destroy(&batch);
}

#else
void
zfs_changelog_flush_notify(objset_t *os)
{
	(void) os;
}
#endif	/* _KERNEL */


ZFS_MODULE_PARAM(zfs_changelog, changelog_, retention_recs, U64, ZMOD_RW,
	"Records kept per dataset before the oldest are dropped");
ZFS_MODULE_PARAM(zfs_changelog, changelog_, chunk_recs, U64, ZMOD_RW,
	"Record slots per chunk object, fixed when a log is created");
ZFS_MODULE_PARAM(zfs_changelog, changelog_, test_nowrite, UINT, ZMOD_RW,
	"Perf attribution only: keep holds but skip record writes");
ZFS_MODULE_PARAM(zfs_changelog, changelog_, async, UINT, ZMOD_RW,
	"Op path only queues records; the txg sync path flushes them");


/*
 * One record per rename, with both names packed as "old\0new" in the
 * name field and the old parent in clr_spare (CL_F_RENAME2).
 */
void
zfs_changelog_record_rename(objset_t *os, dmu_tx_t *tx, changelog_res_t *res,
    uint64_t object, uint64_t oldparent, const char *oldname,
    uint64_t newparent, const char *newname)
{
	changelog_t *dl;
	changelog_rec_t *rec;

	if (os == NULL || os->os_changelog == 0)
		return;
	dl = os->os_changelog_state;

	if (changelog_async) {
		changelog_qent_t *qe;

		if (changelog_test_nowrite)
			return;
		if (dl == NULL)
			dl = cl_state(os);
		if (!dl->cl_ready && cl_open(os, dl) != 0) {
			CL_STAT_BUMP(dlk_errors);
			return;
		}
		qe = kmem_cache_alloc(cl_qent_cache, KM_SLEEP);
		cl_rec_init(&qe->ql_rec, CL_OP_RENAME, tx, object, newparent);
		qe->ql_rec.clr_flags |= CL_F_RENAME2;
		qe->ql_rec.clr_spare = oldparent;
		cl_rename_pack_name(&qe->ql_rec, oldname, newname);
		cl_enqueue(os, dl, qe, tx);
		return;
	}
	if (res == NULL || !res->clr_held)
		return;
	res->clr_held = B_FALSE;

	if (changelog_test_nowrite)
		return;

	if (dl == NULL || !dl->cl_ready)
		return;

	rec = kmem_cache_alloc(cl_rec_cache, KM_SLEEP);
	cl_rec_init(rec, CL_OP_RENAME, tx, object, newparent);
	rec->clr_flags |= CL_F_RENAME2;
	rec->clr_spare = oldparent;
	cl_rename_pack_name(rec, oldname, newname);
	cl_append_sync(os, tx, dl, res, rec);
}
