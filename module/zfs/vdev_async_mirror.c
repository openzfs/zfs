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
 */

/*
 * Copyright (c) 2026, John White.
 */

/* Replay committed txgs to a lagging child while holding its frees. */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_async_mirror.h>
#include <sys/vdev_async_mirror_impl.h>
#include <sys/uberblock_impl.h>
#include <sys/metaslab_impl.h>
#include <sys/zap.h>
#include <sys/zfeature.h>
#include <sys/dmu.h>
#include <sys/dmu_tx.h>
#include <sys/zio.h>
#include <sys/abd.h>
#include <sys/txg.h>
#include <sys/zthr.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_synctask.h>
#include <sys/dsl_scan.h>
#include <sys/dsl_dataset.h>
#include <sys/dmu_traverse.h>
#include <sys/dmu_objset.h>
#include <sys/dnode.h>
#include <sys/arc.h>
#include <sys/zio_checksum.h>
#include <sys/zil.h>

/* The block size of the space map of a record. */
#define	ASYNC_MIRROR_RECORD_BLKSZ	SPA_OLD_MAXBLOCKSIZE

/* The block shift of the record ZAP, as for a fat ZAP by default. */
#define	ASYNC_MIRROR_ZAP_SHIFT	14

/* About the most bytes of copies that one commit covers. */
#define	ASYNC_MIRROR_COMMIT_BYTES	(256ULL << 20)

/* Test controls can suspend copy or release and delay copy I/O. */
int zfs_vdev_async_mirror_suspend_copy = 0;
static int zfs_vdev_async_mirror_suspend_release = 0;
uint_t zfs_vdev_async_mirror_copy_delay_ms = 0;
static uint_t zfs_vdev_async_mirror_inject_dtl_delay = 0;
static uint_t zfs_vdev_async_mirror_inject_dtl_label_flush = 0;

/* Back-pressure: writes slow down near these limits, and wait above them. */
static uint64_t zfs_vdev_async_mirror_max_pending_bytes = 2ULL << 30;
static uint64_t zfs_vdev_async_mirror_max_pending_txgs = 256;

/*
 * The most time, in milliseconds, that one release sync task spends on the
 * entries of the records, and the most SM_FREE entries that it releases.  0
 * is no limit.  The time is the limit of the sync thread; the entry limit
 * is for tests: a small one can release less than the txg of the release
 * holds anew, so such a release is not forced again and again; see
 * async_mirror_held_urgent().
 */
static uint_t zfs_vdev_async_mirror_release_ms = 250;
static uint64_t zfs_vdev_async_mirror_release_limit = 0;

/*
 * Ready held space, as a percentage of the space of the pool, at which the
 * write throttle reaches its most delay; 0 turns this off.  See "Held
 * space" at the top of this file.
 */
static uint_t zfs_vdev_async_mirror_max_held_percent = 25;

/*
 * Held space that waits for the lagging child, as a percentage of the space
 * of the pool, at which the async mirror gives up on that child; 0 turns
 * this off.  The held space must stay there for
 * zfs_vdev_async_mirror_held_limit_secs seconds, with a child that does not
 * pass the txg of the start of that time.  See "The give-up" at the top of
 * this file.
 */
static uint_t zfs_vdev_async_mirror_held_limit_percent = 50;
static uint_t zfs_vdev_async_mirror_held_limit_secs = 60;

/*
 * The async mirror also gives up on the lagging child when the pool is
 * short of space and held space waits for the child; 0 turns this off.
 * The pool is short of space when the datasets can use less than this
 * percentage of the space of the pool, and less than a quarter of the slop
 * space; see async_mirror_giveup_margin().
 */
static uint_t zfs_vdev_async_mirror_held_free_percent = 2;

/*
 * The seconds with no progress of the copy, while its backlog is above a
 * limit, after which the async mirror gives up on the lagging child; 0
 * turns this off, and writes then wait for a stalled child without end.
 */
static uint_t zfs_vdev_async_mirror_stall_secs = 30;

/* How often the give-up timer checks a stalled or space-starved pool. */
#define	ASYNC_MIRROR_GIVEUP_TICK	SEC2NSEC(1)

/*
 * The most bytes that an import allocates for the list of the records, and
 * for the list of the deferred blocks; 0 is no limit.  The default is room
 * for about 4 million records, which a pool reaches only after months with
 * a lagging child that does not advance.  The limit keeps a damaged pool
 * from an allocation without end.  An import that needs more fails with a
 * message that names this tunable, and works after the limit is raised.
 */
static uint64_t zfs_vdev_async_mirror_load_max_bytes = 1ULL << 30;

/*
 * The most txgs above the first txg of an import that the DTLs for the
 * claimed logs can reach; see vdev_async_mirror_claimed().
 */
#define	ASYNC_MIRROR_CLAIM_TXG_SLACK	(1ULL << 20)

/* The most bytes of the copy in flight; see "The I/O of the copy". */
uint64_t zfs_vdev_async_mirror_copy_limit = 64ULL << 20;

/*
 * The most bytes of memory for the blocks of a commit that wait to be sorted
 * by their offset; see "The I/O of the copy".  A block uses about 300 bytes.
 */
uint64_t zfs_vdev_async_mirror_sort_limit = 64ULL << 20;

/*
 * A read error during the copy of a txg can pass: the leader drops off the
 * bus for a few seconds, or its controller resets.  So the copy of the txg
 * is tried again, up to zfs_vdev_async_mirror_read_retries times, with
 * ASYNC_MIRROR_RETRY_MS between two tries.  Only then is the read error
 * final.
 */
uint_t zfs_vdev_async_mirror_read_retries = 10;


_Static_assert(sizeof (uberblock_t) <= sizeof (uint64_t) *
	ASYNC_MIRROR_REC_UB_WORDS, "uberblock_t must fit in amr_ub");
_Static_assert(P2PHASE(sizeof (uberblock_t), sizeof (uint64_t)) == 0,
	"uberblock_t must use whole words in amr_ub");
_Static_assert(ASYNC_MIRROR_REC_WORDS == 3 + ASYNC_MIRROR_REC_UB_WORDS,
	"async_mirror_rec_phys_t must have no padding");
_Static_assert(ASYNC_MIRROR_CHILD_WORDS == 24,
	"async_mirror_child_phys_t is 24 words on disk");
_Static_assert(ASYNC_MIRROR_LINEAGE_WORDS == 16,
	"async_mirror_lineage_phys_t is 16 words on disk");

/* Log-device frees do not affect pool space. */
static uint64_t
async_mirror_held_dspace(vdev_t *vd, uint64_t size)
{
	return (vd->vdev_islog ? 0 : vdev_deflated_space(vd, size));
}

/* Caller holds am_lock. */
void
async_mirror_update_stats(vdev_async_mirror_t *am)
{
	spa_async_mirror_stats_t *sams = &am->am_spa->spa_async_mirror_stats;

	ASSERT(MUTEX_HELD(&am->am_lock));

	sams->sams_active = async_mirror_has_child(am->am_state);
	sams->sams_failed = (am->am_state == AMS_FROZEN ||
	    am->am_state == AMS_FAILED || am->am_failed_guid != 0);
	sams->sams_applied_txg = am->am_applied_txg;
	sams->sams_dtl_txg = am->am_dtl_txg;
	sams->sams_dtl_child_txg = am->am_dtl_child_txg;
	sams->sams_repair_inflight = am->am_repair_count;
	sams->sams_pending_bytes = am->am_pending_bytes;
	sams->sams_pending_txgs = am->am_pending_txgs;
	sams->sams_held_bytes = am->am_held_bytes;
	sams->sams_frozen = (am->am_state == AMS_FROZEN);
	sams->sams_frozen_txg = sams->sams_frozen ? am->am_applied_txg : 0;
	sams->sams_held_limit = (am->am_giveup &&
	    (am->am_state == AMS_FROZEN || am->am_state == AMS_FAILED));
	sams->sams_giveups = am->am_giveups;
	sams->sams_giveup_reason = sams->sams_held_limit ?
	    am->am_giveup_why : 0;
	sams->sams_freeze_read = (sams->sams_frozen && am->am_freeze_read);
	spa_async_mirror_stats_update(am->am_spa);
}

/* Caller holds am_lock; am_pass_txg fences lockless write readers. */
void
async_mirror_set_state(vdev_async_mirror_t *am, async_mirror_state_t state)
{
	ASSERT(MUTEX_HELD(&am->am_lock));
	ASSERT(!async_mirror_has_child(state) || am->am_lagging != NULL);

	/* A requested switch off is not a failure. */
	if (state == AMS_FAILED && am->am_lagging != NULL && !am->am_off)
		am->am_failed_guid = am->am_lagging->vdev_guid;
	else if (state == AMS_STARTING || state == AMS_ACTIVE)
		am->am_failed_guid = 0;
	if (am->am_lagging != NULL) {
		atomic_store_32(&am->am_lagging->vdev_async_state,
		    async_mirror_has_child(state) ? state : AMS_OFF);
	}
	if (!async_mirror_has_child(state)) {
		am->am_lagging = NULL;
		am->am_leader = NULL;
	}
	if (state != AMS_ACTIVE)
		atomic_store_32(&am->am_defer_pending, B_FALSE);
	atomic_store_32(&am->am_state, state);
	spa_notify_waiters(am->am_spa);
	cv_broadcast(&am->am_drain_cv);
}

/* Keep records until a stopped copy is durable. */
static boolean_t
async_mirror_tracking(vdev_async_mirror_t *am)
{
	uint32_t state = atomic_load_32(&am->am_state);

	return (async_mirror_copying(state) ||
	    ((state == AMS_FROZEN || state == AMS_FAILED) &&
	    !async_mirror_durable(am, am->am_halt_txg)));
}

/* Allocate the in-memory part of a record. */
static async_mirror_txg_t *
async_mirror_txg_alloc(uint64_t txg, uint64_t obj)
{
	async_mirror_txg_t *at = kmem_zalloc(sizeof (*at), KM_SLEEP);

	ASSERT3U(txg, !=, 0);
	ASSERT3U(obj, !=, 0);

	at->at_txg = txg;
	at->at_obj = obj;
	/* A zeroed link counts as active on Linux; see list_link_active(). */
	list_link_init(&at->at_node);
	async_mirror_defer_list_create(&at->at_defer);
	return (at);
}

/* Return the child-entry object, or 0 if the pool has none. */
static int
async_mirror_child_obj(objset_t *mos, uint64_t *objp)
{
	int error = zap_lookup(mos, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_ASYNC_MIRROR_CHILD, sizeof (uint64_t), 1, objp);

	if (error == ENOENT) {
		*objp = 0;
		error = 0;
	}
	return (error);
}

/* Return EINVAL for a size mismatch, ENOTSUP for unknown fields, or the */
/* first read or callback error. */
int
async_mirror_child_iterate(objset_t *mos, async_mirror_child_cb_t *cb,
    void *arg)
{
	async_mirror_child_phys_t amc;
	zap_cursor_t zc;
	uint64_t obj;
	int error = async_mirror_child_obj(mos, &obj);

	if (error != 0 || obj == 0)
		return (error);

	zap_attribute_t *za = zap_attribute_alloc();

	for (zap_cursor_init(&zc, mos, obj);
	    (error = zap_cursor_retrieve(&zc, za)) == 0;
	    zap_cursor_advance(&zc)) {
		if (za->za_integer_length != sizeof (uint64_t) ||
		    za->za_num_integers != ASYNC_MIRROR_CHILD_WORDS) {
			error = SET_ERROR(EINVAL);
			break;
		}
		const uint64_t *guid = (const uint64_t *)za->za_name;

		error = zap_lookup_uint64(mos, obj, guid, 1, sizeof (uint64_t),
		    ASYNC_MIRROR_CHILD_WORDS, &amc);
		if (error == 0 &&
		    (amc.amc_version != ASYNC_MIRROR_CHILD_VERSION ||
		    amc.amc_flags == 0 ||
		    (amc.amc_flags & ~ASYNC_MIRROR_CHILD_FLAGS) != 0 ||
		    amc.amc_set != ASYNC_MIRROR_CHILD_SET ||
		    (((amc.amc_flags & ASYNC_MIRROR_CHILD_DTL_PENDING) != 0) !=
		    (amc.amc_reserved[ASYNC_MIRROR_CHILD_DTL_TXG_WORD] != 0))))
			error = SET_ERROR(ENOTSUP);
		if (error == 0)
			error = cb(*guid, &amc, arg);
		if (error != 0)
			break;
	}
	zap_cursor_fini(&zc);
	zap_attribute_free(za);
	return (error == ENOENT ? 0 : error);
}

/* Read an entry in sync context, or initialize *amc when none exists. */
boolean_t
async_mirror_child_get(spa_t *spa, uint64_t guid,
    async_mirror_child_phys_t *amc)
{
	objset_t *mos = spa_meta_objset(spa);
	uint64_t obj;
	int error;

	VERIFY0(async_mirror_child_obj(mos, &obj));
	error = (obj == 0) ? ENOENT : zap_lookup_uint64(mos, obj, &guid, 1,
	    sizeof (uint64_t), ASYNC_MIRROR_CHILD_WORDS, amc);
	if (error == 0)
		return (B_TRUE);
	VERIFY3S(error, ==, ENOENT);
	memset(amc, 0, sizeof (*amc));
	amc->amc_version = ASYNC_MIRROR_CHILD_VERSION;
	amc->amc_set = ASYNC_MIRROR_CHILD_SET;
	return (B_FALSE);
}

/* Write or remove an entry in sync context; each entry holds a feature ref. */
void
async_mirror_child_put(spa_t *spa, uint64_t guid,
    async_mirror_child_phys_t *amc, dmu_tx_t *tx)
{
	objset_t *mos = spa_meta_objset(spa);
	uint64_t obj, isize, words;

	ASSERT(dmu_tx_is_syncing(tx));
	ASSERT3U(guid, !=, 0);
	ASSERT3U(amc->amc_version, ==, ASYNC_MIRROR_CHILD_VERSION);
	ASSERT0(amc->amc_flags & ~ASYNC_MIRROR_CHILD_FLAGS);

	VERIFY0(async_mirror_child_obj(mos, &obj));
	boolean_t exists = (obj != 0 && zap_length_uint64(mos, obj, &guid, 1,
	    &isize, &words) == 0);

	if (amc->amc_flags == 0) {
		uint64_t count;

		if (!exists)
			return;
		VERIFY0(zap_remove_uint64(mos, obj, &guid, 1, tx));
		spa_feature_decr(spa, SPA_FEATURE_ASYNC_MIRROR, tx);
		VERIFY0(zap_count(mos, obj, &count));
		if (count == 0) {
			VERIFY0(zap_destroy(mos, obj, tx));
			VERIFY0(zap_remove(mos, DMU_POOL_DIRECTORY_OBJECT,
			    DMU_POOL_ASYNC_MIRROR_CHILD, tx));
		}
		return;
	}

	if ((amc->amc_flags & ASYNC_MIRROR_CHILD_LAGGING) == 0)
		amc->amc_start_txg = 0;
	if ((amc->amc_flags & ASYNC_MIRROR_CHILD_OFF) == 0)
		amc->amc_off_txg = 0;
	if ((amc->amc_flags & ASYNC_MIRROR_CHILD_FROZEN) == 0) {
		amc->amc_frozen_txg = amc->amc_frozen_reason = 0;
		amc->amc_frozen_from = 0;
	}
	amc->amc_reserved_frozen_flags = 0;
	amc->amc_reserved_giveups = 0;
	amc->amc_reserved_frozen_time = 0;
	amc->amc_reserved_12 = 0;
	if ((amc->amc_flags & ASYNC_MIRROR_CHILD_DTL_PENDING) == 0)
		amc->amc_reserved[ASYNC_MIRROR_CHILD_DTL_TXG_WORD] = 0;
	else
		VERIFY3U(amc->amc_reserved[ASYNC_MIRROR_CHILD_DTL_TXG_WORD], !=,
		    0);
	if (obj == 0) {
		obj = zap_create_flags(mos, 0,
		    ZAP_FLAG_HASH64 | ZAP_FLAG_UINT64_KEY, DMU_OTN_ZAP_METADATA,
		    ASYNC_MIRROR_ZAP_SHIFT, ASYNC_MIRROR_ZAP_SHIFT, DMU_OT_NONE,
		    0, tx);
		VERIFY0(zap_add(mos, DMU_POOL_DIRECTORY_OBJECT,
		    DMU_POOL_ASYNC_MIRROR_CHILD, sizeof (uint64_t), 1, &obj,
		    tx));
	}
	VERIFY0(zap_update_uint64(mos, obj, &guid, 1, sizeof (uint64_t),
	    ASYNC_MIRROR_CHILD_WORDS, amc, tx));
	if (!exists)
		spa_feature_incr(spa, SPA_FEATURE_ASYNC_MIRROR, tx);
}

/* Set or clear flags that have no associated data fields. */
void
async_mirror_child_flags(spa_t *spa, uint64_t guid, uint64_t flags_on,
    uint64_t flags_off, dmu_tx_t *tx)
{
	async_mirror_child_phys_t amc;

	if (guid == 0)
		return;
	(void) async_mirror_child_get(spa, guid, &amc);
	amc.amc_flags = (amc.amc_flags | flags_on) & ~flags_off;
	async_mirror_child_put(spa, guid, &amc, tx);
}

static int
async_mirror_child_replace_cb(uint64_t guid,
    const async_mirror_child_phys_t *amc, void *arg)
{
	uint64_t *guidp = arg;

	if ((amc->amc_flags & ASYNC_MIRROR_CHILD_REPLACE) != 0 && *guidp == 0)
		*guidp = guid;
	return (0);
}

/* Find the leaf with a pending replacement; return ENOENT if there is none. */
int
async_mirror_child_replace_lookup(spa_t *spa, uint64_t *guidp)
{
	int error;

	*guidp = 0;
	error = async_mirror_child_iterate(spa_meta_objset(spa),
	    async_mirror_child_replace_cb, guidp);
	if (error == 0 && *guidp == 0)
		error = ENOENT;
	return (error);
}

/* Caller holds am_lock. */
static void
async_mirror_entry_guid_update(vdev_async_mirror_t *am)
{
	ASSERT(MUTEX_HELD(&am->am_lock));

	if (am->am_zap_obj == 0 && !am->am_frozen_entry &&
	    !am->am_off_entry && am->am_dtl_child_guid == 0)
		am->am_entry_guid = 0;
}

/* Find or create this txg's record. The sync thread calls this. */
static async_mirror_txg_t *
async_mirror_record_get(vdev_async_mirror_t *am, dmu_tx_t *tx)
{
	spa_t *spa = am->am_spa;
	objset_t *mos = spa_meta_objset(spa);
	uint64_t txg = dmu_tx_get_txg(tx);
	async_mirror_txg_t *at = list_tail(&am->am_txgs);

	ASSERT(dsl_pool_sync_context(dmu_objset_pool(mos)));
	ASSERT3U(txg, ==, spa_syncing_txg(spa));
	ASSERT3P(am->am_lagging, !=, NULL);

	if (at != NULL && at->at_txg == txg)
		return (at);
	ASSERT(at == NULL || at->at_txg < txg);

	if (am->am_zap_obj == 0) {
		uint64_t zap = zap_create_flags(mos, 0,
		    ZAP_FLAG_HASH64 | ZAP_FLAG_UINT64_KEY, DMU_OTN_ZAP_METADATA,
		    ASYNC_MIRROR_ZAP_SHIFT, ASYNC_MIRROR_ZAP_SHIFT, DMU_OT_NONE,
		    0, tx);

		VERIFY0(zap_add(mos, DMU_POOL_DIRECTORY_OBJECT,
		    DMU_POOL_ASYNC_MIRROR_ZAP, sizeof (uint64_t), 1, &zap, tx));
		spa_feature_incr(spa, SPA_FEATURE_ASYNC_MIRROR, tx);

		async_mirror_child_phys_t amc;
		uint64_t guid = am->am_lagging->vdev_guid;

		ASSERT(am->am_entry_guid == 0 || am->am_entry_guid == guid);
		(void) async_mirror_child_get(spa, guid, &amc);
		amc.amc_flags |= ASYNC_MIRROR_CHILD_LAGGING;
		amc.amc_start_txg = txg;
		amc.amc_applied_txg = am->am_applied_txg;
		async_mirror_child_put(spa, guid, &amc, tx);

		mutex_enter(&am->am_lock);
		am->am_zap_obj = zap;
		am->am_entry_guid = guid;
		mutex_exit(&am->am_lock);
	}

	uint64_t obj = space_map_alloc(mos, ASYNC_MIRROR_RECORD_BLKSZ, tx);
	async_mirror_rec_phys_t rec = {
		.amr_version = ASYNC_MIRROR_REC_VERSION,
		.amr_sm_obj = obj,
	};

	VERIFY3U(obj, !=, 0);
	memcpy(rec.amr_ub, &am->am_last_ub, sizeof (uberblock_t));
	VERIFY0(zap_add_uint64(mos, am->am_zap_obj, &txg, 1,
	    sizeof (uint64_t), ASYNC_MIRROR_REC_WORDS, &rec, tx));

	at = async_mirror_txg_alloc(txg, obj);
	/* Sync tasks run before metaslabs sync. */
	at->at_release = (am->am_release_only_txg == txg);
	/* Each entry carries a vdev id. */
	VERIFY0(space_map_open(&at->at_sm, mos, obj, 0, UINT64_MAX,
	    SPA_MINBLOCKSHIFT));

	mutex_enter(&am->am_lock);
	list_insert_tail(&am->am_txgs, at);
	mutex_exit(&am->am_lock);
	ASSERT3P(at->at_sm, !=, NULL);
	ASSERT(!at->at_ready);
	return (at);
}

/* Caller holds am_lock or runs in sync context. */
static boolean_t
async_mirror_frozen_clear_due(vdev_async_mirror_t *am)
{
	ASSERT(MUTEX_HELD(&am->am_lock) ||
	    dsl_pool_sync_context(spa_get_dsl(am->am_spa)));

	return (am->am_frozen_entry && am->am_state != AMS_FROZEN &&
	    (am->am_state == AMS_DRAINING || am->am_dtl_txg != 0 ||
	    async_mirror_copying(am->am_state)));
}

/* Clear an ended freeze in the first MOS txg. */
static void
async_mirror_frozen_clear(vdev_async_mirror_t *am, dmu_tx_t *tx)
{
	spa_t *spa = am->am_spa;

	if (!async_mirror_frozen_clear_due(am))
		return;
	async_mirror_child_flags(spa, am->am_entry_guid, 0,
	    ASYNC_MIRROR_CHILD_FROZEN, tx);
	mutex_enter(&am->am_lock);
	am->am_frozen_entry = B_FALSE;
	am->am_frozen_stale = B_FALSE;
	async_mirror_entry_guid_update(am);
	mutex_exit(&am->am_lock);
	spa_history_log_internal(spa, "async mirror", tx,
	    "the freeze of the lagging child is cleared");
}

/* Record every MOS txg while replay is active. */
void
vdev_async_mirror_sync_record(spa_t *spa, dmu_tx_t *tx)
{
	vdev_async_mirror_t *am = spa->spa_async_mirror;

	ASSERT(dsl_pool_sync_context(dmu_objset_pool(spa_meta_objset(spa))));

	if (am == NULL)
		return;
	async_mirror_frozen_clear(am, tx);
	if (!async_mirror_tracking(am))
		return;
	(void) async_mirror_record_get(am, tx);

	if (am->am_claim_rt != NULL && dmu_tx_get_txg(tx) == am->am_claim_txg &&
	    !zfs_range_tree_is_empty(am->am_claim_rt)) {
		vdev_async_mirror_sync_write(spa, am->am_lagging->vdev_top,
		    am->am_claim_rt, SM_ALLOC, tx);
		zfs_range_tree_vacate(am->am_claim_rt, NULL, NULL);
	}
}

/* Add claimed log blocks to the claim txg before the sync thread starts. */
void
vdev_async_mirror_claim_committed(spa_t *spa, const blkptr_t *bp,
    uint64_t txg)
{
	vdev_async_mirror_t *am = spa->spa_async_mirror;

	if (am == NULL || am->am_lagging == NULL ||
	    BP_GET_TYPE(bp) != DMU_OT_INTENT_LOG)
		return;

	ASSERT(!spa->spa_sync_on);

	/* The claim can run in a thread that holds no config lock. */
	spa_config_enter(spa, SCL_VDEV, FTAG, RW_READER);
	for (int d = 0; d < BP_GET_NDVAS(bp); d++) {
		const dva_t *dva = &bp->blk_dva[d];
		vdev_t *vd = vdev_lookup_top(spa, DVA_GET_VDEV(dva));
		uint64_t offset = DVA_GET_OFFSET(dva);
		uint64_t asize = DVA_GET_ASIZE(dva);

		if (vd == NULL || !vdev_async_mirror_tracks(vd) ||
		    DVA_GET_GANG(dva) || asize == 0)
			continue;

		mutex_enter(&am->am_lock);
		if (am->am_claim_rt == NULL) {
			am->am_claim_rt = zfs_range_tree_create(NULL,
			    ZFS_RANGE_SEG64, NULL, 0, 0);
		}
		ASSERT(am->am_claim_txg == 0 || am->am_claim_txg == txg);
		am->am_claim_txg = txg;
		zfs_range_tree_clear(am->am_claim_rt, offset, asize);
		zfs_range_tree_add(am->am_claim_rt, offset, asize);
		mutex_exit(&am->am_lock);
	}
	spa_config_exit(spa, SCL_VDEV, FTAG);
}

/* The range of birth txgs of the blocks of the claimed logs. */
typedef struct async_mirror_log_arg {
	uint64_t	ala_lo;
	uint64_t	ala_hi;
} async_mirror_log_arg_t;

static void
async_mirror_log_note(async_mirror_log_arg_t *ala, uint64_t birth)
{
	ala->ala_lo = MIN(ala->ala_lo, birth);
	ala->ala_hi = MAX(ala->ala_hi, birth);
}

static int
async_mirror_log_blk_cb(zilog_t *zilog, const blkptr_t *bp, void *arg,
    uint64_t claim_txg)
{
	(void) zilog, (void) claim_txg;

	if (!BP_IS_HOLE(bp))
		async_mirror_log_note(arg, BP_GET_BIRTH(bp));
	return (0);
}

/* Claim log blocks born at or after the claim txg. */
static int
async_mirror_log_lr_cb(zilog_t *zilog, const lr_t *lrc, void *arg,
    uint64_t claim_txg)
{
	(void) zilog;

	if (lrc->lrc_txtype != TX_WRITE ||
	    lrc->lrc_reclen < sizeof (lr_write_t))
		return (0);

	const blkptr_t *bp = &((const lr_write_t *)lrc)->lr_blkptr;

	if (!BP_IS_HOLE(bp) && BP_GET_BIRTH(bp) >= claim_txg)
		async_mirror_log_note(arg, BP_GET_BIRTH(bp));
	return (0);
}

static int
async_mirror_log_ds_cb(dsl_pool_t *dp, dsl_dataset_t *ds, void *arg)
{
	objset_t *os;

	if (ds->ds_is_snapshot || dmu_objset_from_ds(ds, &os) != 0)
		return (0);

	zil_header_t *zh = &os->os_zil_header;

	if (zh->zh_claim_txg == 0 || BP_IS_HOLE(&zh->zh_log))
		return (0);

	zilog_t *zilog = zil_alloc(dp->dp_meta_objset, zh);

	(void) zil_parse(zilog, async_mirror_log_blk_cb, async_mirror_log_lr_cb,
	    arg, zh->zh_claim_txg, B_FALSE);
	zil_free(zilog);
	return (0);
}

/* Add claimed-log block birth txgs to children that leave the async mirror. */
void
vdev_async_mirror_claimed(spa_t *spa)
{
	dsl_pool_t *dp = spa_get_dsl(spa);
	const uint64_t *resync = spa->spa_async_mirror_import.ami_resync;
	async_mirror_log_arg_t ala = { .ala_lo = UINT64_MAX, .ala_hi = 0 };

	ASSERT(!spa->spa_sync_on);

	if (!spa_writeable(spa) || resync[0] == 0)
		return;

	(void) dmu_objset_find_dp(dp, dp->dp_root_dir_obj,
	    async_mirror_log_ds_cb, &ala, DS_FIND_CHILDREN);
	if (ala.ala_lo > ala.ala_hi)
		return;

	/* Bound on-disk births to an importable txg range. */
	uint64_t hi_max = spa_first_txg(spa) + ASYNC_MIRROR_CLAIM_TXG_SLACK;

	if (ala.ala_lo < TXG_INITIAL || ala.ala_hi > hi_max) {
		zfs_dbgmsg("async mirror: the claimed logs name txgs %llu to "
		    "%llu; the DTLs take only txgs %llu to %llu",
		    (u_longlong_t)ala.ala_lo, (u_longlong_t)ala.ala_hi,
		    (u_longlong_t)TXG_INITIAL, (u_longlong_t)hi_max);
		ala.ala_lo = MAX(ala.ala_lo, TXG_INITIAL);
		ala.ala_hi = MIN(ala.ala_hi, hi_max);
		if (ala.ala_lo > ala.ala_hi)
			return;
	}

	for (int i = 0; i < 2; i++) {
		vdev_t *vd = (resync[i] == 0) ? NULL :
		    vdev_lookup_by_guid(spa->spa_root_vdev, resync[i]);

		if (vd == NULL || !vd->vdev_ops->vdev_op_leaf)
			continue;
		zfs_dbgmsg("async mirror: the claimed logs of this import "
		    "are on txgs %llu to %llu", (u_longlong_t)ala.ala_lo,
		    (u_longlong_t)ala.ala_hi);
		async_mirror_mark_dtl(spa, vd, ala.ala_lo,
		    ala.ala_hi - ala.ala_lo + 1);
	}
}

/* Append rt to the record; the caller drops ms_lock across this call. */
void
vdev_async_mirror_sync_write(spa_t *spa, vdev_t *vd, zfs_range_tree_t *rt,
    maptype_t type, dmu_tx_t *tx)
{
	vdev_async_mirror_t *am = spa->spa_async_mirror;
	async_mirror_txg_t *at = async_mirror_record_get(am, tx);
	uint64_t space = zfs_range_tree_space(rt);

	ASSERT(dsl_pool_sync_context(dmu_objset_pool(spa_meta_objset(spa))));
	ASSERT(type == SM_ALLOC || type == SM_FREE);
	ASSERT(am->am_lagging->vdev_top == vd ||
	    (type == SM_FREE && vd->vdev_islog));
	ASSERT3U(at->at_txg, ==, dmu_tx_get_txg(tx));
	ASSERT3P(at->at_sm, !=, NULL);
	ASSERT(!at->at_ready);

	space_map_write(at->at_sm, rt, type, vd->vdev_id, tx);

	mutex_enter(&am->am_lock);
	if (type == SM_ALLOC) {
		at->at_bytes += space;
	} else {
		uint64_t dspace = async_mirror_held_dspace(vd, space);

		at->at_held += space;
		at->at_held_dspace += dspace;
		am->am_held_bytes += space;
		am->am_held_dspace += dspace;
		if (vd->vdev_islog)
			am->am_held_log_bytes += space;
		async_mirror_update_stats(am);
	}
	mutex_exit(&am->am_lock);
}

/* Remove one range from the range tree in arg. */
static void
async_mirror_rt_clear_cb(void *arg, uint64_t start, uint64_t size)
{
	zfs_range_tree_clear(arg, start, size);
}

/* Append allocations, excluding ranges that exist only on the leader. */
void
vdev_async_mirror_sync_list(spa_t *spa, vdev_t *vd, zfs_range_tree_t *alloctree,
    zfs_range_tree_t *nolist, dmu_tx_t *tx)
{
	if (zfs_range_tree_is_empty(alloctree))
		return;

	if (nolist == NULL || zfs_range_tree_is_empty(nolist)) {
		vdev_async_mirror_sync_write(spa, vd, alloctree, SM_ALLOC, tx);
		return;
	}

	zfs_range_tree_t *rt = zfs_range_tree_create(NULL, alloctree->rt_type,
	    NULL, alloctree->rt_start, alloctree->rt_shift);

	zfs_range_tree_walk(alloctree, zfs_range_tree_add, rt);
	zfs_range_tree_walk(nolist, async_mirror_rt_clear_cb, rt);
	if (!zfs_range_tree_is_empty(rt))
		vdev_async_mirror_sync_write(spa, vd, rt, SM_ALLOC, tx);
	zfs_range_tree_vacate(rt, NULL, NULL);
	zfs_range_tree_destroy(rt);
}

/* A syncing txg is durable after spa_sync() writes its uberblock. */
static boolean_t
async_mirror_dtl_durable(vdev_async_mirror_t *am, uint64_t txg)
{
	return (txg != 0 && txg <= spa_last_synced_txg(am->am_spa));
}

boolean_t
vdev_async_mirror_is_async(vdev_t *vd)
{
	uint32_t state = atomic_load_32(&vd->vdev_async_state);
	vdev_async_mirror_t *am = vd->vdev_spa->spa_async_mirror;

	if (async_mirror_copying(state) || state == AMS_FROZEN)
		return (B_TRUE);
	if (am != NULL && am->am_dtl_child_guid == vd->vdev_guid &&
	    (am->am_dtl_child_txg == 0 ||
	    !async_mirror_durable(am, am->am_dtl_child_txg)))
		return (B_TRUE);
	if (state == AMS_FAILED) {
		return (am != NULL && am->am_lagging == vd &&
		    !async_mirror_dtl_durable(am, am->am_dtl_txg));
	}
	return (B_FALSE);
}

/* Hold resilver and scrub until the child's DTL label and uberblock flush. */
boolean_t
vdev_async_mirror_dtl_fence(vdev_t *vd)
{
	vdev_async_mirror_t *am = vd->vdev_spa->spa_async_mirror;
	boolean_t fence;

	if (am == NULL)
		return (B_FALSE);
	mutex_enter(&am->am_lock);
	fence = (am->am_dtl_child_guid != 0 &&
	    am->am_dtl_child_guid == vd->vdev_guid);
	mutex_exit(&am->am_lock);
	return (fence);
}

/* True if the DTL fence of any child of the pool is open. */
boolean_t
vdev_async_mirror_dtl_fence_open(spa_t *spa)
{
	vdev_async_mirror_t *am = spa->spa_async_mirror;
	boolean_t fence;

	if (am == NULL)
		return (B_FALSE);
	mutex_enter(&am->am_lock);
	fence = (am->am_dtl_child_guid != 0);
	mutex_exit(&am->am_lock);
	return (fence);
}
/* Keep labels behind the durable DTL fence. */
boolean_t
vdev_async_mirror_skip_labels(const vdev_t *vd)
{
	vdev_async_mirror_t *am = vd->vdev_spa->spa_async_mirror;
	uint64_t dtl_txg;

	if (am == NULL)
		return (B_FALSE);
	if (am->am_lagging == vd &&
	    !async_mirror_dtl_durable(am, am->am_dtl_txg))
		return (B_TRUE);
	if (am->am_dtl_child_guid != vd->vdev_guid)
		return (B_FALSE);
	dtl_txg = am->am_dtl_txg;
	if (!async_mirror_dtl_durable(am, dtl_txg))
		return (B_TRUE);
	return (zfs_vdev_async_mirror_inject_dtl_delay != 0 &&
	    am->am_synced_txg - dtl_txg <
	    zfs_vdev_async_mirror_inject_dtl_delay);
}

/* Inject a failed label flush for file-backed tests. */
boolean_t
vdev_async_mirror_inject_dtl_label_flush(const vdev_t *vd)
{
	vdev_async_mirror_t *am = vd->vdev_spa->spa_async_mirror;
	boolean_t inject;

	if (am == NULL || zfs_vdev_async_mirror_inject_dtl_label_flush == 0)
		return (B_FALSE);

	mutex_enter(&am->am_lock);
	inject = (am->am_dtl_child_guid == vd->vdev_guid &&
	    am->am_dtl_txg != 0 &&
	    async_mirror_dtl_durable(am, am->am_dtl_txg) &&
	    am->am_dtl_label_txg <= am->am_dtl_txg);
	mutex_exit(&am->am_lock);

	return (inject);
}

/* Flush the failed leaf itself; aggregate vdev flushes hide leaf failures. */
static void
async_mirror_dtl_child_flush(spa_t *spa, uint64_t txg, boolean_t label)
{
	vdev_async_mirror_t *am = spa->spa_async_mirror;
	vdev_t *vd;
	uint64_t guid;
	boolean_t pending;
	boolean_t inject;
	int error;

	if (am == NULL)
		return;

	mutex_enter(&am->am_lock);
	guid = am->am_dtl_child_guid;
	pending = (guid != 0 && (label ?
	    am->am_dtl_label_write_txg == txg :
	    am->am_dtl_ub_write_txg == txg));
	mutex_exit(&am->am_lock);
	if (guid == 0)
		return;

	vd = vdev_lookup_by_guid(spa->spa_root_vdev, guid);
	if (vd == NULL || !vd->vdev_ops->vdev_op_leaf)
		return;
	inject = (label && vdev_async_mirror_inject_dtl_label_flush(vd));
	if (!pending && !inject)
		return;

	if (inject) {
		error = SET_ERROR(EIO);
	} else {
		zio_t *zio = zio_root(spa, NULL, NULL,
		    ZIO_FLAG_CONFIG_WRITER | ZIO_FLAG_CANFAIL);
		zio_flush_leaf(zio, vd);
		error = zio_wait(zio);
	}
	if (error == 0) {
		if (label)
			vdev_async_mirror_label_flushed(vd, txg);
		else
			vdev_async_mirror_uberblock_flushed(vd, txg);
	} else {
		zfs_dbgmsg("async mirror: %s flush failed for leaf guid %llu, "
		    "txg %llu, error %d", label ? "label" : "uberblock",
		    (u_longlong_t)guid, (u_longlong_t)txg, error);
		spa_async_request(spa, SPA_ASYNC_CONFIG_UPDATE);
	}
}

void
vdev_async_mirror_label_flush(spa_t *spa, uint64_t txg)
{
	async_mirror_dtl_child_flush(spa, txg, B_TRUE);
}

void
vdev_async_mirror_uberblock_flush(spa_t *spa, uint64_t txg)
{
	async_mirror_dtl_child_flush(spa, txg, B_FALSE);
}

static void
async_mirror_dtl_child_written(vdev_t *vd, uint64_t txg, boolean_t label)
{
	vdev_async_mirror_t *am = vd->vdev_spa->spa_async_mirror;

	if (am == NULL)
		return;
	mutex_enter(&am->am_lock);
	if (am->am_dtl_child_guid == vd->vdev_guid &&
	    txg > am->am_dtl_txg) {
		uint64_t *write_txg = label ? &am->am_dtl_label_write_txg :
		    &am->am_dtl_ub_write_txg;
		*write_txg = MAX(*write_txg, txg);
	}
	mutex_exit(&am->am_lock);
}

void
vdev_async_mirror_label_written(vdev_t *vd, uint64_t txg)
{
	async_mirror_dtl_child_written(vd, txg, B_TRUE);
}


void
vdev_async_mirror_uberblock_written(vdev_t *vd, uint64_t txg)
{
	async_mirror_dtl_child_written(vd, txg, B_FALSE);
}

static void
async_mirror_dtl_child_flushed(vdev_t *vd, uint64_t txg, boolean_t label)
{
	if (!vd->vdev_ops->vdev_op_leaf)
		return;
	vdev_async_mirror_t *am = vd->vdev_spa->spa_async_mirror;
	boolean_t wake = B_FALSE;
	boolean_t request_label = B_FALSE;

	if (am == NULL)
		return;
	uint64_t *write_txg = label ? &am->am_dtl_label_write_txg :
	    &am->am_dtl_ub_write_txg;
	uint64_t *flushed_txg = label ? &am->am_dtl_label_txg :
	    &am->am_dtl_ub_txg;
	mutex_enter(&am->am_lock);
	if (am->am_dtl_child_guid == vd->vdev_guid &&
	    *write_txg == txg) {
		*flushed_txg = MAX(*flushed_txg, txg);
		*write_txg = 0;
		uint64_t child_txg = am->am_dtl_child_txg;
		ASSERT(MUTEX_HELD(&am->am_lock));
		wake = (am->am_dtl_child_guid != 0 &&
		    am->am_dtl_label_txg > am->am_dtl_txg &&
		    am->am_dtl_ub_txg > am->am_dtl_txg &&
		    am->am_dtl_label_txg >= am->am_dtl_ub_txg);
		if (wake) {
			am->am_dtl_child_txg = MAX(am->am_dtl_child_txg,
			    am->am_dtl_ub_txg);
			wake = async_mirror_durable(am, am->am_dtl_child_txg);
		}
		if (child_txg != am->am_dtl_child_txg)
			async_mirror_update_stats(am);
		request_label = (am->am_dtl_label_txg < am->am_dtl_ub_txg);
	}
	mutex_exit(&am->am_lock);
	if (request_label)
		spa_async_request(vd->vdev_spa, SPA_ASYNC_CONFIG_UPDATE);
	if (wake && am->am_release_zthr != NULL)
		zthr_wakeup(am->am_release_zthr);
}

void
vdev_async_mirror_label_flushed(vdev_t *vd, uint64_t txg)
{
	async_mirror_dtl_child_flushed(vd, txg, B_TRUE);
}

void
vdev_async_mirror_uberblock_flushed(vdev_t *vd, uint64_t txg)
{
	async_mirror_dtl_child_flushed(vd, txg, B_FALSE);
}

boolean_t
vdev_async_mirror_skip_uberblock(const vdev_t *vd)
{
	vdev_async_mirror_t *am = vd->vdev_spa->spa_async_mirror;
	boolean_t skip;

	if (am == NULL)
		return (vdev_async_mirror_skip_labels(vd));
	mutex_enter(&am->am_lock);
	ASSERT(MUTEX_HELD(&am->am_lock));
	skip = vdev_async_mirror_skip_labels(vd) ||
	    (am->am_dtl_child_guid == vd->vdev_guid &&
	    am->am_dtl_label_txg <= am->am_dtl_txg) ||
	    (am->am_lagging != NULL &&
	    !async_mirror_dtl_durable(am, am->am_dtl_txg) &&
	    vd->vdev_top != NULL && vd->vdev_top->vdev_islog);
	mutex_exit(&am->am_lock);
	return (skip);
}

/* Fence writes that replay, repair, or a pending DTL could make unsafe. */
boolean_t
vdev_async_mirror_intercept_write(zio_t *zio, vdev_t *vd)
{
	uint32_t state = atomic_load_32(&vd->vdev_async_state);
	vdev_async_mirror_t *am = vd->vdev_spa->spa_async_mirror;

	ASSERT3U(zio->io_type, ==, ZIO_TYPE_WRITE);

	return ((am != NULL && am->am_dtl_child_guid == vd->vdev_guid &&
	    vdev_async_mirror_is_async(vd)) ||
	    state == AMS_ACTIVE || state == AMS_FROZEN ||
	    (state == AMS_FAILED && vdev_async_mirror_is_async(vd)));
}

/* True if this pool uses, or is still draining, an async mirror. */
boolean_t
vdev_async_mirror_in_use(spa_t *spa)
{
	vdev_async_mirror_t *am = spa->spa_async_mirror;

	if (am == NULL)
		return (B_FALSE);

	uint32_t state = atomic_load_32(&am->am_state);

	return (async_mirror_has_child(state) || state == AMS_DRAINING ||
	    am->am_dtl_child_guid != 0);
}

/* Refuse removal while records or held log blocks depend on this vdev. */
boolean_t
vdev_async_mirror_removal_refused(const vdev_t *vd)
{
	vdev_async_mirror_t *am = vd->vdev_spa->spa_async_mirror;

	ASSERT3P(vd->vdev_top, ==, vd);

	if (am == NULL)
		return (B_FALSE);

	mutex_enter(&am->am_lock);
	boolean_t refused = (am->am_lagging != NULL &&
	    am->am_lagging->vdev_top == vd) ||
	    (vd->vdev_islog && am->am_held_log_bytes != 0) ||
	    (async_mirror_draining(am->am_state) && am->am_zap_obj != 0);
	mutex_exit(&am->am_lock);
	return (refused);
}

/* True if the async mirror is on for this pool. */
boolean_t
vdev_async_mirror_active(spa_t *spa)
{
	vdev_async_mirror_t *am = spa->spa_async_mirror;

	return (am != NULL &&
	    (async_mirror_has_child(atomic_load_32(&am->am_state)) ||
	    am->am_dtl_child_guid != 0));
}

/* Clear a reported failure after the operator removes or replaces the leaf. */
void
vdev_async_mirror_failed_clear(spa_t *spa, uint64_t guid)
{
	vdev_async_mirror_t *am = spa->spa_async_mirror;

	if (am == NULL || guid == 0)
		return;
	mutex_enter(&am->am_lock);
	if (am->am_failed_guid == guid) {
		am->am_failed_guid = 0;
		async_mirror_update_stats(am);
	}
	mutex_exit(&am->am_lock);
}

uint64_t
vdev_async_mirror_failed_guid(spa_t *spa)
{
	vdev_async_mirror_t *am = spa->spa_async_mirror;
	uint64_t guid;

	if (am == NULL)
		return (0);
	mutex_enter(&am->am_lock);
	guid = am->am_failed_guid;
	mutex_exit(&am->am_lock);
	return (guid);
}

/* True if vd is a referenced child of the async mirror. */
boolean_t
vdev_async_mirror_is_child(const vdev_t *vd)
{
	vdev_async_mirror_t *am = vd->vdev_spa->spa_async_mirror;

	if (am == NULL)
		return (B_FALSE);

	mutex_enter(&am->am_lock);
	boolean_t child = (am->am_lagging != NULL &&
	    (vd == am->am_lagging || vd == am->am_leader));
	mutex_exit(&am->am_lock);
	return (child);
}

/* True if vd is the failed lagging child that can be replaced. */
boolean_t
vdev_async_mirror_failed_lagging(vdev_t *vd)
{
	vdev_async_mirror_t *am = vd->vdev_spa->spa_async_mirror;
	boolean_t failed;
	boolean_t lagging;

	if (am == NULL)
		return (B_FALSE);

	mutex_enter(&am->am_lock);
	lagging = (am->am_lagging == vd);
	/* Failure state outlives child release. */
	failed = (lagging && (am->am_state == AMS_FROZEN ||
	    am->am_state == AMS_FAILED)) ||
	    am->am_failed_guid == vd->vdev_guid;
	mutex_exit(&am->am_lock);

	return (failed || (lagging && (vd->vdev_state == VDEV_STATE_FAULTED ||
	    vd->vdev_state == VDEV_STATE_REMOVED || !vdev_readable(vd) ||
	    !vdev_writeable(vd))));
}

/* Wait until the switch off has stored the DTLs and released the child. */
int
vdev_async_mirror_wait_off(spa_t *spa, uint64_t guid)
{
	vdev_async_mirror_t *am = spa->spa_async_mirror;
	int error = 0;
	uint64_t replace_dtl_txg = 0;
	boolean_t wake_release = B_FALSE;

	if (am == NULL)
		return (0);

	mutex_enter(&am->am_lock);
	while (am->am_lagging != NULL &&
	    am->am_lagging->vdev_guid == guid) {
		boolean_t replace = (am->am_replace_guid == guid &&
		    am->am_state == AMS_FAILED &&
		    am->am_dtl_child_guid == guid);

		if (replace && am->am_dtl_range_txg != 0) {
			if (replace_dtl_txg == 0)
				replace_dtl_txg = am->am_dtl_range_txg;

			if (!async_mirror_durable(am, replace_dtl_txg)) {
				mutex_exit(&am->am_lock);
				txg_wait_synced(spa_get_dsl(spa),
				    replace_dtl_txg);
				mutex_enter(&am->am_lock);
				continue;
			}

			/* The durable DTL protects the replacement. */
			am->am_off = B_FALSE;
			async_mirror_set_state(am, AMS_DRAINING);
			async_mirror_update_stats(am);
			wake_release = B_TRUE;
			break;
		}

		if (cv_wait_sig(&am->am_drain_cv, &am->am_lock) == 0) {
			error = EINTR;
			break;
		}
	}
	mutex_exit(&am->am_lock);
	if (wake_release && am->am_release_zthr != NULL)
		zthr_wakeup(am->am_release_zthr);
	return (error);
}

/* True if this top-level vdev needs records and held frees. */
boolean_t
vdev_async_mirror_tracks(const vdev_t *vd)
{
	vdev_async_mirror_t *am = vd->vdev_spa->spa_async_mirror;

	ASSERT3P(vd->vdev_top, ==, vd);

	return (am != NULL && am->am_lagging != NULL &&
	    am->am_lagging->vdev_top == vd && async_mirror_tracking(am));
}

/* Hold frees referenced by the child's importable txgs, except current ZIL. */
boolean_t
vdev_async_mirror_should_hold(const vdev_t *vd, metaslab_free_kind_t kind)
{
	vdev_async_mirror_t *am = vd->vdev_spa->spa_async_mirror;

	if (kind == METASLAB_FREE_ZIL)
		return (B_FALSE);
	if (vd->vdev_islog) {
		return (kind == METASLAB_FREE_ZIL_CLAIMED && am != NULL &&
		    am->am_lagging != NULL && async_mirror_tracking(am) &&
		    !vd->vdev_removing);
	}
	return (vdev_async_mirror_tracks(vd));
}

/* Held space, in deflated bytes.  The space accounting subtracts it. */
uint64_t
spa_get_async_mirror_held_space(spa_t *spa)
{
	vdev_async_mirror_t *am = spa->spa_async_mirror;

	return (am != NULL ? am->am_held_dspace : 0);
}

/* Caller holds am_lock. */
static boolean_t
async_mirror_passed(vdev_async_mirror_t *am, async_mirror_txg_t *at)
{
	ASSERT(MUTEX_HELD(&am->am_lock));

	return (am->am_state == AMS_PASSTHROUGH &&
	    at->at_txg > am->am_pass_txg);
}

static void async_mirror_giveup_check(vdev_async_mirror_t *, uint64_t);
static uint64_t async_mirror_space_avail(spa_t *);

/* Close the synced record and wake the copy worker. */
void
vdev_async_mirror_txg_synced(spa_t *spa, uint64_t txg)
{
	vdev_async_mirror_t *am = spa->spa_async_mirror;

	if (am == NULL)
		return;

	/* Read space accounting before locking. */
	uint64_t avail = async_mirror_has_child(atomic_load_32(&am->am_state)) ?
	    async_mirror_space_avail(spa) : UINT64_MAX;
	boolean_t request_dtl_label = B_FALSE;
	boolean_t wake_dtl_release = B_FALSE;

	mutex_enter(&am->am_lock);
	async_mirror_txg_t *at = list_tail(&am->am_txgs);

	atomic_store_64(&am->am_avail, avail);

	ASSERT3U(txg, ==, spa_syncing_txg(spa));
	/* Never replay a txg that has no durable uberblock. */
	if (txg > spa_freeze_txg(spa)) {
		if (at != NULL && at->at_txg == txg && at->at_sm != NULL) {
			space_map_close(at->at_sm);
			at->at_sm = NULL;
		}
		mutex_exit(&am->am_lock);
		return;
	}

	am->am_synced_txg = txg;
	am->am_spa->spa_async_mirror_stats.sams_synced_txg = txg;

	if (at != NULL && at->at_txg == txg && !at->at_ready) {
		if (at->at_sm != NULL) {
			at->at_sm_len = space_map_length(at->at_sm);
			space_map_close(at->at_sm);
			at->at_sm = NULL;
		}
		at->at_ub = spa->spa_uberblock;
		at->at_ready = B_TRUE;
		at->at_ready_time = gethrtime();
		am->am_last_ub = at->at_ub;
		if (!async_mirror_passed(am, at)) {
			/* Start stall timing with the first pending txg. */
			if (am->am_pending_txgs == 0)
				async_mirror_progress(am);
			am->am_pending_bytes += at->at_bytes;
			am->am_pending_txgs++;
		}
		ASSERT3U(at->at_txg, >, am->am_applied_txg);
		am->am_spa->spa_async_mirror_stats.sams_synced_txg = txg;
		async_mirror_update_stats(am);
		cv_broadcast(&am->am_cv);
	}
	/* The held space of this txg is counted now. */
	async_mirror_giveup_check(am, avail);
	if (am->am_dtl_child_guid != 0 && am->am_dtl_txg != 0 &&
	    async_mirror_dtl_durable(am, am->am_dtl_txg) &&
	    am->am_dtl_label_txg <= am->am_dtl_txg &&
	    am->am_synced_txg - am->am_dtl_txg >=
	    zfs_vdev_async_mirror_inject_dtl_delay)
		request_dtl_label = B_TRUE;
	wake_dtl_release = (am->am_dtl_child_guid != 0 &&
	    am->am_dtl_child_txg != 0 &&
	    async_mirror_durable(am, am->am_dtl_child_txg));
	async_mirror_update_stats(am);
	mutex_exit(&am->am_lock);
	if (request_dtl_label)
		spa_async_request(spa, SPA_ASYNC_CONFIG_UPDATE);
	if (wake_dtl_release && am->am_release_zthr != NULL)
		zthr_wakeup(am->am_release_zthr);
}

/* Check allocator pressure before a new txg starts to allocate. */
void
vdev_async_mirror_sync_check(spa_t *spa)
{
	vdev_async_mirror_t *am = spa->spa_async_mirror;
	uint64_t avail;

	if (am == NULL)
		return;

	avail = async_mirror_space_avail(spa);
	atomic_store_64(&am->am_avail, avail);
	mutex_enter(&am->am_lock);
	if (async_mirror_copying(am->am_state) && !am->am_stop)
		async_mirror_giveup_check(am, avail);
	mutex_exit(&am->am_lock);
}

/* True while a running copy can make progress and throttle writes. */
static boolean_t
async_mirror_throttling(vdev_async_mirror_t *am)
{
	return (async_mirror_copying(atomic_load_32(&am->am_state)) &&
	    am->am_thread != NULL && !am->am_stop && !am->am_giveup &&
	    !zfs_vdev_async_mirror_suspend_copy);
}

/* True if the copy backlog exceeds a limit. */
static boolean_t
async_mirror_backlog_over(vdev_async_mirror_t *am)
{
	uint64_t max_bytes = zfs_vdev_async_mirror_max_pending_bytes;
	uint64_t max_txgs = zfs_vdev_async_mirror_max_pending_txgs;

	return (am->am_pending_bytes > max_bytes ||
	    am->am_pending_txgs > max_txgs);
}

/* True if the copy currently throttles transactions. */
static boolean_t
async_mirror_over_limit(vdev_async_mirror_t *am)
{
	return (async_mirror_throttling(am) && async_mirror_backlog_over(am));
}

/* Return the dirty-data throttle delay for pending units and a limit. */
static hrtime_t
async_mirror_delay_curve(uint64_t pending, uint64_t limit)
{
	if (limit > UINT64_MAX / 100)
		return (0);

	uint64_t threshold = limit * zfs_delay_min_dirty_percent / 100;

	if (pending <= threshold)
		return (0);
	if (pending >= limit)
		return (INT64_MAX);
	return (zfs_delay_scale * (pending - threshold) /
	    (limit - pending));
}

/* Return the deflated held-space throttle limit, or 0 when disabled. */
static uint64_t
async_mirror_held_limit(spa_t *spa)
{
	uint64_t percent = MIN(zfs_vdev_async_mirror_max_held_percent, 100);

	return (spa_get_dspace(spa) / 100 * percent);
}

/* Caller holds am_lock; count held space a release can free now. */
static void
async_mirror_ready_update(vdev_async_mirror_t *am)
{
	uint64_t ready = 0;

	ASSERT(MUTEX_HELD(&am->am_lock));

	if (async_mirror_draining(am->am_state)) {
		ready = am->am_held_dspace;
	} else {
		for (async_mirror_txg_t *at = list_head(&am->am_txgs);
		    at != NULL && at->at_ready &&
		    at->at_txg <= am->am_applied_txg && at->at_ndefer == 0;
		    at = list_next(&am->am_txgs, at))
			ready += at->at_held_dspace;
	}
	am->am_ready_dspace = ready;
}

/* Bound the free-space margin by the tunable and one quarter of slop. */
static uint64_t
async_mirror_giveup_margin(spa_t *spa)
{
	uint64_t percent = MIN(zfs_vdev_async_mirror_held_free_percent, 100);

	return (MIN(spa_get_dspace(spa) / 100 * percent,
	    spa_get_slop_space(spa) / 4));
}

/* Return space available to allocations after slop and deferred frees. */
static uint64_t
async_mirror_allocator_avail(spa_t *spa)
{
	metaslab_class_t *mc = spa_normal_class(spa);
	uint64_t class_space = metaslab_class_get_dspace(mc);
	uint64_t class_alloc = metaslab_class_get_dalloc(mc);
	uint64_t class_avail = (class_space > class_alloc ?
	    class_space - class_alloc : 0);
	uint64_t slop = spa_get_slop_space(spa);

	/*
	 * The allocation count includes held and deferred frees until they
	 * become allocatable.
	 */
	class_avail = (class_avail > slop ? class_avail - slop : 0);

	return (class_avail);
}

/* Bound dataset space by allocator space. The sync thread calls this. */
static uint64_t
async_mirror_space_avail(spa_t *spa)
{
	dsl_pool_t *dp = spa_get_dsl(spa);
	uint64_t size = dsl_pool_adjustedsize(dp, ZFS_SPACE_CHECK_NORMAL);
	uint64_t used = dsl_dir_phys(dp->dp_root_dir)->dd_used_bytes;
	uint64_t logical_avail = (size > used ? size - used : 0);
	uint64_t allocator_avail = async_mirror_allocator_avail(spa);

	return (MIN(logical_avail, allocator_avail));
}

/* Give-up text for events, history, and status. */
const char *
async_mirror_giveup_text(uint_t why)
{
	static const char *const reason[] = {
		[AMG_HELD] = "held space limit",
		[AMG_SPACE] = "pool space low",
		[AMG_STALL] = "copy stalled",
	};

	return (why < ARRAY_SIZE(reason) && reason[why] != NULL ? reason[why] :
	    reason[AMG_HELD]);
}

/* Give-up text for pool status. */
const char *
vdev_async_mirror_giveup_reason(uint64_t why)
{
	return (async_mirror_giveup_text((uint_t)why));
}

/* Set the give-up state and wake the worker. Caller holds am_lock. */
void
async_mirror_giveup(vdev_async_mirror_t *am, async_mirror_giveup_t why)
{
	ASSERT(MUTEX_HELD(&am->am_lock));
	ASSERT(!am->am_giveup);
	ASSERT(async_mirror_copying(am->am_state));

	zfs_dbgmsg("async mirror: the async mirror gives up on the lagging "
	    "child guid %llu at txg %llu: %s; %llu bytes held, backlog %llu "
	    "bytes in %llu txgs", (u_longlong_t)am->am_lagging->vdev_guid,
	    (u_longlong_t)am->am_applied_txg, async_mirror_giveup_text(why),
	    (u_longlong_t)am->am_held_dspace,
	    (u_longlong_t)am->am_pending_bytes,
	    (u_longlong_t)am->am_pending_txgs);
	am->am_giveup = B_TRUE;
	am->am_giveup_why = why;
	am->am_limit_since = 0;
	am->am_space_since = 0;
	cv_broadcast(&am->am_cv);
	cv_broadcast(&am->am_drain_cv);
}

/* Caller holds am_lock. */
void
async_mirror_freeze_note(vdev_async_mirror_t *am)
{
	ASSERT(MUTEX_HELD(&am->am_lock));

	am->am_frozen_stale = am->am_frozen_entry;
	am->am_limit_since = 0;
	am->am_space_since = 0;
	if (am->am_giveup)
		am->am_giveups++;
}

/* Give up on a stalled backlog. Caller holds am_lock. */
static boolean_t
async_mirror_stall_check(vdev_async_mirror_t *am)
{
	uint_t secs = zfs_vdev_async_mirror_stall_secs;

	ASSERT(MUTEX_HELD(&am->am_lock));

	if (secs == 0 || am->am_giveup || am->am_stop ||
	    am->am_thread == NULL || zfs_vdev_async_mirror_suspend_copy ||
	    spa_suspended(am->am_spa) ||
	    !async_mirror_copying(am->am_state) ||
	    !async_mirror_backlog_over(am))
		return (B_FALSE);

	hrtime_t last = (hrtime_t)atomic_load_64(&am->am_progress_time);

	if (gethrtime() - last < SEC2NSEC((hrtime_t)secs))
		return (B_FALSE);
	async_mirror_giveup(am, AMG_STALL);
	return (B_TRUE);
}

/* Reset a limit clock when its condition clears or the child advances. */
static boolean_t
async_mirror_giveup_clock(vdev_async_mirror_t *am, boolean_t cond,
    hrtime_t *sincep, uint64_t *targetp, hrtime_t wait_ns)
{
	hrtime_t now = gethrtime();

	ASSERT(MUTEX_HELD(&am->am_lock));

	if (!cond) {
		*sincep = 0;
		return (B_FALSE);
	}
	if (*sincep == 0 || am->am_applied_txg >= *targetp) {
		*sincep = now;
		*targetp = am->am_synced_txg;
	}
	return (now - *sincep >= wait_ns);
}

/* Check held-space limits in sync and timer context. Caller holds am_lock. */
static void
async_mirror_giveup_check(vdev_async_mirror_t *am, uint64_t avail)
{
	spa_t *spa = am->am_spa;
	uint64_t limit = spa_get_dspace(spa) / 100 *
	    MIN(zfs_vdev_async_mirror_held_limit_percent, 100);
	uint64_t margin = async_mirror_giveup_margin(spa);

	ASSERT(MUTEX_HELD(&am->am_lock));

	if (am->am_giveup || !async_mirror_copying(am->am_state))
		return;
	if (async_mirror_stall_check(am))
		return;

	/* The common case, with no walk of the records. */
	if ((limit == 0 || am->am_held_dspace < limit) &&
	    (margin == 0 || avail >= margin || am->am_held_dspace == 0)) {
		am->am_limit_since = 0;
		am->am_space_since = 0;
		return;
	}

	async_mirror_ready_update(am);

	uint64_t waiting = am->am_held_dspace -
	    MIN(am->am_held_dspace, am->am_ready_dspace);
	boolean_t over = (limit != 0 && waiting >= limit);
	boolean_t low = (margin != 0 && avail < margin && waiting != 0);
	boolean_t urgent = (low && waiting >= margin - avail);
	hrtime_t limit_ns = SEC2NSEC((hrtime_t)
	    zfs_vdev_async_mirror_held_limit_secs);
	hrtime_t stall_ns = MIN(limit_ns,
	    SEC2NSEC((hrtime_t)zfs_txg_timeout));
	hrtime_t now = gethrtime();
	hrtime_t last_progress = (hrtime_t)
	    atomic_load_64(&am->am_progress_time);
	boolean_t urgent_now = (urgent && !spa_suspended(spa) &&
	    (zfs_vdev_async_mirror_suspend_copy ||
	    now - last_progress >= stall_ns));

	/*
	 * Give up for low space only when the waiting frees can restore the
	 * margin and the copy is suspended or has made no progress.  A child
	 * that is copying blocks will release their held frees as it advances.
	 */
	if (urgent_now) {
		async_mirror_giveup(am, AMG_SPACE);
	} else if (async_mirror_giveup_clock(am, over, &am->am_limit_since,
	    &am->am_limit_target, limit_ns)) {
		async_mirror_giveup(am, AMG_HELD);
	} else if (async_mirror_giveup_clock(am, low, &am->am_space_since,
	    &am->am_space_target, limit_ns) ||
	    (low && gethrtime() - am->am_space_since >= stall_ns &&
	    am->am_pending_txgs != 0 && !spa_suspended(spa) &&
	    gethrtime() - (hrtime_t)atomic_load_64(&am->am_progress_time) >=
	    stall_ns)) {
		async_mirror_giveup(am, AMG_SPACE);
	}
}

/* Caller holds am_lock. */
static boolean_t
async_mirror_held_urgent(vdev_async_mirror_t *am)
{
	uint64_t limit = async_mirror_held_limit(am->am_spa);
	uint64_t ready = am->am_ready_dspace;

	ASSERT(MUTEX_HELD(&am->am_lock));

	if ((limit >> 20) == 0)
		return (B_FALSE);
	if (am->am_release_capped && am->am_release_futile_txg != 0 &&
	    am->am_release_futile_txg == am->am_last_release_txg)
		return (B_FALSE);
	return (async_mirror_delay_curve(ready >> 20, limit >> 20) != 0 ||
	    (am->am_release_more && ready > limit / 4));
}

/* Return the maximum delay from copy backlog and ready held space. */
hrtime_t
vdev_async_mirror_delay(spa_t *spa)
{
	vdev_async_mirror_t *am = spa->spa_async_mirror;

	if (am == NULL || !async_mirror_throttling(am))
		return (0);
	if (async_mirror_over_limit(am))
		return (INT64_MAX);

	hrtime_t bytes = async_mirror_delay_curve(am->am_pending_bytes >> 20,
	    zfs_vdev_async_mirror_max_pending_bytes >> 20);
	hrtime_t txgs = async_mirror_delay_curve(am->am_pending_txgs,
	    zfs_vdev_async_mirror_max_pending_txgs);
	uint64_t held_limit = async_mirror_held_limit(spa) >> 20;
	hrtime_t held = (held_limit != 0 &&
	    !zfs_vdev_async_mirror_suspend_release &&
	    spa_freeze_txg(spa) == UINT64_MAX) ?
	    async_mirror_delay_curve(am->am_ready_dspace >> 20, held_limit) : 0;

	return (MAX(MAX(bytes, txgs), held));
}

/* Wait for the copy backlog to fall below its limit. Caller holds no lock. */
void
vdev_async_mirror_wait(spa_t *spa)
{
	vdev_async_mirror_t *am = spa->spa_async_mirror;

	if (am == NULL || !async_mirror_over_limit(am))
		return;

	hrtime_t start = gethrtime();

	mutex_enter(&am->am_lock);
	while (async_mirror_over_limit(am) && !async_mirror_stall_check(am)) {
		(void) cv_timedwait(&am->am_drain_cv, &am->am_lock,
		    ddi_get_lbolt() + hz);
	}
	spa->spa_async_mirror_stats.sams_throttled_ms +=
	    NSEC2MSEC(gethrtime() - start);
	async_mirror_update_stats(am);
	mutex_exit(&am->am_lock);
}

/* Snapshot the newest ready txg up to limit. */
boolean_t
vdev_async_mirror_wait_target(spa_t *spa, uint64_t limit, uint64_t *targetp)
{
	vdev_async_mirror_t *am = spa->spa_async_mirror;
	boolean_t active = B_FALSE;

	if (am == NULL) {
		if (limit != UINT64_MAX && limit != 0) {
			*targetp = limit;
			return (B_TRUE);
		}
		return (B_FALSE);
	}

	mutex_enter(&am->am_lock);
	if (async_mirror_has_child(am->am_state)) {
		uint64_t newest = am->am_applied_txg;
		for (async_mirror_txg_t *at = list_head(&am->am_txgs);
		    at != NULL; at = list_next(&am->am_txgs, at)) {
			if (at->at_ready && at->at_txg > newest)
				newest = at->at_txg;
		}
		*targetp = MIN(limit, newest);
		active = B_TRUE;
	} else if (limit != UINT64_MAX && limit != 0 &&
	    am->am_applied_txg < limit) {
		/* It stopped after userland captured a target. */
		*targetp = limit;
		active = B_TRUE;
	}
	mutex_exit(&am->am_lock);

	return (active);
}

/* Report whether zpool sync would wait for a stopped or failed copy. */
boolean_t
vdev_async_mirror_sync_stopped(spa_t *spa)
{
	vdev_async_mirror_t *am = spa->spa_async_mirror;
	boolean_t stopped = B_FALSE;

	if (am == NULL)
		return (B_FALSE);

	mutex_enter(&am->am_lock);
	if (async_mirror_has_child(am->am_state) &&
	    am->am_applied_txg < spa_last_synced_txg(spa) &&
	    (am->am_state != AMS_ACTIVE || am->am_off ||
	    zfs_vdev_async_mirror_suspend_copy))
		stopped = B_TRUE;
	/*
	 * A failed copy stays a failure until the operator acts on the leaf,
	 * also after the async mirror let go of the child; see am_failed_guid.
	 */
	if (am->am_failed_guid != 0)
		stopped = B_TRUE;
	mutex_exit(&am->am_lock);

	return (stopped);
}

/* Check a wait target while the caller holds spa_activities_lock. */
int
vdev_async_mirror_wait_pending(spa_t *spa, uint64_t target,
    boolean_t stop_early, boolean_t *pendingp)
{
	vdev_async_mirror_t *am = spa->spa_async_mirror;
	int error = 0;

	ASSERT(MUTEX_HELD(&spa->spa_activities_lock));
	*pendingp = B_FALSE;
	if (am == NULL)
		return (ECANCELED);

	/* Keep the state change lock order used by async_mirror_set_state(). */
	mutex_exit(&spa->spa_activities_lock);
	mutex_enter(&am->am_lock);
	mutex_enter(&spa->spa_activities_lock);
	if (am->am_applied_txg < target) {
		if (am->am_state == AMS_ACTIVE && !am->am_off &&
		    (!stop_early || !zfs_vdev_async_mirror_suspend_copy))
			*pendingp = B_TRUE;
		else
			error = ECANCELED;
	}
	mutex_exit(&am->am_lock);

	return (error);
}

/* Caller holds no lock; return the next copyable record and its retry mode. */
static async_mirror_txg_t *
async_mirror_commit_next(vdev_async_mirror_t *am, async_mirror_txg_t *last,
    boolean_t *quickp)
{
	mutex_enter(&am->am_lock);
	async_mirror_txg_t *next = list_next(&am->am_txgs, last);

	if (next != NULL && (!next->at_ready || am->am_giveup ||
	    zfs_vdev_async_mirror_suspend_copy ||
	    (!am->am_stop && async_mirror_passed(am, next))))
		next = NULL;
	*quickp = am->am_defer_quick;
	mutex_exit(&am->am_lock);
	return (next);
}

typedef struct async_mirror_copy_result {
	async_mirror_txg_t *acr_last;
	async_mirror_txg_t *acr_failed;
	uint64_t acr_bytes;
	uint_t acr_tries;
	async_mirror_err_t acr_kind;
	int acr_error;
	boolean_t acr_stopped;
	boolean_t acr_quick;
} async_mirror_copy_result_t;

/* Copy the ready txgs of one commit with one sort of their blocks. */
static async_mirror_txg_t *
async_mirror_copy_batch(vdev_async_mirror_t *am, async_mirror_io_t *io,
    async_mirror_txg_t *at, uint64_t *bytesp, int *errp,
    async_mirror_err_t *kindp, uint64_t *refusedp, int *refused_errp,
    uint64_t *rereadp)
{
	async_mirror_txg_t *last = NULL;
	async_mirror_txg_t *cur = at;
	async_mirror_err_t kind = ASYNC_MIRROR_ERR_WRITE;
	async_mirror_err_t wkind;
	uint64_t bytes = 0;
	uint64_t etxg = 0;
	uint64_t bad_txg = 0;
	boolean_t quick;
	uint_t ndef = 0;
	list_t def;
	int bad_err = 0;
	int err = 0;

	while (cur != NULL && bytes < ASYNC_MIRROR_COMMIT_BYTES) {
		err = async_mirror_copy(am, io, cur, B_FALSE, &kind);
		if (err != 0)
			break;
		last = cur;
		bytes += cur->at_bytes;
		cur = async_mirror_commit_next(am, last, &quick);
	}
	if (err != 0 && !async_mirror_err_ends(kind)) {
		if (kind == ASYNC_MIRROR_ERR_BLKPTR) {
			bad_txg = cur->at_txg;
			bad_err = err;
		}
		err = 0;
	}

	async_mirror_defer_list_create(&def);
	int werr = async_mirror_copy_wait(io, &wkind, &etxg, &def, &ndef);
	ASSERT0(ndef);
	list_destroy(&def);
	if (err == 0) {
		err = werr;
		kind = wkind;
	}

	*bytesp = 0;
	if (err != 0 && (async_mirror_err_ends(kind) ||
	    kind == ASYNC_MIRROR_ERR_STOP)) {
		*errp = err;
		*kindp = kind;
		return (NULL);
	}
	if (err != 0 && last != NULL) {
		ASSERT3U(etxg, >=, at->at_txg);
		ASSERT3U(etxg, <=, last->at_txg);
		if (etxg < at->at_txg || etxg > last->at_txg) {
			zfs_dbgmsg("async mirror: txg %llu: a block cannot be "
			    "read, error %d, and it is not of the txgs %llu to "
			    "%llu of the commit; nothing is committed",
			    (u_longlong_t)etxg, err,
			    (u_longlong_t)at->at_txg,
			    (u_longlong_t)last->at_txg);
			return (NULL);
		}
		zfs_dbgmsg("async mirror: txg %llu: a block cannot be read, "
		    "error %d; the commit ends before this txg",
		    (u_longlong_t)etxg, err);
		*rereadp = etxg;
		/* The record list can change while copy I/O runs. */
		mutex_enter(&am->am_lock);
		last = NULL;
		bytes = 0;
		for (cur = at; cur->at_txg < etxg;
		    cur = list_next(&am->am_txgs, cur)) {
			last = cur;
			bytes += cur->at_bytes;
		}
		mutex_exit(&am->am_lock);
	}
	if (bad_txg == at->at_txg) {
		*errp = bad_err;
		*kindp = ASYNC_MIRROR_ERR_BLKPTR;
		return (NULL);
	}
	if (bad_txg != 0) {
		*refusedp = bad_txg;
		*refused_errp = bad_err;
	}
	if (last != NULL)
		*bytesp = bytes;
	return (last);
}

/* Copy one txg at a time after a sorted batch or a read error. */
static void
async_mirror_copy_single(vdev_async_mirror_t *am, async_mirror_io_t *io,
    async_mirror_txg_t *at, uint_t retries,
    async_mirror_copy_result_t *result, list_t *def, uint_t *ndefp,
    uint64_t *refused_txgp, int *refused_errp)
{
	uint64_t etxg;
	boolean_t single = (result->acr_last == NULL &&
	    result->acr_error == 0);

	if (single) {
		result->acr_error = async_mirror_copy(am, io, at,
		    retries == 0 || result->acr_quick, &result->acr_kind);
		if (result->acr_error == 0) {
			result->acr_error = async_mirror_copy_wait(io,
			    &result->acr_kind, &etxg, def, ndefp);
		}
		result->acr_last = at;
		result->acr_bytes = at->at_bytes;
	}

	while (result->acr_error != 0 &&
	    result->acr_kind == ASYNC_MIRROR_ERR_META &&
	    result->acr_tries < retries) {
		if (result->acr_tries == 0)
			async_mirror_retry_event(am, at->at_txg,
			    result->acr_error);
		if (!async_mirror_retry_wait(am)) {
			result->acr_stopped = B_TRUE;
			break;
		}
		result->acr_tries++;
		result->acr_error = async_mirror_copy(am, io, at,
		    result->acr_tries == retries, &result->acr_kind);
		if (result->acr_error == 0) {
			result->acr_error = async_mirror_copy_wait(io,
			    &result->acr_kind, &etxg, def, ndefp);
		}
	}

	while (single && result->acr_error == 0 && !result->acr_stopped &&
	    *ndefp == 0 && result->acr_bytes < ASYNC_MIRROR_COMMIT_BYTES) {
		boolean_t quick;
		async_mirror_txg_t *next = async_mirror_commit_next(am,
		    result->acr_last, &quick);

		if (next == NULL)
			break;

		async_mirror_err_t kind;
		int error = async_mirror_copy(am, io, next,
		    retries == 0 || quick, &kind);

		if (error == 0)
			error = async_mirror_copy_wait(io, &kind, &etxg, def,
			    ndefp);
		if (error != 0) {
			if (async_mirror_err_ends(kind)) {
				result->acr_error = error;
				result->acr_kind = kind;
				result->acr_failed = next;
			} else if (kind == ASYNC_MIRROR_ERR_BLKPTR) {
				*refused_txgp = next->at_txg;
				*refused_errp = error;
			}
			break;
		}
		result->acr_last = next;
		result->acr_bytes += next->at_bytes;
		result->acr_quick = quick;
	}
}

/* Run one copy step.  The caller holds am_lock on entry and return. */
static async_mirror_copy_result_t
async_mirror_thread_copy(vdev_async_mirror_t *am, async_mirror_txg_t *at,
    boolean_t quick, uint64_t *refused_txgp, int *refused_errp,
    uint64_t *reread_txgp)
{
	async_mirror_copy_result_t result = {
		.acr_failed = at,
		.acr_kind = ASYNC_MIRROR_ERR_WRITE,
		.acr_quick = quick
	};
	async_mirror_io_t *io;
	uint_t retries;
	uint_t ndef = 0;
	list_t def;

	mutex_exit(&am->am_lock);
	io = async_mirror_io_create(am);
	retries = zfs_vdev_async_mirror_read_retries;
	async_mirror_defer_list_create(&def);
	if (!quick && retries > 0 && at->at_txg != *reread_txgp) {
		result.acr_last = async_mirror_copy_batch(am, io, at,
		    &result.acr_bytes, &result.acr_error, &result.acr_kind,
		    refused_txgp, refused_errp, reread_txgp);
	}
	async_mirror_copy_single(am, io, at, retries, &result, &def, &ndef,
	    refused_txgp, refused_errp);
	if (result.acr_error == 0 && !result.acr_stopped) {
		result.acr_failed = result.acr_last;
		result.acr_error = async_mirror_commit(am, result.acr_last,
		    result.acr_bytes > 0, &def, ndef, &result.acr_kind);
	}
	async_mirror_defer_list_free(&def);
	list_destroy(&def);
	async_mirror_io_destroy(io);
	mutex_enter(&am->am_lock);
	return (result);
}

	/* Publish the prefix under am_lock and wake work that depends on it. */
static void
async_mirror_thread_committed(vdev_async_mirror_t *am,
    spa_async_mirror_stats_t *sams, async_mirror_txg_t *at,
    const async_mirror_copy_result_t *result)
{
	zfs_dbgmsg("async mirror: txgs %llu to %llu are on the lagging "
	    "child, %llu bytes", (u_longlong_t)at->at_txg,
	    (u_longlong_t)result->acr_last->at_txg,
	    (u_longlong_t)result->acr_bytes);
	hrtime_t now = gethrtime();
	async_mirror_txg_t *walk = at;

	for (;;) {
		uint64_t lag = NSEC2MSEC(now - walk->at_ready_time);

		sams->sams_last_lag_ms = lag;
		if (lag > sams->sams_max_lag_ms)
			sams->sams_max_lag_ms = lag;
		if (!async_mirror_passed(am, walk)) {
			ASSERT3U(am->am_pending_txgs, >, 0);
			ASSERT3U(am->am_pending_bytes, >=, walk->at_bytes);
			am->am_pending_bytes -= walk->at_bytes;
			am->am_pending_txgs--;
		}
		if (walk == result->acr_last)
			break;
		walk = list_next(&am->am_txgs, walk);
	}
	am->am_applied_txg = result->acr_last->at_txg;
	async_mirror_progress(am);
	/* The held space of these txgs is ready for a release now. */
	async_mirror_ready_update(am);
	async_mirror_publish_txg(am);
	async_mirror_update_stats(am);
	spa_notify_waiters(am->am_spa);
	cv_broadcast(&am->am_drain_cv);
	if (am->am_release_zthr != NULL) {
		mutex_exit(&am->am_lock);
		zthr_wakeup(am->am_release_zthr);
		mutex_enter(&am->am_lock);
	}
}

/* Copy ready records until stop or error, then record the child state. */
__attribute__((noreturn)) void
async_mirror_thread(void *arg)
{
	vdev_async_mirror_t *am = arg;
	spa_async_mirror_stats_t *sams = &am->am_spa->spa_async_mirror_stats;
	/* A refused txg may not be the first of a commit. */
	uint64_t refused_txg = 0;
	int refused_err = 0;
	/* Retry a failed txg separately after its committed prefix. */
	uint64_t reread_txg = 0;
	boolean_t was_suspended = B_FALSE;

	mutex_enter(&am->am_lock);
	/* The copy starts now; see async_mirror_stall_check(). */
	async_mirror_progress(am);
	for (;;) {
		boolean_t suspended = zfs_vdev_async_mirror_suspend_copy;
		async_mirror_txg_t *at = NULL;

		if (!suspended) {
			for (at = list_head(&am->am_txgs); at != NULL;
			    at = list_next(&am->am_txgs, at)) {
				if (at->at_txg > am->am_applied_txg) {
					if (!at->at_ready || (!am->am_stop &&
					    async_mirror_passed(am, at)))
						at = NULL;
					break;
				}
			}
		}

		/* The copy starts again: its last progress is old. */
		if (was_suspended && !suspended)
			async_mirror_progress(am);
		was_suspended = suspended;

		if (am->am_defer_failed && async_mirror_copying(am->am_state)) {
			async_mirror_halt(am, am->am_applied_txg,
			    SET_ERROR(EIO), ASYNC_MIRROR_ERR_WRITE);
			break;
		}
		if (am->am_giveup && async_mirror_copying(am->am_state)) {
			async_mirror_halt(am, am->am_applied_txg,
			    SET_ERROR(ENOSPC), ASYNC_MIRROR_ERR_HELD);
			break;
		}
		if (at == NULL) {
			if (am->am_stop || !async_mirror_copying(am->am_state))
				break;
			if (suspended) {
				/* Nothing signals the end of the suspend. */
				(void) cv_timedwait_idle(&am->am_cv,
				    &am->am_lock, ddi_get_lbolt() + hz);
			} else {
				cv_wait_idle(&am->am_cv, &am->am_lock);
			}
			continue;
		}

		ASSERT(at->at_ready);
		ASSERT3U(at->at_txg, >, am->am_applied_txg);
		/* A refused block pointer freezes the child. */
		if (at->at_txg == refused_txg &&
		    async_mirror_copying(am->am_state)) {
			async_mirror_halt(am, at->at_txg, refused_err,
			    ASYNC_MIRROR_ERR_BLKPTR);
			break;
		}
		/* In an error episode, defer a lost data block at once. */
		boolean_t quick = am->am_defer_quick;
		async_mirror_copy_result_t result = async_mirror_thread_copy(am,
		    at, quick, &refused_txg, &refused_err, &reread_txg);

		/* Preserve records on export or unfinished deferred writes. */
		if (result.acr_stopped || (result.acr_error != 0 &&
		    result.acr_kind == ASYNC_MIRROR_ERR_STOP))
			break;
		if (result.acr_error != 0) {
			async_mirror_halt(am, result.acr_failed->at_txg,
			    result.acr_error, result.acr_kind);
			break;
		}
		if (result.acr_quick && result.acr_tries == 0 &&
		    result.acr_last->at_ndefer_count > 0) {
			zfs_dbgmsg("async mirror: txg %llu: earlier deferred "
			    "blocks wait for their tries; these were deferred "
			    "with no tries of the copy",
			    (u_longlong_t)result.acr_last->at_txg);
		}

		async_mirror_thread_committed(am, sams, at, &result);
	}
	ASSERT(am->am_stop || !async_mirror_copying(am->am_state));
	am->am_thread = NULL;
	cv_broadcast(&am->am_drain_cv);
	mutex_exit(&am->am_lock);
	thread_exit();
}

/* Free the in-memory part of a record.  The record stays on disk. */
static void
async_mirror_txg_free(async_mirror_txg_t *at)
{
	ASSERT(!list_link_active(&at->at_node));

	if (at->at_sm != NULL)
		space_map_close(at->at_sm);
	async_mirror_defer_list_free(&at->at_defer);
	list_destroy(&at->at_defer);
	kmem_free(at, sizeof (*at));
}

/* Persist the lagging marker on lag and clear stale markers on peers. */
void
async_mirror_mark(spa_t *spa, vdev_t *lag, uint64_t txg)
{
	vdev_t *rvd = spa->spa_root_vdev;

	ASSERT(spa_writeable(spa));
	ASSERT(spa_config_held(spa, SCL_CONFIG, RW_WRITER) ||
	    (dsl_pool_sync_context(spa_get_dsl(spa)) &&
	    spa_config_held(spa, SCL_CONFIG, RW_READER)));

	for (uint64_t c = 0; c < rvd->vdev_children; c++) {
		vdev_t *tvd = rvd->vdev_child[c];
		boolean_t dirty = B_FALSE;

		if (tvd->vdev_ops != &vdev_mirror_ops)
			continue;
		for (uint64_t i = 0; i < tvd->vdev_children; i++) {
			vdev_t *leaf = tvd->vdev_child[i];
			boolean_t want = (leaf == lag);

			/* Keep a set number that is there already. */
			if ((leaf->vdev_async_lagging != 0) == want)
				continue;
			leaf->vdev_async_lagging = want ?
			    ASYNC_MIRROR_CHILD_SET : 0;
			atomic_store_64(&leaf->vdev_async_txg, want ? txg : 0);
			dirty = B_TRUE;
			zfs_dbgmsg("async mirror: %s the mark of leaf guid "
			    "%llu", want ? "set" : "cleared",
			    (u_longlong_t)leaf->vdev_guid);
		}
		if (dirty)
			vdev_config_dirty(tvd);
	}
}

/* Update the missing DTL range until the label and uberblock are durable. */
void
async_mirror_fail_sync(vdev_async_mirror_t *am, dmu_tx_t *tx)
{
	spa_t *spa = am->am_spa;
	vdev_t *lag;
	vdev_t *rvd = spa->spa_root_vdev;
	uint64_t txg = dmu_tx_get_txg(tx);
	uint64_t from;
	uint64_t guid;
	boolean_t first;
	boolean_t durable;

	ASSERT(am->am_state == AMS_FAILED || am->am_dtl_child_guid != 0);
	from = am->am_fail_from;
	ASSERT3U(from, <=, txg);

	if (txg < am->am_fail_open_txg)
		return;
	/* Keep DTL maps unchanged after the final dirty txg of an export. */
	if (txg > spa_final_dirty_txg(spa))
		return;

	mutex_enter(&am->am_lock);
	first = (am->am_dtl_txg == 0);
	guid = am->am_dtl_child_guid;
	lag = am->am_lagging;
	durable = (am->am_dtl_child_txg != 0 &&
	    async_mirror_durable(am, am->am_dtl_child_txg));
	mutex_exit(&am->am_lock);
	if (durable || (first && zfs_vdev_async_mirror_suspend_release))
		return;
	if (lag == NULL && guid != 0)
		lag = vdev_lookup_by_guid(rvd, guid);
	if (lag == NULL)
		return;

	for (vdev_t *vd = lag; vd != rvd; vd = vd->vdev_parent)
		vdev_dtl_dirty(vd, DTL_PARTIAL, from, txg - from + 1);
	vdev_dtl_dirty(lag, DTL_MISSING, from, txg - from + 1);
	vdev_dirty(lag->vdev_top, VDD_DTL, lag, txg);
	if (first) {
		async_mirror_mark(spa, NULL, 0);
		spa_async_request(spa, SPA_ASYNC_RESILVER);

		async_mirror_child_phys_t amc;
		(void) async_mirror_child_get(spa, lag->vdev_guid, &amc);
		amc.amc_flags |= ASYNC_MIRROR_CHILD_DTL_PENDING;
		amc.amc_reserved[ASYNC_MIRROR_CHILD_DTL_TXG_WORD] = txg;
		async_mirror_child_put(spa, lag->vdev_guid, &amc, tx);
	}

	mutex_enter(&am->am_lock);
	if (first) {
		am->am_dtl_txg = txg;
		am->am_dtl_child_clearing = B_FALSE;
	}
	am->am_dtl_range_txg = txg;
	am->am_dtl_label_txg = am->am_dtl_label_write_txg = 0;
	am->am_dtl_ub_txg = am->am_dtl_ub_write_txg = 0;
	am->am_dtl_child_txg = 0;
	if (first)
		am->am_dtl_child_guid = lag->vdev_guid;
	if (am->am_halt_txg == 0)
		am->am_halt_txg = txg;
	async_mirror_update_stats(am);
	if (first)
		cv_broadcast(&am->am_drain_cv);
	mutex_exit(&am->am_lock);

	if (first)
		spa_history_log_internal(spa, "async mirror", tx,
		    "the lagging child guid %llu misses txgs %llu to %llu; it "
		    "is resilvered",
		    (u_longlong_t)lag->vdev_guid, (u_longlong_t)from,
		    (u_longlong_t)txg);
	if (am->am_release_zthr != NULL)
		zthr_wakeup(am->am_release_zthr);
}

/* Do not release held ranges while the pool is frozen. */
static boolean_t
async_mirror_release_frozen(vdev_async_mirror_t *am)
{
	return (spa_freeze_txg(am->am_spa) != UINT64_MAX);
}

/* Caller holds am_lock. */
static boolean_t
async_mirror_freeze_due(vdev_async_mirror_t *am)
{
	ASSERT(MUTEX_HELD(&am->am_lock));

	return (am->am_state == AMS_FROZEN &&
	    (!am->am_frozen_entry || am->am_frozen_stale));
}

/* The entries between two looks at the clock in a release; a power of 2. */
#define	ASYNC_MIRROR_RELEASE_CHECK	1024

typedef struct async_mirror_release_arg {
	vdev_async_mirror_t	*ara_am;
	async_mirror_txg_t	*ara_at;	/* the current record */
	uint64_t	ara_txg;
	uint64_t	ara_limit;	/* SM_FREE entries it may release */
	hrtime_t	ara_deadline;	/* when it must stop, or 0 */
	uint64_t	ara_entries;	/* entries of both types seen */
	boolean_t	ara_stop;	/* a limit ended this release */
	uint64_t	ara_bytes;	/* released from the current record */
	uint64_t	ara_dspace;
	uint64_t	ara_log_bytes;
} async_mirror_release_arg_t;

/* True if this release reached its deadline. */
static boolean_t
async_mirror_release_expired(const async_mirror_release_arg_t *ara)
{
	return (ara->ara_deadline != 0 && gethrtime() > ara->ara_deadline);
}

/* Release one free entry; periodic checks ensure progress. */
static int
async_mirror_release_cb(space_map_entry_t *sme, void *arg)
{
	async_mirror_release_arg_t *ara = arg;
	vdev_async_mirror_t *am = ara->ara_am;

	if ((++ara->ara_entries & (ASYNC_MIRROR_RELEASE_CHECK - 1)) == 0 &&
	    async_mirror_release_expired(ara)) {
		ara->ara_stop = B_TRUE;
		return (SET_ERROR(EINTR));
	}
	if (sme->sme_type != SM_FREE)
		return (0);

	spa_t *spa = am->am_spa;
	vdev_t *vd = vdev_lookup_top(spa, sme->sme_vdev);

	/* The import checked each entry; see async_mirror_scan_cb(). */
	ASSERT3P(vd, !=, NULL);
	ASSERT3U(sme->sme_offset >> vd->vdev_ms_shift, <, vd->vdev_ms_count);

	metaslab_t *msp = vd->vdev_ms[sme->sme_offset >> vd->vdev_ms_shift];

	ASSERT3U(sme->sme_offset + sme->sme_run, <=,
	    msp->ms_start + msp->ms_size);

	if (ara->ara_limit == 0) {
		ara->ara_stop = B_TRUE;
		return (SET_ERROR(EINTR));
	}
	ara->ara_limit--;

	mutex_enter(&msp->ms_lock);
	if (zfs_range_tree_is_empty(msp->ms_freeing))
		vdev_dirty(vd, VDD_METASLAB, msp, ara->ara_txg);
	/* ms_lock protects the range as it enters the metaslab's free list. */
	zfs_range_tree_add(msp->ms_freeing, sme->sme_offset, sme->sme_run);
	mutex_exit(&msp->ms_lock);

	ara->ara_bytes += sme->sme_run;
	ara->ara_dspace += async_mirror_held_dspace(vd, sme->sme_run);
	if (vd->vdev_islog)
		ara->ara_log_bytes += sme->sme_run;
	return (0);
}

/* Clear the persistent DTL fence after proof and repairs are complete. */
static void
async_mirror_release_clear_dtl(vdev_async_mirror_t *am, dmu_tx_t *tx)
{
	uint64_t guid;
	boolean_t cleared = B_FALSE;

	/* am_lock protects the pending proof and repair count. */
	mutex_enter(&am->am_lock);
	if (am->am_dtl_child_guid == 0 || am->am_dtl_child_txg == 0 ||
	    am->am_repair_count != 0 || am->am_dtl_child_clearing ||
	    !async_mirror_durable(am, am->am_dtl_child_txg)) {
		mutex_exit(&am->am_lock);
		return;
	}
	guid = am->am_dtl_child_guid;
	am->am_dtl_child_clearing = B_TRUE;
	mutex_exit(&am->am_lock);

	async_mirror_child_flags(am->am_spa, guid, 0,
	    ASYNC_MIRROR_CHILD_DTL_PENDING, tx);

	/* Recheck the durable proof after the MOS entry is updated. */
	mutex_enter(&am->am_lock);
	if (am->am_dtl_child_guid == guid &&
	    async_mirror_durable(am, am->am_dtl_child_txg)) {
		cleared = B_TRUE;
		am->am_dtl_child_guid = 0;
		am->am_dtl_child_txg = 0;
		am->am_dtl_label_txg = 0;
		am->am_dtl_label_write_txg = 0;
		am->am_dtl_ub_txg = 0;
		am->am_dtl_ub_write_txg = 0;
		am->am_dtl_txg = 0;
		am->am_dtl_range_txg = 0;
		if (am->am_state == AMS_FAILED) {
			async_mirror_set_state(am,
			    (list_is_empty(&am->am_txgs) &&
			    am->am_zap_obj == 0 && am->am_defer_zap == 0) ?
			    AMS_OFF : AMS_DRAINING);
		}
		async_mirror_entry_guid_update(am);
		async_mirror_update_stats(am);
	}
	am->am_dtl_child_clearing = B_FALSE;
	mutex_exit(&am->am_lock);

	/* Restart in a later txg so the resilver covers every DTL range. */
	if (cleared) {
		dsl_pool_t *dp = am->am_spa->spa_dsl_pool;

		if (dsl_scan_resilvering(dp))
			dsl_scan_restart_resilver(dp, dmu_tx_get_txg(tx) + 1);
		else
			spa_async_request(am->am_spa, SPA_ASYNC_RESILVER);
	}
}

/* Record a freeze before the batch can release any of its records. */
static void
async_mirror_release_freeze(vdev_async_mirror_t *am, dmu_tx_t *tx,
    uint64_t txg)
{
	spa_t *spa = am->am_spa;
	boolean_t freeze;
	boolean_t held;
	uint_t why;

	/* Snapshot the freeze cause while am_lock is held. */
	mutex_enter(&am->am_lock);
	freeze = async_mirror_freeze_due(am);
	held = am->am_giveup;
	why = am->am_giveup_why;
	mutex_exit(&am->am_lock);
	if (freeze) {
		uint64_t guid = am->am_lagging->vdev_guid;
		async_mirror_child_phys_t amc;

		ASSERT(am->am_entry_guid == 0 || am->am_entry_guid == guid);
		(void) async_mirror_child_get(spa, guid, &amc);
		amc.amc_flags |= ASYNC_MIRROR_CHILD_FROZEN;
		amc.amc_applied_txg = am->am_applied_txg;
		amc.amc_frozen_txg = am->am_applied_txg;
		amc.amc_frozen_reason = held ? ASYNC_MIRROR_FROZEN_HELD :
		    ASYNC_MIRROR_FROZEN_METADATA;
		amc.amc_frozen_from = am->am_frozen_from;
		amc.amc_reserved_frozen_flags = 0;
		amc.amc_reserved_giveups = 0;
		amc.amc_reserved_frozen_time = 0;
		amc.amc_reserved_12 = 0;
		async_mirror_child_put(spa, guid, &amc, tx);

		mutex_enter(&am->am_lock);
		am->am_entry_guid = guid;
		am->am_frozen_entry = B_TRUE;
		am->am_frozen_stale = B_FALSE;
		am->am_entry_txg = txg;
		mutex_exit(&am->am_lock);

		mutex_enter(&am->am_lock);
		if (am->am_halt_txg == 0)
			am->am_halt_txg = txg;
		mutex_exit(&am->am_lock);
		spa_history_log_internal(spa, "async mirror", tx,
		    "%s; the lagging child guid %llu is frozen "
		    "at txg %llu", held ?
		    async_mirror_giveup_text(why) : "a block cannot be read",
		    (u_longlong_t)guid, (u_longlong_t)am->am_applied_txg);
	}
	async_mirror_frozen_clear(am, tx);
}

/* Release one record and account for every range it frees. */
static int
async_mirror_release_record(vdev_async_mirror_t *am,
    async_mirror_release_arg_t *ara, async_mirror_txg_t *at,
    dmu_tx_t *tx)
{
	objset_t *mos = spa_meta_objset(am->am_spa);
	space_map_t *sm = NULL;
	int error;

	ASSERT3U(am->am_zap_obj, !=, 0);
	VERIFY0(space_map_open(&sm, mos, at->at_obj, 0, UINT64_MAX,
	    SPA_MINBLOCKSHIFT));
	ara->ara_at = at;
	error = space_map_incremental_destroy(sm, async_mirror_release_cb,
	    ara, tx);
	mutex_enter(&am->am_lock);
	ASSERT3U(am->am_held_bytes, >=, ara->ara_bytes);
	ASSERT3U(am->am_held_dspace, >=, ara->ara_dspace);
	ASSERT3U(am->am_held_log_bytes, >=, ara->ara_log_bytes);
	am->am_held_bytes -= ara->ara_bytes;
	am->am_held_dspace -= ara->ara_dspace;
	am->am_held_log_bytes -= ara->ara_log_bytes;
	ara->ara_at->at_held_dspace -= MIN(ara->ara_at->at_held_dspace,
	    ara->ara_dspace);
	am->am_ready_dspace -= MIN(am->am_ready_dspace, ara->ara_dspace);
	async_mirror_update_stats(am);
	mutex_exit(&am->am_lock);
	ara->ara_bytes = ara->ara_dspace = ara->ara_log_bytes = 0;

	if (error != 0) {
		if (error != EINTR) {
			zfs_panic_recover("zfs: error %d releasing "
			    "async mirror record of txg %llu", error,
			    (u_longlong_t)at->at_txg);
		}
		/* Keep the remaining map length for the next batch. */
		mutex_enter(&am->am_lock);
		at->at_sm_len = space_map_length(sm);
		mutex_exit(&am->am_lock);
		space_map_close(sm);
		return (error);
	}
	space_map_free(sm, tx);
	space_map_close(sm);
	VERIFY0(zap_remove_uint64(mos, am->am_zap_obj, &at->at_txg, 1, tx));
	if (am->am_defer_zap != 0)
		async_mirror_defer_clear(am, at->at_txg, tx);

	mutex_enter(&am->am_lock);
	if (at->at_ndefer > 0)
		async_mirror_defer_drop(am, at);
	list_remove(&am->am_txgs, at);
	if (async_mirror_draining(am->am_state) &&
	    list_is_empty(&am->am_txgs)) {
		am->am_pending_bytes = am->am_pending_txgs = 0;
		async_mirror_update_stats(am);
	}
	mutex_exit(&am->am_lock);
	async_mirror_txg_free(at);
	return (0);
}

/* Release ready records within the entry and time limits in ara. */
static boolean_t
async_mirror_release_records(vdev_async_mirror_t *am,
    async_mirror_release_arg_t *ara, dmu_tx_t *tx)
{
	boolean_t released = B_FALSE;

	/* A draining sync task must not wait for a deferred child write. */
	mutex_enter(&am->am_lock);
	if (async_mirror_draining(am->am_state) && am->am_defer_io) {
		boolean_t more = !list_is_empty(&am->am_txgs);

		mutex_exit(&am->am_lock);
		return (more);
	}
	boolean_t keep = (am->am_state == AMS_FAILED && am->am_dtl_txg == 0);
	mutex_exit(&am->am_lock);

	for (;;) {
		mutex_enter(&am->am_lock);
		async_mirror_txg_t *at = list_head(&am->am_txgs);
		boolean_t draining = async_mirror_draining(am->am_state);
		/*
		 * Principle 1: no txg above am_applied_txg reaches the lagging
		 * child.
		 */
		/* am_lock protects the applied txg and defer count. */
		boolean_t ok = at != NULL && !keep && (draining ||
		    (at->at_ready && at->at_txg <= am->am_applied_txg &&
		    at->at_ndefer == 0));
		mutex_exit(&am->am_lock);
		if (!ok)
			return (B_FALSE);

		/* Principle 2: limits prevent an unbounded release wait. */
		if (ara->ara_stop || ara->ara_limit == 0 ||
		    (released && async_mirror_release_expired(ara)))
			return (B_TRUE);

		int error = async_mirror_release_record(am, ara, at, tx);
		if (error != 0)
			return (error == EINTR);
		released = B_TRUE;
	}
}

/* Tear down the ZAPs only after the final record has been released. */
static void
async_mirror_release_done(vdev_async_mirror_t *am, dmu_tx_t *tx)
{
	spa_t *spa = am->am_spa;
	objset_t *mos = spa_meta_objset(spa);
	uint64_t gone = 0;

	mutex_enter(&am->am_lock);
	boolean_t drained = async_mirror_draining(am->am_state) &&
	    list_is_empty(&am->am_txgs);
	mutex_exit(&am->am_lock);
	if (!drained)
		return;

	if (am->am_zap_obj != 0) {
		ASSERT0(am->am_held_bytes);
		ASSERT0(am->am_held_dspace);
		ASSERT0(am->am_held_log_bytes);
		VERIFY0(zap_destroy(mos, am->am_zap_obj, tx));
		VERIFY0(zap_remove(mos, DMU_POOL_DIRECTORY_OBJECT,
		    DMU_POOL_ASYNC_MIRROR_ZAP, tx));
		spa_feature_decr(spa, SPA_FEATURE_ASYNC_MIRROR, tx);
		gone |= ASYNC_MIRROR_CHILD_LAGGING;
		mutex_enter(&am->am_lock);
		am->am_zap_obj = 0;
		mutex_exit(&am->am_lock);
		spa_history_log_internal(spa, "async mirror", tx,
		    "released the last record");
	}
	if (am->am_defer_zap != 0) {
		VERIFY0(zap_destroy(mos, am->am_defer_zap, tx));
		VERIFY0(zap_remove(mos, DMU_POOL_DIRECTORY_OBJECT,
		    DMU_POOL_ASYNC_MIRROR_DEFERRED, tx));
		mutex_enter(&am->am_lock);
		am->am_defer_zap = 0;
		mutex_exit(&am->am_lock);
	}
	if (am->am_off_entry) {
		gone |= ASYNC_MIRROR_CHILD_OFF;
		mutex_enter(&am->am_lock);
		am->am_off_entry = B_FALSE;
		mutex_exit(&am->am_lock);
	}
	if (gone != 0) {
		async_mirror_child_flags(spa, am->am_entry_guid, 0, gone, tx);
		mutex_enter(&am->am_lock);
		async_mirror_entry_guid_update(am);
		mutex_exit(&am->am_lock);
	}
}

/* Sync task for freeze changes and record release. */
static void
async_mirror_release_sync(void *arg, dmu_tx_t *tx)
{
	vdev_async_mirror_t *am = arg;
	spa_t *spa = am->am_spa;
	objset_t *mos = spa_meta_objset(spa);
	uint64_t txg = dmu_tx_get_txg(tx);

	ASSERT(dsl_pool_sync_context(dmu_objset_pool(mos)));
	ASSERT3U(txg, ==, spa_syncing_txg(spa));

	if (txg > spa_freeze_txg(spa))
		return;

	/* Keep the DTL fence until durable proof and repairs are complete. */
	async_mirror_release_clear_dtl(am, tx);

	/* Sync tasks run before vdev_sync(); their frees land in this txg. */
	/* The first MOS pass distinguishes this release from other changes. */
	boolean_t only = BP_GET_LOGICAL_BIRTH(&spa->spa_uberblock.ub_rootbp) <
	    txg && !dmu_objset_is_dirty(mos, txg);

	async_mirror_release_freeze(am, tx, txg);

	/* Mark this txg as release-only before record creation. */
	mutex_enter(&am->am_lock);
	ASSERT(list_is_empty(&am->am_txgs) ||
	    ((async_mirror_txg_t *)list_tail(&am->am_txgs))->at_txg < txg);
	/* Preserve the first release decision for this txg. */
	if (am->am_last_release_txg != txg)
		am->am_release_only_txg = only ? txg : 0;
	am->am_last_release_txg = txg;
	mutex_exit(&am->am_lock);

	uint64_t limit = zfs_vdev_async_mirror_release_limit;
	uint_t ms = zfs_vdev_async_mirror_release_ms;
	async_mirror_release_arg_t ara = {
		.ara_am = am,
		.ara_txg = txg,
		.ara_limit = (limit != 0) ? limit : UINT64_MAX,
		.ara_deadline = (ms != 0) ? gethrtime() + MSEC2NSEC(ms) : 0
	};
	/* Principle 2: bound each release by its configured limits. */
	boolean_t more = async_mirror_release_records(am, &ara, tx);

	mutex_enter(&am->am_lock);
	am->am_release_more = more;
	am->am_release_capped = (more && limit != 0 && ara.ara_limit == 0);
	async_mirror_ready_update(am);
	mutex_exit(&am->am_lock);

	async_mirror_release_done(am, tx);
	mutex_enter(&am->am_lock);
	if (am->am_state == AMS_DRAINING && list_is_empty(&am->am_txgs) &&
	    am->am_zap_obj == 0 && am->am_defer_zap == 0 &&
	    am->am_dtl_child_guid == 0 && !am->am_off_entry &&
	    !am->am_frozen_entry)
		async_mirror_set_state(am, AMS_OFF);
	mutex_exit(&am->am_lock);

	/* Continue work that did not fit in this txg. */
	if (am->am_release_zthr != NULL)
		zthr_wakeup(am->am_release_zthr);
}

/* Caller holds am_lock. */
static boolean_t
async_mirror_release_urgent(vdev_async_mirror_t *am)
{
	ASSERT(MUTEX_HELD(&am->am_lock));

	return (async_mirror_draining(am->am_state) ||
	    async_mirror_freeze_due(am) ||
	    async_mirror_frozen_clear_due(am) ||
	    async_mirror_held_urgent(am));
}

/* Caller holds am_lock. */
static boolean_t
async_mirror_release_wanted(vdev_async_mirror_t *am)
{
	ASSERT(MUTEX_HELD(&am->am_lock));

	async_mirror_ready_update(am);
	async_mirror_txg_t *at = list_head(&am->am_txgs);
	boolean_t draining = async_mirror_draining(am->am_state);
	boolean_t pending = (am->am_dtl_child_guid != 0 &&
	    am->am_dtl_child_txg != 0 &&
	    async_mirror_durable(am, am->am_dtl_child_txg)) ||
	    async_mirror_freeze_due(am) || async_mirror_frozen_clear_due(am);

	if (!pending && at == NULL)
		pending = draining && (am->am_zap_obj != 0 ||
		    am->am_defer_zap != 0 || am->am_off_entry);
	else if (!pending && (draining || am->am_release_more))
		pending = B_TRUE;
	else if (!pending) {
		for (; at != NULL && at->at_ready &&
		    at->at_txg <= am->am_applied_txg;
		    at = list_next(&am->am_txgs, at)) {
			if (at->at_ndefer > 0)
				break;
			if (!at->at_release) {
				pending = B_TRUE;
				break;
			}
		}
	}
	if (!pending &&
	    (am->am_ready_dspace == 0 || !async_mirror_held_urgent(am)))
		return (B_FALSE);
	return (async_mirror_release_urgent(am) ||
	    am->am_release_queued_txg <= am->am_last_release_txg);
}

/* zthr check for record release or deferred-block retry work. */
boolean_t
async_mirror_release_thread_check(void *arg, zthr_t *zthr)
{
	(void) zthr;
	vdev_async_mirror_t *am = arg;

	if (zfs_vdev_async_mirror_suspend_release ||
	    async_mirror_release_frozen(am))
		return (B_FALSE);

	mutex_enter(&am->am_lock);
	boolean_t wanted = async_mirror_release_wanted(am) ||
	    async_mirror_defer_next(am) != NULL;
	mutex_exit(&am->am_lock);
	return (wanted);
}

/* The records that one step of the prefetch takes under am_lock. */
#define	ASYNC_MIRROR_PREFETCH_RECS	32

/*
 * The most bytes of space maps that one release prefetches: more than a
 * release reads in its time limit.
 */
#define	ASYNC_MIRROR_PREFETCH_BYTES	(32ULL << 20)

/* Release records through sync tasks; ordinary release does not force txg. */
void
async_mirror_release_thread(void *arg, zthr_t *zthr)
{
	vdev_async_mirror_t *am = arg;
	spa_t *spa = am->am_spa;
	dsl_pool_t *dp = spa_get_dsl(spa);

	for (;;) {
		if (zthr_iscancelled(zthr) ||
		    zfs_vdev_async_mirror_suspend_release ||
		    async_mirror_release_frozen(am))
			return;

		mutex_enter(&am->am_lock);
		if (!async_mirror_release_wanted(am)) {
			boolean_t defer = (async_mirror_defer_next(am) != NULL);

			mutex_exit(&am->am_lock);
			if (!defer)
				return;
			async_mirror_defer_retry(am, zthr);
			continue;
		}
		boolean_t urgent = async_mirror_release_urgent(am);
		uint64_t held = am->am_held_dspace;
		mutex_exit(&am->am_lock);
		/* Prefetch release maps so sync-thread reads do not block. */
		objset_t *mos = spa_meta_objset(spa);
		uint64_t obj[ASYNC_MIRROR_PREFETCH_RECS];
		uint64_t off[ASYNC_MIRROR_PREFETCH_RECS];
		uint64_t len[ASYNC_MIRROR_PREFETCH_RECS];
		uint64_t last_txg = 0;
		uint64_t bytes = 0;
		uint_t n;

		do {
			n = 0;
			mutex_enter(&am->am_lock);
			for (async_mirror_txg_t *at = list_head(&am->am_txgs);
			    at != NULL && at->at_ready &&
			    n < ASYNC_MIRROR_PREFETCH_RECS &&
			    bytes < ASYNC_MIRROR_PREFETCH_BYTES;
			    at = list_next(&am->am_txgs, at)) {
				if (!async_mirror_draining(am->am_state) &&
				    at->at_txg > am->am_applied_txg)
					break;
				if (at->at_txg <= last_txg)
					continue;
				last_txg = at->at_txg;
				uint64_t take = MIN(at->at_sm_len,
				    ASYNC_MIRROR_PREFETCH_BYTES - bytes);

				obj[n] = at->at_obj;
				off[n] = at->at_sm_len - take;
				len[n] = take;
				bytes += take;
				n++;
			}
			mutex_exit(&am->am_lock);

			for (uint_t i = 0; i < n; i++) {
				if (len[i] == 0)
					continue;
				dmu_prefetch(mos, obj[i], 0, off[i], len[i],
				    ZIO_PRIORITY_ASYNC_READ);
			}
		} while (n == ASYNC_MIRROR_PREFETCH_RECS &&
		    bytes < ASYNC_MIRROR_PREFETCH_BYTES);

		if (urgent) {
			VERIFY0(dsl_sync_task(spa_name(spa), NULL,
			    async_mirror_release_sync, am, 0,
			    ZFS_SPACE_CHECK_NONE));
			/* Check whether the forced release freed space. */
			mutex_enter(&am->am_lock);
			am->am_release_futile_txg =
			    (am->am_held_dspace >= held) ?
			    am->am_last_release_txg : 0;
			mutex_exit(&am->am_lock);
			continue;
		}

		dmu_tx_t *tx = dmu_tx_create_dd(dp->dp_mos_dir);

		VERIFY0(dmu_tx_assign(tx, DMU_TX_WAIT | DMU_TX_SUSPEND));
		mutex_enter(&am->am_lock);
		am->am_release_queued_txg = dmu_tx_get_txg(tx);
		mutex_exit(&am->am_lock);
		dsl_sync_task_nowait(dp, async_mirror_release_sync, am, tx);
		dmu_tx_commit(tx);

		mutex_enter(&am->am_lock);
		boolean_t defer = (async_mirror_defer_next(am) != NULL);
		mutex_exit(&am->am_lock);
		if (!defer)
			return;
		async_mirror_defer_retry(am, zthr);
	}
}

/* Check give-up limits once a second. */
static boolean_t
async_mirror_giveup_thread_check(void *arg, zthr_t *zthr)
{
	(void) zthr;
	vdev_async_mirror_t *am = arg;

	mutex_enter(&am->am_lock);
	boolean_t due = (async_mirror_copying(am->am_state) && !am->am_stop);
	mutex_exit(&am->am_lock);
	return (due);
}

static void
async_mirror_giveup_thread(void *arg, zthr_t *zthr)
{
	(void) zthr;
	vdev_async_mirror_t *am = arg;
	uint64_t avail = async_mirror_allocator_avail(am->am_spa);

	mutex_enter(&am->am_lock);
	if (async_mirror_copying(am->am_state) && !am->am_stop)
		async_mirror_giveup_check(am, avail);
	mutex_exit(&am->am_lock);
}

/* Create the release and give-up timer threads when the pool opens. */
void
vdev_async_mirror_spawn(spa_t *spa)
{
	vdev_async_mirror_t *am = spa->spa_async_mirror;

	if (am == NULL)
		return;

	ASSERT3P(am->am_release_zthr, ==, NULL);
	ASSERT3P(am->am_giveup_zthr, ==, NULL);
	am->am_release_zthr = zthr_create(ASYNC_MIRROR_RELEASE_THREAD,
	    async_mirror_release_thread_check, async_mirror_release_thread, am,
	    minclsyspri);
	am->am_giveup_zthr = async_mirror_giveup_zthr_create(am);
}

zthr_t *
async_mirror_giveup_zthr_create(vdev_async_mirror_t *am)
{
	return (zthr_create_timer(ASYNC_MIRROR_GIVEUP_THREAD,
	    async_mirror_giveup_thread_check, async_mirror_giveup_thread, am,
	    ASYNC_MIRROR_GIVEUP_TICK, minclsyspri));
}

/* Finish any deferred-block wait before the pool stops its sync thread. */
void
vdev_async_mirror_zthr_cancel(spa_t *spa)
{
	vdev_async_mirror_t *am = spa->spa_async_mirror;

	if (am == NULL)
		return;
	mutex_enter(&am->am_lock);
	am->am_quiesce++;
	while (am->am_persist == AMP_SYNC)
		cv_wait(&am->am_drain_cv, &am->am_lock);
	mutex_exit(&am->am_lock);
	if (am->am_release_zthr != NULL)
		zthr_cancel(am->am_release_zthr);
	if (am->am_giveup_zthr != NULL)
		zthr_cancel(am->am_giveup_zthr);
}

/* Let the release thread and the worker run again. */
void
vdev_async_mirror_zthr_resume(spa_t *spa)
{
	vdev_async_mirror_t *am = spa->spa_async_mirror;

	if (am == NULL)
		return;
	mutex_enter(&am->am_lock);
	if (am->am_quiesce > 0)
		am->am_quiesce--;
	cv_broadcast(&am->am_cv);
	mutex_exit(&am->am_lock);
	if (am->am_release_zthr != NULL)
		zthr_resume(am->am_release_zthr);
	if (am->am_giveup_zthr != NULL)
		zthr_resume(am->am_giveup_zthr);
}

/* The state of one record read from the ZAP at import. */
typedef struct async_mirror_load_rec {
	uint64_t	alr_txg;
	uint64_t	alr_obj;
	uberblock_t	alr_prev_ub;
} async_mirror_load_rec_t;

typedef struct async_mirror_scan_arg {
	spa_t		*asa_spa;
	uint64_t	asa_alloc;
	uint64_t	asa_held;
	uint64_t	asa_held_dspace;
	uint64_t	asa_held_log;
	/* The held ranges of every record so far, by top-level vdev id. */
	zfs_range_tree_t **asa_free;
	uint64_t	asa_nfree;
} async_mirror_scan_arg_t;

/* Reject invalid or overlapping space-map entries before release. */
static int
async_mirror_scan_cb(space_map_entry_t *sme, void *arg)
{
	async_mirror_scan_arg_t *asa = arg;
	vdev_t *vd = vdev_lookup_top(asa->asa_spa, sme->sme_vdev);
	uint64_t offset = sme->sme_offset;
	uint64_t run = sme->sme_run;

	if (vd == NULL || vd->vdev_id >= asa->asa_nfree || run == 0 ||
	    offset + run < offset ||
	    (offset >> vd->vdev_ms_shift) >= vd->vdev_ms_count)
		return (SET_ERROR(ECKSUM));

	metaslab_t *msp = vd->vdev_ms[offset >> vd->vdev_ms_shift];

	if (offset + run > msp->ms_start + msp->ms_size)
		return (SET_ERROR(ECKSUM));

	if (sme->sme_type == SM_ALLOC) {
		if (UINT64_MAX - asa->asa_alloc < run)
			return (SET_ERROR(ECKSUM));
		asa->asa_alloc += run;
		return (0);
	}
	if (sme->sme_type != SM_FREE)
		return (SET_ERROR(ECKSUM));

	if (vd->vdev_id >= asa->asa_nfree)
		return (SET_ERROR(ECKSUM));

	zfs_range_tree_t **rtp = &asa->asa_free[vd->vdev_id];

	if (*rtp == NULL) {
		*rtp = zfs_range_tree_create(NULL, ZFS_RANGE_SEG64, NULL, 0,
		    0);
	}
	uint64_t dspace = async_mirror_held_dspace(vd, run);
	uint64_t start;
	uint64_t size;

	if (zfs_range_tree_find_in(*rtp, offset, run, &start, &size) ||
	    UINT64_MAX - asa->asa_held < run ||
	    UINT64_MAX - asa->asa_held_dspace < dspace ||
	    (vd->vdev_islog && UINT64_MAX - asa->asa_held_log < run))
		return (SET_ERROR(ECKSUM));
	zfs_range_tree_add(*rtp, offset, run);
	asa->asa_held += run;
	asa->asa_held_dspace += dspace;
	if (vd->vdev_islog)
		asa->asa_held_log += run;
	return (0);
}

/* Iterate records and return the first read, validation, or callback error. */
int
async_mirror_rec_iterate(objset_t *mos, uint64_t zap,
    async_mirror_rec_cb_t *cb, void *arg)
{
	async_mirror_rec_phys_t rec;
	zap_cursor_t zc;
	zap_attribute_t *za = zap_attribute_alloc();
	int error;

	for (zap_cursor_init(&zc, mos, zap);
	    (error = zap_cursor_retrieve(&zc, za)) == 0;
	    zap_cursor_advance(&zc)) {
		if (za->za_integer_length != sizeof (uint64_t) ||
		    za->za_num_integers != ASYNC_MIRROR_REC_WORDS) {
			error = SET_ERROR(EINVAL);
			break;
		}
		const uint64_t *txg = (const uint64_t *)za->za_name;

		error = zap_lookup_uint64(mos, zap, txg, 1, sizeof (uint64_t),
		    ASYNC_MIRROR_REC_WORDS, &rec);
		if (error == 0 && rec.amr_version != ASYNC_MIRROR_REC_VERSION)
			error = SET_ERROR(EINVAL);
		if (error == 0 && rec.amr_flags != 0)
			error = SET_ERROR(ENOTSUP);
		for (uint64_t i = sizeof (uberblock_t) / sizeof (uint64_t);
		    error == 0 && i < ASYNC_MIRROR_REC_UB_WORDS; i++) {
			if (rec.amr_ub[i] != 0)
				error = SET_ERROR(ENOTSUP);
		}
		if (error == 0)
			error = cb(*txg, &rec, arg);
		if (error != 0)
			break;
	}
	zap_cursor_fini(&zc);
	zap_attribute_free(za);
	return (error == ENOENT ? 0 : error);
}

typedef struct async_mirror_load_arg {
	async_mirror_load_rec_t	*ala_recs;
	uint64_t		ala_count;
	uint64_t		ala_n;
} async_mirror_load_arg_t;

static int
async_mirror_load_rec_cb(uint64_t txg, const async_mirror_rec_phys_t *rec,
    void *arg)
{
	async_mirror_load_arg_t *ala = arg;

	if (ala->ala_n == ala->ala_count)
		return (SET_ERROR(EINVAL));

	async_mirror_load_rec_t *r = &ala->ala_recs[ala->ala_n++];

	r->alr_txg = txg;
	r->alr_obj = rec->amr_sm_obj;
	memcpy(&r->alr_prev_ub, rec->amr_ub, sizeof (uberblock_t));
	return (0);
}

static int
async_mirror_load_rec_compare(const void *a, const void *b)
{
	const async_mirror_load_rec_t *ra = a;
	const async_mirror_load_rec_t *rb = b;

	return (TREE_CMP(ra->alr_txg, rb->alr_txg));
}

/* Count by walk; the on-disk ZAP count is untrusted at import. */
static int
async_mirror_zap_entries(objset_t *mos, uint64_t zap, uint64_t *countp)
{
	zap_cursor_t zc;
	zap_attribute_t *za = zap_attribute_alloc();
	uint64_t count = 0;
	int error;

	for (zap_cursor_init(&zc, mos, zap);
	    (error = zap_cursor_retrieve(&zc, za)) == 0;
	    zap_cursor_advance(&zc))
		count++;
	zap_cursor_fini(&zc);
	zap_attribute_free(za);
	*countp = count;
	return (error == ENOENT ? 0 : error);
}

/* Check multiplication and the import-size limit. */
static int
async_mirror_load_size(spa_t *spa, const char *what, uint64_t count,
    size_t each, size_t *sizep)
{
	uint64_t limit = zfs_vdev_async_mirror_load_max_bytes;

	if (each == 0 || count > UINT64_MAX / each ||
	    count > SIZE_MAX / each ||
	    (limit != 0 && count > limit / each)) {
		cmn_err(CE_WARN, "pool '%s': the async mirror has %llu %s, "
		    "more than an import reads in "
		    "zfs_vdev_async_mirror_load_max_bytes (%llu bytes); raise "
		    "that limit, or set it to 0, to import the pool",
		    spa_name(spa), (u_longlong_t)count, what,
		    (u_longlong_t)limit);
		spa_load_note(spa, "async mirror: %llu %s are above "
		    "zfs_vdev_async_mirror_load_max_bytes",
		    (u_longlong_t)count, what);
		return (SET_ERROR(EOVERFLOW));
	}
	*sizep = count * each;
	return (0);
}

static int
async_mirror_load_obj_compare(const void *a, const void *b)
{
	const uint64_t *oa = a;
	const uint64_t *ob = b;

	return (TREE_CMP(*oa, *ob));
}

/* Read pool records in txg order; the caller owns *recsp on success. */
static int
async_mirror_load_zap(spa_t *spa, uint64_t zap,
    async_mirror_load_rec_t **recsp, uint64_t *np)
{
	objset_t *mos = spa_meta_objset(spa);
	uint64_t count = 0;
	int error = async_mirror_zap_entries(mos, zap, &count);

	*recsp = NULL;
	*np = 0;
	if (error != 0 || count == 0)
		return (error);

	/* Valid records have unique txgs at or below the imported txg. */
	if (count > spa->spa_uberblock.ub_txg)
		return (SET_ERROR(EINVAL));

	size_t size;

	error = async_mirror_load_size(spa, "records", count,
	    sizeof (async_mirror_load_rec_t), &size);
	if (error != 0)
		return (error);

	async_mirror_load_arg_t ala = {
		.ala_recs = vmem_zalloc(size, KM_SLEEP),
		.ala_count = count,
	};

	error = async_mirror_rec_iterate(mos, zap, async_mirror_load_rec_cb,
	    &ala);
	if (error == 0 && ala.ala_n != count)
		error = SET_ERROR(EINVAL);
	if (error == 0) {
		qsort(ala.ala_recs, count, sizeof (async_mirror_load_rec_t),
		    async_mirror_load_rec_compare);
		if (count > SIZE_MAX / sizeof (uint64_t)) {
			error = SET_ERROR(EOVERFLOW);
		} else {
			uint64_t *objs = vmem_alloc(count * sizeof (uint64_t),
			    KM_SLEEP);
			int rec_error = 0;

			for (uint64_t i = 0; i < count; i++) {
				if (i > 0 &&
				    ala.ala_recs[i - 1].alr_txg >=
				    ala.ala_recs[i].alr_txg)
					rec_error = SET_ERROR(EINVAL);
				objs[i] = ala.ala_recs[i].alr_obj;
			}
			qsort(objs, count, sizeof (uint64_t),
			    async_mirror_load_obj_compare);
			for (uint64_t i = 1; rec_error == 0 && i < count; i++) {
				if (objs[i - 1] == objs[i])
					rec_error = SET_ERROR(EINVAL);
			}
			vmem_free(objs, count * sizeof (uint64_t));
			error = rec_error;
		}
	}
	if (error != 0) {
		vmem_free(ala.ala_recs, size);
		return (error);
	}

	*recsp = ala.ala_recs;
	*np = count;
	return (0);
}

/* One deferred block read from the MOS at import. */
typedef struct async_mirror_load_defer {
	uint64_t	ald_txg;
	blkptr_t	ald_bp;
	zbookmark_phys_t ald_zb;
} async_mirror_load_defer_t;

static int
async_mirror_load_defer_compare(const void *a, const void *b)
{
	const async_mirror_load_defer_t *da = a;
	const async_mirror_load_defer_t *db = b;

	return (TREE_CMP(da->ald_txg, db->ald_txg));
}

/* Validate one deferred block before the release thread can read it. */
static int
async_mirror_load_defer_check(spa_t *spa, const async_mirror_load_defer_t *d)
{
	const blkptr_t *bp = &d->ald_bp;
	int error = 0;

	if (d->ald_txg == 0 || d->ald_txg > spa->spa_uberblock.ub_txg ||
	    BP_IS_HOLE(bp) || BP_IS_EMBEDDED(bp) || BP_GET_PSIZE(bp) == 0 ||
	    zfs_blkptr_verify(spa, bp, BLK_CONFIG_NEEDED, BLK_VERIFY_LOG) != 0)
		return (SET_ERROR(ECKSUM));

	/* Hold SCL_VDEV while each DVA is checked against the vdev tree. */
	spa_config_enter(spa, SCL_VDEV, FTAG, RW_READER);
	for (int i = 0; i < BP_GET_NDVAS(bp); i++) {
		const dva_t *dva = &bp->blk_dva[i];
		vdev_t *vd = vdev_lookup_top(spa, DVA_GET_VDEV(dva));

		if (vd == NULL || !vdev_is_concrete(vd) || DVA_GET_GANG(dva))
			continue;
		/* The release allocates each DVA's size for its read buffer. */
		if (DVA_GET_ASIZE(dva) >
		    vdev_psize_to_asize(vd, SPA_MAXBLOCKSIZE))
			error = SET_ERROR(ECKSUM);
	}
	spa_config_exit(spa, SCL_VDEV, FTAG);
	return (error);
}

/* Read deferred blocks in txg order; the caller owns *defsp on success. */
static int
async_mirror_load_defer(spa_t *spa, uint64_t zap,
    async_mirror_load_defer_t **defsp, uint64_t *np)
{
	objset_t *mos = spa_meta_objset(spa);
	uint64_t count = 0;
	int error = async_mirror_zap_entries(mos, zap, &count);

	*defsp = NULL;
	*np = 0;
	if (error != 0 || count == 0)
		return (error);

	size_t size;

	error = async_mirror_load_size(spa, "deferred blocks", count,
	    sizeof (async_mirror_load_defer_t), &size);
	if (error != 0)
		return (error);

	async_mirror_load_defer_t *defs = vmem_zalloc(size, KM_SLEEP);
	uint64_t val[ASYNC_MIRROR_DEFER_WORDS];
	uint64_t n = 0;
	zap_cursor_t zc;
	zap_attribute_t *za = zap_attribute_alloc();

	for (zap_cursor_init(&zc, mos, zap);
	    (error = zap_cursor_retrieve(&zc, za)) == 0;
	    zap_cursor_advance(&zc)) {
		if (n == count || za->za_integer_length != sizeof (uint64_t) ||
		    za->za_num_integers != ASYNC_MIRROR_DEFER_WORDS) {
			error = SET_ERROR(EINVAL);
			break;
		}
		error = zap_lookup(mos, zap, za->za_name, sizeof (uint64_t),
		    ASYNC_MIRROR_DEFER_WORDS, val);
		if (error != 0)
			break;

		async_mirror_load_defer_t *d = &defs[n++];

		d->ald_txg = val[0];
		memcpy(&d->ald_bp, &val[1], sizeof (blkptr_t));
		memcpy(&d->ald_zb,
		    &val[1 + sizeof (blkptr_t) / sizeof (uint64_t)],
		    sizeof (zbookmark_phys_t));
		error = async_mirror_load_defer_check(spa, d);
		if (error != 0)
			break;
	}
	zap_cursor_fini(&zc);
	zap_attribute_free(za);

	if (error == ENOENT)
		error = (n == count) ? 0 : SET_ERROR(EINVAL);
	if (error != 0) {
		vmem_free(defs, size);
		return (error);
	}
	/* Attach by txg. */
	qsort(defs, count, sizeof (async_mirror_load_defer_t),
	    async_mirror_load_defer_compare);
	*defsp = defs;
	*np = count;
	return (0);
}

/* Set the DTL range for resilver. */
void
async_mirror_mark_dtl(spa_t *spa, vdev_t *lag, uint64_t from, uint64_t size)
{
	vdev_t *rvd = spa->spa_root_vdev;

	ASSERT(lag->vdev_ops->vdev_op_leaf);
	ASSERT3U(size, >, 0);

	for (vdev_t *vd = lag; vd != rvd; vd = vd->vdev_parent)
		vdev_dtl_dirty(vd, DTL_PARTIAL, from, size);
	vdev_dtl_dirty(lag, DTL_MISSING, from, size);
	vdev_dirty(lag->vdev_top, VDD_DTL, lag, spa_first_txg(spa));
	spa->spa_async_mirror_stats.sams_resync_from = from;
	spa_async_mirror_stats_update(spa);
	zfs_dbgmsg("async mirror: lagging child guid %llu marked for "
	    "resilver of txgs %llu to %llu", (u_longlong_t)lag->vdev_guid,
	    (u_longlong_t)from, (u_longlong_t)(from + size - 1));
}

/* Track children taken out of the async mirror during this import. */
void
async_mirror_note_resync(spa_t *spa, vdev_t *vd)
{
	uint64_t *resync = spa->spa_async_mirror_import.ami_resync;

	for (int i = 0; i < 2; i++) {
		if (resync[i] == vd->vdev_guid)
			return;
		if (resync[i] == 0) {
			resync[i] = vd->vdev_guid;
			return;
		}
	}
}

/* Allocate and initialize state before starting threads. */
vdev_async_mirror_t *
async_mirror_create(spa_t *spa)
{
	vdev_async_mirror_t *am = kmem_zalloc(sizeof (*am), KM_SLEEP);

	am->am_spa = spa;
	am->am_last_ub = spa->spa_uberblock;
	am->am_synced_txg = spa->spa_uberblock.ub_txg;
	am->am_avail = UINT64_MAX;
	mutex_init(&am->am_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&am->am_cv, NULL, CV_DEFAULT, NULL);
	cv_init(&am->am_drain_cv, NULL, CV_DEFAULT, NULL);
	list_create(&am->am_txgs, sizeof (async_mirror_txg_t),
	    offsetof(async_mirror_txg_t, at_node));
	return (am);
}

/* Free every in-memory record. */
static void
async_mirror_free_index(vdev_async_mirror_t *am)
{
	async_mirror_txg_t *at;

	while ((at = list_remove_head(&am->am_txgs)) != NULL)
		async_mirror_txg_free(at);
}

/* Free state after its threads exit. */
static void
async_mirror_destroy(vdev_async_mirror_t *am)
{
	ASSERT(list_is_empty(&am->am_txgs));
	ASSERT3P(am->am_thread, ==, NULL);
	ASSERT3P(am->am_release_zthr, ==, NULL);
	ASSERT3P(am->am_giveup_zthr, ==, NULL);

	list_destroy(&am->am_txgs);
	cv_destroy(&am->am_cv);
	cv_destroy(&am->am_drain_cv);
	mutex_destroy(&am->am_lock);
	kmem_free(am, sizeof (*am));
}

/* True if vd is a leaf that the pool can read and write. */
boolean_t
async_mirror_child_usable(vdev_t *vd)
{
	return (vd != NULL && vd->vdev_ops->vdev_op_leaf &&
	    vdev_readable(vd) && vdev_writeable(vd));
}

/* Read eligible and newest uberblocks, then restore the load cursor. */
void
async_mirror_read_leaf_ub(spa_t *spa, vdev_t *leaf, uberblock_t *ub,
    uberblock_t *newest, boolean_t *io_error)
{
	nvlist_t *cfg = NULL;
	uint64_t saved = spa->spa_load_latest_ub_txg;

	if (newest != NULL) {
		vdev_uberblock_load_newest(leaf, ub, newest, &cfg, io_error);
	} else {
		vdev_uberblock_load(leaf, ub, &cfg);
		if (io_error != NULL)
			*io_error = B_FALSE;
	}
	spa->spa_load_latest_ub_txg = saved;
	if (cfg != NULL)
		nvlist_free(cfg);
}

/* Read marked child uberblocks before metadata through the vdev tree. */
void
vdev_async_mirror_load_marked(spa_t *spa)
{
	vdev_t *rvd = spa->spa_root_vdev;

	for (uint64_t c = 0; c < rvd->vdev_children; c++) {
		vdev_t *tvd = rvd->vdev_child[c];

		if (tvd->vdev_ops != &vdev_mirror_ops)
			continue;
		for (uint64_t i = 0; i < tvd->vdev_children; i++) {
			vdev_t *leaf = tvd->vdev_child[i];
			uberblock_t ub, newest;
			boolean_t other = B_FALSE;

			/* The copy that a keep discards holds no txg. */
			if (leaf->vdev_guid == spa->spa_async_mirror_skip) {
				leaf->vdev_async_lagging =
				    ASYNC_MIRROR_CHILD_SET;
				leaf->vdev_async_txg = 0;
				continue;
			}
			if (!leaf->vdev_async_lagging)
				continue;
			leaf->vdev_async_txg = 0;
			if (!vdev_readable(leaf))
				continue;
			for (uint64_t j = 0; j < tvd->vdev_children; j++) {
				vdev_t *cvd = tvd->vdev_child[j];

				if (cvd != leaf && vdev_readable(cvd))
					other = B_TRUE;
			}
			async_mirror_read_leaf_ub(spa, leaf, &ub, &newest,
			    NULL);
			if (newest.ub_txg > ub.ub_txg && other) {
				zfs_dbgmsg("async mirror: marked leaf guid "
				    "%llu holds txg %llu, above the rewind "
				    "txg; it is stale",
				    (u_longlong_t)leaf->vdev_guid,
				    (u_longlong_t)newest.ub_txg);
				continue;
			}
			leaf->vdev_async_txg = ub.ub_txg;
			zfs_dbgmsg("async mirror: marked leaf guid %llu holds "
			    "txg %llu", (u_longlong_t)leaf->vdev_guid,
			    (u_longlong_t)ub.ub_txg);
		}
	}
}

/* True if txg is newer than the marked child's applied txg. */
boolean_t
vdev_async_mirror_stale(vdev_t *vd, uint64_t txg)
{
	return (vd->vdev_async_lagging &&
	    txg > atomic_load_64(&vd->vdev_async_txg));
}

/* Caller holds am_lock; publish no newer than the oldest deferred block. */
void
async_mirror_publish_txg(vdev_async_mirror_t *am)
{
	vdev_t *lag = am->am_lagging;
	uint64_t txg = am->am_applied_txg;

	ASSERT(MUTEX_HELD(&am->am_lock));

	if (!async_mirror_copying(am->am_state) || !lag->vdev_async_lagging)
		return;
	if (am->am_defer_txg != 0)
		txg = MIN(txg, am->am_defer_txg - 1);
	atomic_store_64(&lag->vdev_async_txg, txg);
}

/* True if vd is a frozen lagging child, which gets no I/O. */
static boolean_t
async_mirror_frozen(vdev_t *vd)
{
	return (atomic_load_32(&vd->vdev_async_state) == AMS_FROZEN);
}

/* Fence newer data and intent logs; use older data as a last resort. */
boolean_t
vdev_async_mirror_read_fenced(vdev_t *vd, const zio_t *zio)
{
	if (async_mirror_frozen(vd))
		return (B_TRUE);
	if (!vd->vdev_async_lagging)
		return (vdev_async_mirror_is_async(vd));
	if (zio->io_bp == NULL)
		return (B_TRUE);
	if (BP_GET_TYPE(zio->io_bp) == DMU_OT_INTENT_LOG) {
		vdev_t *pvd = vd->vdev_parent;

		for (uint64_t c = 0; pvd != NULL &&
		    c < pvd->vdev_children; c++) {
			vdev_t *cvd = pvd->vdev_child[c];

			if (cvd != vd && vdev_readable(cvd))
				return (B_TRUE);
		}
		return (B_FALSE);
	}
	return (vdev_async_mirror_stale(vd, zio->io_txg));
}

/* Count repair writes on a marked leaf until its DTL fence opens. */
boolean_t
vdev_async_mirror_repair_start(vdev_t *vd, const zio_t *zio, void **tokenp)
{
	vdev_async_mirror_t *am = vd->vdev_spa->spa_async_mirror;
	boolean_t marked = (vd->vdev_async_lagging != 0);

	*tokenp = NULL;
	if (async_mirror_frozen(vd))
		return (B_FALSE);
	if (!marked && vdev_async_mirror_is_async(vd))
		return (B_FALSE);
	if (marked && (zio->io_bp == NULL ||
	    BP_GET_TYPE(zio->io_bp) == DMU_OT_INTENT_LOG ||
	    vdev_async_mirror_stale(vd, zio->io_txg)))
		return (B_FALSE);

	am = vd->vdev_spa->spa_async_mirror;
	if (am == NULL)
		return (B_TRUE);

	mutex_enter(&am->am_lock);
	if (am->am_dtl_child_clearing &&
	    am->am_dtl_child_guid == vd->vdev_guid) {
		mutex_exit(&am->am_lock);
		return (B_FALSE);
	}
	if (am->am_lagging != vd && am->am_entry_guid != vd->vdev_guid &&
	    am->am_dtl_child_guid != vd->vdev_guid) {
		mutex_exit(&am->am_lock);
		return (!vdev_async_mirror_is_async(vd));
	}
	am->am_repair_count++;
	am->am_spa->spa_async_mirror_stats.sams_repair_inflight =
	    am->am_repair_count;
	spa_async_mirror_stats_update(am->am_spa);
	mutex_exit(&am->am_lock);
	*tokenp = am;
	return (B_TRUE);
}

void
vdev_async_mirror_repair_done(void *token)
{
	vdev_async_mirror_t *am = token;

	if (am == NULL)
		return;
	/* The pool's I/O drain keeps am alive until every repair ends. */
	mutex_enter(&am->am_lock);
	ASSERT3U(am->am_repair_count, >, 0);
	am->am_repair_count--;
	am->am_spa->spa_async_mirror_stats.sams_repair_inflight =
	    am->am_repair_count;
	spa_async_mirror_stats_update(am->am_spa);
	mutex_exit(&am->am_lock);
	if (am->am_release_zthr != NULL)
		zthr_wakeup(am->am_release_zthr);
}

/* True if outstanding DTL or resilver work prevents use of this child. */
boolean_t
async_mirror_child_needs_resilver(spa_t *spa, vdev_t *vd)
{
	return (!vdev_dtl_empty(vd, DTL_MISSING) ||
	    !vdev_dtl_empty(vd, DTL_PARTIAL) ||
	    dsl_scan_resilvering(spa_get_dsl(spa)) ||
	    dsl_scan_resilver_scheduled(spa_get_dsl(spa)));
}

/* True if every top-level vdev is supported by async mirror holds. */
static boolean_t
async_mirror_layout_ok(spa_t *spa, vdev_t *lag)
{
	vdev_t *rvd = spa->spa_root_vdev;

	for (uint64_t c = 0; c < rvd->vdev_children; c++) {
		vdev_t *tvd = rvd->vdev_child[c];

		if (tvd != lag->vdev_top && vdev_is_concrete(tvd) &&
		    !tvd->vdev_islog)
			return (B_FALSE);
	}
	return (B_TRUE);
}

/* True if both uberblocks point to the same root block. */
static boolean_t
async_mirror_same_root(const uberblock_t *a, const uberblock_t *b)
{
	return (memcmp(&a->ub_rootbp, &b->ub_rootbp, sizeof (blkptr_t)) == 0);
}

/* Build the index; the caller frees it on error. */
static int
async_mirror_load_index(spa_t *spa, vdev_async_mirror_t *am,
    const async_mirror_load_rec_t *recs, uint64_t n)
{
	objset_t *mos = spa_meta_objset(spa);
	async_mirror_scan_arg_t asa = { .asa_spa = spa };
	int error = 0;

	ASSERT(list_is_empty(&am->am_txgs));
	ASSERT0(am->am_held_bytes);

	if (n == 0)
		return (0);

	spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);
	asa.asa_nfree = spa->spa_root_vdev->vdev_children;
	asa.asa_free = kmem_zalloc(asa.asa_nfree * sizeof (zfs_range_tree_t *),
	    KM_SLEEP);
	for (uint64_t i = 0; i < n; i++) {
		const async_mirror_load_rec_t *r = &recs[i];
		dmu_object_info_t doi;
		space_map_t *sm = NULL;

		ASSERT(i == 0 || recs[i - 1].alr_txg < r->alr_txg);
		if (r->alr_txg == 0 || r->alr_txg > spa->spa_uberblock.ub_txg ||
		    r->alr_obj == 0) {
			error = SET_ERROR(ECKSUM);
			break;
		}
		/* The release frees this object: it must be a space map. */
		error = dmu_object_info(mos, r->alr_obj, &doi);
		if (error == 0 && (doi.doi_type != DMU_OT_SPACE_MAP ||
		    doi.doi_bonus_type != DMU_OT_SPACE_MAP_HEADER ||
		    doi.doi_bonus_size < SPACE_MAP_SIZE_V0))
			error = SET_ERROR(ECKSUM);
		if (error != 0)
			break;
		error = space_map_open(&sm, mos, r->alr_obj, 0, UINT64_MAX,
		    SPA_MINBLOCKSHIFT);
		if (error != 0)
			break;

		async_mirror_txg_t *at = async_mirror_txg_alloc(r->alr_txg,
		    r->alr_obj);

		asa.asa_alloc = 0;
		asa.asa_held = 0;
		asa.asa_held_dspace = 0;
		asa.asa_held_log = 0;
		uint64_t sm_len = space_map_length(sm);

		error = space_map_iterate(sm, sm_len,
		    async_mirror_scan_cb, &asa);
		space_map_close(sm);
		if (error != 0) {
			async_mirror_txg_free(at);
			break;
		}

		at->at_sm_len = sm_len;
		at->at_bytes = asa.asa_alloc;
		at->at_held = asa.asa_held;
		at->at_held_dspace = asa.asa_held_dspace;
		am->am_held_bytes += asa.asa_held;
		am->am_held_dspace += asa.asa_held_dspace;
		am->am_held_log_bytes += asa.asa_held_log;
		at->at_ub = (i + 1 < n) ? recs[i + 1].alr_prev_ub :
		    spa->spa_uberblock;
		at->at_ready = B_TRUE;
		at->at_ready_time = gethrtime();
		list_insert_tail(&am->am_txgs, at);
	}
	for (uint64_t c = 0; c < asa.asa_nfree; c++) {
		if (asa.asa_free[c] == NULL)
			continue;
		zfs_range_tree_vacate(asa.asa_free[c], NULL, NULL);
		zfs_range_tree_destroy(asa.asa_free[c]);
	}
	kmem_free(asa.asa_free, asa.asa_nfree * sizeof (zfs_range_tree_t *));
	spa_config_exit(spa, SCL_CONFIG, FTAG);
	return (error);
}

/* Erase uberblocks above a rewind txg before writes start. */
static void
async_mirror_erase_ahead(spa_t *spa, vdev_t *lag, uint64_t newest)
{
	uint64_t import_txg = spa->spa_uberblock.ub_txg;

	ASSERT(!spa->spa_sync_on);
	ASSERT3U(newest, >, import_txg);

	spa_load_note(spa, "async mirror: lagging child guid %llu "
	    "holds txg %llu, above the rewind txg %llu; it is erased "
	    "and resilvered", (u_longlong_t)lag->vdev_guid,
	    (u_longlong_t)newest, (u_longlong_t)import_txg);
	spa_history_log_internal(spa, "async mirror", NULL,
	    "rewind to txg %llu discards txgs up to %llu on the "
	    "lagging child guid %llu; the child is resilvered",
	    (u_longlong_t)import_txg, (u_longlong_t)newest,
	    (u_longlong_t)lag->vdev_guid);

	int error = vdev_uberblock_erase(lag, import_txg);

	if (error != 0) {
		/*
		 * Keep the child out of the pool.  Do not fail the import:
		 * the operator is recovering the pool.
		 */
		spa_load_note(spa, "async mirror: cannot erase the "
		    "uberblocks of the lagging child guid %llu, error "
		    "%d; the child is faulted",
		    (u_longlong_t)lag->vdev_guid, error);
		spa_config_enter(spa, SCL_STATE_ALL, FTAG, RW_WRITER);
		vdev_set_state(lag, B_FALSE, VDEV_STATE_FAULTED,
		    VDEV_AUX_IO_FAILURE);
		spa_config_exit(spa, SCL_STATE_ALL, FTAG);
	}
}

/* Set the state that the import found, before the threads start. */
static void
async_mirror_load_state(vdev_async_mirror_t *am, vdev_t *lag, vdev_t *lead,
    async_mirror_state_t state)
{
	ASSERT(!am->am_spa->spa_sync_on);
	ASSERT3B(lag != NULL, ==, async_mirror_has_child(state));

	mutex_enter(&am->am_lock);
	async_mirror_set_state(am, AMS_OFF);
	am->am_lagging = lag;
	am->am_leader = lead;
	async_mirror_set_state(am, state);
	mutex_exit(&am->am_lock);
}

/* Check for a top-level two-way mirror and return its other child. */
static boolean_t
async_mirror_topology_leader(vdev_t *lag, vdev_t **leadp)
{
	*leadp = NULL;
	if (lag == NULL || !lag->vdev_ops->vdev_op_leaf)
		return (B_FALSE);

	vdev_t *pvd = lag->vdev_parent;

	if (pvd == NULL || pvd->vdev_ops != &vdev_mirror_ops ||
	    pvd->vdev_children != 2 || pvd != lag->vdev_top)
		return (B_FALSE);
	*leadp = pvd->vdev_child[pvd->vdev_child[0] == lag ? 1 : 0];
	return (B_TRUE);
}

/* Select copy or drain from records, import txg, and child txg. */
static void
async_mirror_resume_or_drain(spa_t *spa, vdev_async_mirror_t *am,
    const async_mirror_load_rec_t *recs, uint64_t n, uint64_t leaf_guid,
    boolean_t frozen, uint64_t defer_txg, uint64_t want, uint64_t off_txg)
{
	vdev_t *rvd = spa->spa_root_vdev;
	uint64_t import_txg = spa->spa_uberblock.ub_txg;

	ASSERT3U(n, >, 0);
	ASSERT3P(am->am_lagging, ==, NULL);

	if (off_txg != 0) {
		async_mirror_load_state(am, NULL, NULL, AMS_DRAINING);
		am->am_applied_txg = UINT64_MAX;
		zfs_dbgmsg("async mirror: leaf guid %llu left the async mirror "
		    "at txg %llu; releasing %llu records",
		    (u_longlong_t)leaf_guid, (u_longlong_t)off_txg,
		    (u_longlong_t)n);
		return;
	}

	vdev_t *lag = vdev_lookup_by_guid(rvd, leaf_guid);
	vdev_t *lead = NULL;
	boolean_t usable = async_mirror_child_usable(lag) &&
	    async_mirror_topology_leader(lag, &lead);
	boolean_t have_child = B_FALSE;
	uberblock_t child_ub, child_newest;

	if (usable) {
		async_mirror_read_leaf_ub(spa, lag, &child_ub, &child_newest,
		    NULL);
		have_child = B_TRUE;
	}

	uint64_t child_txg = have_child ? child_ub.ub_txg : 0;
	boolean_t lead_ok = async_mirror_child_usable(lead);
	boolean_t ahead = have_child && lead_ok &&
	    child_newest.ub_txg > import_txg;
	boolean_t linked = B_FALSE;

	/*
	 * Require a record for the imported root txg. A gap means frees were
	 * unheld, so a block that the child uses may have been reused.
	 */
	uint64_t root_txg = BP_GET_LOGICAL_BIRTH(&spa->spa_uberblock.ub_rootbp);
	boolean_t complete = (recs[n - 1].alr_txg == root_txg);

	if (!complete) {
		zfs_dbgmsg("async mirror: the newest record is of txg %llu, "
		    "but the pool's root block is of txg %llu",
		    (u_longlong_t)recs[n - 1].alr_txg, (u_longlong_t)root_txg);
	}

	if (have_child && !ahead && child_txg <= import_txg && complete) {
		const async_mirror_load_rec_t *q = NULL;

		for (uint64_t i = 0; i < n; i++) {
			if (recs[i].alr_txg > child_txg) {
				q = &recs[i];
				break;
			}
		}
		/* Equal roots prove equal state even when txgs differ. */
		const uberblock_t *ub = q != NULL ? &q->alr_prev_ub :
		    &spa->spa_uberblock;

		linked = child_ub.ub_txg <= ub->ub_txg &&
		    async_mirror_same_root(&child_ub, ub);
	}

	boolean_t resume = !frozen && linked && lead_ok &&
	    want == leaf_guid &&
	    !spa_feature_is_active(spa, SPA_FEATURE_POOL_CHECKPOINT) &&
	    !spa_multihost(spa) &&
	    async_mirror_layout_ok(spa, lag) &&
	    !async_mirror_child_needs_resilver(spa, lag) &&
	    !async_mirror_child_needs_resilver(spa, lead);

	if (resume) {
		async_mirror_load_state(am, lag, lead, AMS_ACTIVE);
		am->am_applied_txg = child_txg;
		zfs_dbgmsg("async mirror: resume, leaf guid %llu at txg %llu, "
		    "pool at txg %llu", (u_longlong_t)leaf_guid,
		    (u_longlong_t)child_txg, (u_longlong_t)import_txg);
		return;
	}

	/* Release the records.  The child keeps normal mirror writes. */
	async_mirror_load_state(am, NULL, NULL, AMS_DRAINING);
	am->am_applied_txg = UINT64_MAX;

	if (ahead)
		async_mirror_erase_ahead(spa, lag, child_newest.ub_txg);

	/* async_mirror_frozen_load() decides what the child needs. */
	if (frozen)
		return;

	if (lag == NULL || !lag->vdev_ops->vdev_op_leaf) {
		zfs_dbgmsg("async mirror: leaf guid %llu is gone; dropping "
		    "%llu records", (u_longlong_t)leaf_guid, (u_longlong_t)n);
	} else if (have_child && !lead_ok) {
		/* The child is the only copy, so it is the pool. */
		zfs_dbgmsg("async mirror: leaf guid %llu is the only copy; "
		    "dropping %llu records", (u_longlong_t)leaf_guid,
		    (u_longlong_t)n);
	} else if (have_child && linked) {
		/* A valid older child can recover by resilver. */
		uint64_t from = MIN(child_txg + 1, defer_txg);

		if (from <= import_txg)
			async_mirror_mark_dtl(spa, lag, from,
			    import_txg - from + 1);
		async_mirror_note_resync(spa, lag);
	} else {
		/* Gap, lagging child ahead, not linked, or unreadable. */
		async_mirror_mark_dtl(spa, lag, TXG_INITIAL,
		    import_txg - TXG_INITIAL + 1);
		async_mirror_note_resync(spa, lag);
	}
}

/* Load freeze state after records and any ahead-child erase. */
static void
async_mirror_frozen_load(spa_t *spa, vdev_async_mirror_t *am,
    const async_mirror_child_phys_t *amc, uint64_t leaf_guid, uint64_t n,
    uint64_t want)
{
	vdev_t *rvd = spa->spa_root_vdev;
	uint64_t frozen_txg = amc->amc_frozen_txg;
	uint64_t from = amc->amc_frozen_from;
	uint64_t import_txg = spa->spa_uberblock.ub_txg;
	vdev_t *lag = vdev_lookup_by_guid(rvd, leaf_guid);
	vdev_t *lead = NULL;
	uberblock_t ub = { 0 };
	boolean_t at_frozen = B_FALSE;
	boolean_t usable = async_mirror_child_usable(lag) &&
	    async_mirror_topology_leader(lag, &lead);

	ASSERT3P(am->am_lagging, ==, NULL);
	if (from < TXG_INITIAL || from > frozen_txg + 1)
		from = frozen_txg + 1;
	if (usable) {
		uberblock_t newest;

		async_mirror_read_leaf_ub(spa, lag, &ub, &newest, NULL);
		if (n == 0 && newest.ub_txg > import_txg &&
		    async_mirror_child_usable(lead)) {
			async_mirror_erase_ahead(spa, lag, newest.ub_txg);
		} else {
			at_frozen = (ub.ub_txg == frozen_txg &&
			    newest.ub_txg == frozen_txg);
		}
	}
	boolean_t lead_ok = async_mirror_child_usable(lead);

	if (usable && lead_ok && at_frozen && frozen_txg <= import_txg &&
	    want == leaf_guid && !spa_multihost(spa) &&
	    !async_mirror_child_needs_resilver(spa, lag) &&
	    !async_mirror_child_needs_resilver(spa, lead)) {
		async_mirror_load_state(am, lag, lead, AMS_FROZEN);
		am->am_applied_txg = frozen_txg;
		am->am_halt_txg = import_txg;
		am->am_entry_txg = import_txg;
		am->am_giveup = (amc->amc_frozen_reason ==
		    ASYNC_MIRROR_FROZEN_HELD);
		am->am_giveup_why = am->am_giveup ? AMG_HELD : AMG_NONE;
		am->am_frozen_from = from;
		spa_load_note(spa, "async mirror: the lagging child guid %llu "
		    "stays frozen at txg %llu", (u_longlong_t)leaf_guid,
		    (u_longlong_t)frozen_txg);
		return;
	}

	async_mirror_load_state(am, NULL, NULL, AMS_DRAINING);
	spa_load_note(spa, "async mirror: the freeze of the lagging child "
	    "guid %llu at txg %llu ends", (u_longlong_t)leaf_guid,
	    (u_longlong_t)frozen_txg);
	if (lag == NULL || !lag->vdev_ops->vdev_op_leaf) {
		zfs_dbgmsg("async mirror: frozen leaf guid %llu is gone",
		    (u_longlong_t)leaf_guid);
	} else if (usable && !lead_ok) {
		zfs_dbgmsg("async mirror: frozen leaf guid %llu is the only "
		    "copy", (u_longlong_t)leaf_guid);
	} else if (usable && at_frozen && frozen_txg <= import_txg) {
		if (from <= import_txg)
			async_mirror_mark_dtl(spa, lag, from,
			    import_txg - from + 1);
		async_mirror_note_resync(spa, lag);
	} else {
		async_mirror_mark_dtl(spa, lag, TXG_INITIAL,
		    import_txg - TXG_INITIAL + 1);
		async_mirror_note_resync(spa, lag);
	}
}

/* Call async_mirror_mark() at import, before the sync thread starts. */
static void
async_mirror_mark_at_load(spa_t *spa, vdev_t *lag, uint64_t txg)
{
	ASSERT(!spa->spa_sync_on);

	spa_config_enter(spa, SCL_CONFIG, FTAG, RW_WRITER);
	async_mirror_mark(spa, lag, txg);
	spa_config_exit(spa, SCL_CONFIG, FTAG);
}

/* Validate a lagging leaf and return its leader in *leadp. */
int
async_mirror_can_start(spa_t *spa, vdev_t *lag, vdev_t **leadp)
{
	ASSERT(lag->vdev_ops->vdev_op_leaf);

	if (!spa_feature_is_enabled(spa, SPA_FEATURE_ASYNC_MIRROR))
		return (SET_ERROR(ENOTSUP));

	vdev_t *lead = NULL;

	if (!async_mirror_topology_leader(lag, &lead) ||
	    !async_mirror_layout_ok(spa, lag))
		return (SET_ERROR(ENOTSUP));
	if (spa_multihost(spa))
		return (SET_ERROR(ZFS_ERR_ASYNC_MIRROR_MULTIHOST));
	if (spa_feature_is_active(spa, SPA_FEATURE_POOL_CHECKPOINT))
		return (SET_ERROR(ZFS_ERR_CHECKPOINT_EXISTS));

	if (!async_mirror_child_usable(lag) || !async_mirror_child_usable(lead))
		return (SET_ERROR(ENXIO));
	if (async_mirror_child_needs_resilver(spa, lag) ||
	    async_mirror_child_needs_resilver(spa, lead))
		return (SET_ERROR(EBUSY));
	*leadp = lead;
	return (0);
}

/* What the MOS holds of the async mirror, read at import. */
typedef struct async_mirror_entries {
	uint64_t	ame_zap;	/* the record ZAP, or 0 */
	async_mirror_load_rec_t *ame_recs; /* its records, by txg */
	uint64_t	ame_nrecs;
	uint64_t	ame_dzap;	/* the deferred-block ZAP, or 0 */
	async_mirror_load_defer_t *ame_defs;
	uint64_t	ame_ndefs;
	uint64_t	ame_defer_txg;	/* oldest txg of a deferred block */
	uint64_t	ame_child_guid;	/* leaf of the per-child entry, or 0 */
	async_mirror_child_phys_t ame_child;	/* that entry */
	boolean_t	ame_frozen;	/* it has the freeze */
	boolean_t	ame_off;	/* it has the switch off */
	uint64_t	ame_off_txg;
	uint64_t	ame_replace_guid;	/* replacement reservation */
} async_mirror_entries_t;

/* Load one per-child entry; reject unsupported duplicate entries. */
static int
async_mirror_entries_child_cb(uint64_t guid,
    const async_mirror_child_phys_t *amc, void *arg)
{
	async_mirror_entries_t *ame = arg;

	if ((amc->amc_flags & ASYNC_MIRROR_CHILD_STATE) != 0) {
		if (ame->ame_child_guid != 0)
			return (SET_ERROR(ENOTSUP));
		ame->ame_child_guid = guid;
		ame->ame_child = *amc;
	}
	if ((amc->amc_flags & ASYNC_MIRROR_CHILD_REPLACE) != 0) {
		if (ame->ame_replace_guid != 0)
			return (SET_ERROR(ENOTSUP));
		ame->ame_replace_guid = guid;
	}
	return (0);
}

/* Read an entry of the MOS directory.  A missing entry is no error. */
static int
async_mirror_dir_lookup(objset_t *mos, const char *name, uint64_t words,
    uint64_t *val, boolean_t *found)
{
	int error = zap_lookup(mos, DMU_POOL_DIRECTORY_OBJECT, name,
	    sizeof (uint64_t), words, val);

	*found = (error == 0);
	return (error == ENOENT ? 0 : error);
}

/* Read the entries of the async mirror.  The caller frees them in all cases. */
static int
async_mirror_entries_load(spa_t *spa, async_mirror_entries_t *ame)
{
	objset_t *mos = spa_meta_objset(spa);
	boolean_t zap, dzap;
	int error;

	memset(ame, 0, sizeof (*ame));
	ame->ame_defer_txg = UINT64_MAX;

	error = async_mirror_dir_lookup(mos, DMU_POOL_ASYNC_MIRROR_ZAP, 1,
	    &ame->ame_zap, &zap);
	if (error == 0 && zap) {
		error = async_mirror_load_zap(spa, ame->ame_zap,
		    &ame->ame_recs, &ame->ame_nrecs);
	}
	if (error == 0) {
		uint64_t flags;
		uint64_t dtl_txg;

		error = async_mirror_child_iterate(mos,
		    async_mirror_entries_child_cb, ame);
		flags = ame->ame_child.amc_flags;
		dtl_txg = ame->ame_child.amc_reserved[
		    ASYNC_MIRROR_CHILD_DTL_TXG_WORD];
		ame->ame_frozen = (flags & ASYNC_MIRROR_CHILD_FROZEN) != 0;
		ame->ame_off = (flags & ASYNC_MIRROR_CHILD_OFF) != 0;
		ame->ame_off_txg = ame->ame_child.amc_off_txg;
		if ((flags & ASYNC_MIRROR_CHILD_DTL_PENDING) != 0 &&
		    dtl_txg > spa->spa_uberblock.ub_txg) {
			spa_load_note(spa, "DTL marker is newer than imported "
			    "uberblock");
			error = SET_ERROR(EINVAL);
		}
		if (error == ENOTSUP) {
			spa_load_note(spa, "async mirror: a per-child entry "
			    "is of a layout that this software does not "
			    "know");
		}
		/* Commit the lagging flag with its record ZAP. */
		if (error == 0 && (ame->ame_zap != 0) !=
		    ((flags & ASYNC_MIRROR_CHILD_LAGGING) != 0)) {
			spa_load_note(spa, "async mirror: the record ZAP and "
			    "the per-child entry do not agree");
			error = SET_ERROR(EINVAL);
		}
	}
	if (error == 0) {
		error = async_mirror_dir_lookup(mos,
		    DMU_POOL_ASYNC_MIRROR_DEFERRED, 1, &ame->ame_dzap, &dzap);
	}
	if (error == 0 && dzap) {
		error = async_mirror_load_defer(spa, ame->ame_dzap,
		    &ame->ame_defs, &ame->ame_ndefs);
	}
	for (uint64_t i = 0; i < ame->ame_ndefs; i++) {
		ame->ame_defer_txg = MIN(ame->ame_defer_txg,
		    ame->ame_defs[i].ald_txg);
	}
	return (error);
}

/* Set the import-time write mode and record why it could not stay behind. */
static void
async_mirror_import_off(spa_t *spa, vdev_t *lag, const char *reason)
{
	VERIFY3U(lag->vdev_async_mirror, ==, VDEV_ASYNC_MIRROR_LAGGING);
	VERIFY0(spa->spa_async_mirror_import.ami_off);

	lag->vdev_async_mirror = VDEV_ASYNC_MIRROR_OFF;
	spa->spa_async_mirror_import.ami_off = lag->vdev_guid;
	spa->spa_async_mirror_import.ami_off_reason = reason;
	spa_load_note(spa, "async mirror: write_mode=sync for child guid %llu "
	    "because %s", (u_longlong_t)lag->vdev_guid, reason);
}

/* Find the requested lagging leaf and reject multiple requests. */
static uint64_t
async_mirror_wanted(spa_t *spa, boolean_t *multiplep)
{
	vdev_t *rvd = spa->spa_root_vdev;
	uint64_t want = 0;

	*multiplep = B_FALSE;
	for (uint64_t c = 0; c < rvd->vdev_children; c++) {
		vdev_t *tvd = rvd->vdev_child[c];

		if (tvd->vdev_ops != &vdev_mirror_ops)
			continue;
		for (uint64_t i = 0; i < tvd->vdev_children; i++) {
			vdev_t *leaf = tvd->vdev_child[i];

			if (leaf->vdev_async_mirror !=
			    VDEV_ASYNC_MIRROR_LAGGING)
				continue;
			if (want != 0) {
				zfs_dbgmsg("async mirror: more than one leaf "
				    "is lagging; taking none");
				*multiplep = B_TRUE;
				spa->spa_async_mirror_import.
				    ami_off_all = B_TRUE;
				spa_load_note(spa,
				    "async mirror: more than one leaf "
				    "is marked "
				    "behind; none can start, so each is set to "
				    "write_mode=sync at the first txg");
				return (0);
			}
			want = leaf->vdev_guid;
		}
	}
	return (want);
}

/* Count records above the txg that the lagging child has applied. */
static void
async_mirror_load_pending(vdev_async_mirror_t *am)
{
	for (async_mirror_txg_t *at = list_head(&am->am_txgs); at != NULL;
	    at = list_next(&am->am_txgs, at)) {
		if (at->at_txg > am->am_applied_txg) {
			am->am_pending_bytes += at->at_bytes;
			am->am_pending_txgs++;
		}
	}
}

/* Attach only through the applied txg; later txgs are copied again. */
static void
async_mirror_defer_attach(vdev_async_mirror_t *am,
    const async_mirror_load_defer_t *defs, uint64_t nd)
{
	spa_async_mirror_stats_t *sams = &am->am_spa->spa_async_mirror_stats;
	uint64_t i = 0;

	for (async_mirror_txg_t *at = list_head(&am->am_txgs); at != NULL &&
	    at->at_txg <= am->am_applied_txg;
	    at = list_next(&am->am_txgs, at)) {
		uint_t k = 0;

		ASSERT(list_is_empty(&at->at_defer));
		while (i < nd && defs[i].ald_txg < at->at_txg)
			i++;
		for (; i < nd && defs[i].ald_txg == at->at_txg; i++) {
			async_mirror_defer_t *ad = kmem_zalloc(sizeof (*ad),
			    KM_SLEEP);

			ad->ad_bp = defs[i].ald_bp;
			ad->ad_zb = defs[i].ald_zb;
			list_insert_tail(&at->at_defer, ad);
			k++;
		}
		if (k == 0)
			continue;
		at->at_ndefer_count = k;
		at->at_ndefer = k;
		sams->sams_data_deferred += k;
		zfs_dbgmsg("async mirror: txg %llu: %u deferred data blocks "
		    "to try", (u_longlong_t)at->at_txg, k);
	}
	mutex_enter(&am->am_lock);
	async_mirror_defer_floor(am);
	mutex_exit(&am->am_lock);
}

/* Start a new copy when no saved records or freeze exist. */
static void
async_mirror_load_new(spa_t *spa, vdev_async_mirror_t *am, uint64_t want)
{
	vdev_t *lag = vdev_lookup_by_guid(spa->spa_root_vdev, want);
	vdev_t *lead = NULL;
	uint64_t import_txg = spa->spa_uberblock.ub_txg;
	uberblock_t ub;
	uberblock_t newest;
	int start_error;
	const char *reason;

	if (lag == NULL || !lag->vdev_ops->vdev_op_leaf) {
		zfs_dbgmsg("async mirror: lagging leaf not found or not "
		    "usable");
		return;
	}
	boolean_t incomplete = (!lag->vdev_offline &&
	    (vdev_is_dead(lag) || !vdev_readable(lag) ||
	    !vdev_dtl_empty(lag, DTL_MISSING) ||
	    !vdev_dtl_empty(lag, DTL_PARTIAL)));
	if (incomplete) {
		async_mirror_mark_dtl(spa, lag, TXG_INITIAL,
		    import_txg - TXG_INITIAL + 1);
		async_mirror_note_resync(spa, lag);
		async_mirror_import_off(spa, lag,
		    "it was missing or incomplete at import; a normal "
		    "resilver is required");
		return;
	}

	start_error = async_mirror_can_start(spa, lag, &lead);
	if (start_error != 0) {
		reason = "the pool layout or feature did not allow it to start";
		if (start_error == ENXIO)
			reason = "a mirror child was not usable at import";
		else if (start_error == EBUSY)
			reason = "a mirror child needed a resilver at import";
		else if (start_error == ZFS_ERR_ASYNC_MIRROR_MULTIHOST)
			reason = "multihost mode prevented the mirror from "
			    "starting";
		else if (start_error == ZFS_ERR_CHECKPOINT_EXISTS)
			reason = "a pool checkpoint prevented the mirror from "
			    "starting";
		zfs_dbgmsg("async mirror: lagging leaf cannot start, error %d",
		    start_error);
		async_mirror_import_off(spa, lag, reason);
		return;
	}

	async_mirror_read_leaf_ub(spa, lag, &ub, &newest, NULL);
	if (ub.ub_txg < import_txg &&
	    !async_mirror_same_root(&ub, &spa->spa_uberblock)) {
		async_mirror_mark_dtl(spa, lag, ub.ub_txg + 1,
		    import_txg - ub.ub_txg);
		async_mirror_note_resync(spa, lag);
		async_mirror_import_off(spa, lag,
		    "it was incomplete at import; a normal resilver is "
		    "required");
	} else if (newest.ub_txg > import_txg ||
	    !async_mirror_same_root(&ub, &spa->spa_uberblock)) {
		zfs_dbgmsg("async mirror: child ahead or divergent, not "
		    "activating");
		async_mirror_import_off(spa, lag,
		    "it was ahead of or divergent from the imported pool");
	} else {
		async_mirror_load_state(am, lag, lead, AMS_ACTIVE);
		am->am_applied_txg = ub.ub_txg;
	}
}

/* Select record replay, drain, or a new copy before import cleanup. */
static void
async_mirror_import_select_state(spa_t *spa, vdev_async_mirror_t *am,
    const async_mirror_entries_t *ame, uint64_t want)
{
	if (ame->ame_nrecs > 0) {
		async_mirror_resume_or_drain(spa, am, ame->ame_recs,
		    ame->ame_nrecs, ame->ame_child_guid, ame->ame_frozen,
		    ame->ame_defer_txg, want,
		    ame->ame_off ? MAX(ame->ame_off_txg, 1) : 0);
		async_mirror_load_pending(am);
		/* Set am_applied_txg before attaching deferred blocks. */
		if (async_mirror_copying(am->am_state))
			async_mirror_defer_attach(am, ame->ame_defs,
			    ame->ame_ndefs);
	} else if (ame->ame_zap != 0 || ame->ame_frozen ||
	    ame->ame_dzap != 0 || ame->ame_off) {
		async_mirror_load_state(am, NULL, NULL, AMS_DRAINING);
	} else if (want != 0) {
		async_mirror_load_new(spa, am, want);
	}

	/* Resolve a freeze after record selection erases an ahead child. */
	if (ame->ame_frozen)
		async_mirror_frozen_load(spa, am, &ame->ame_child,
		    ame->ame_child_guid, ame->ame_nrecs, want);
}

/* Restore import flags and offline state before reading the saved entries. */
static int
async_mirror_import_prepare(spa_t *spa)
{
	spa->spa_async_mirror_import.ami_resync[0] = 0;
	spa->spa_async_mirror_import.ami_resync[1] = 0;
	spa->spa_async_mirror_import.ami_off_all = B_FALSE;
	int error = async_mirror_erase_skipped(spa);

	if (error != 0)
		return (error);
	async_mirror_mark_erased(spa);
	uint64_t offline_guid = spa->spa_async_mirror_import.ami_skip_offline;
	if (offline_guid != 0) {
		vdev_t *offline_vd = vdev_lookup_by_guid(spa->spa_root_vdev,
		    offline_guid);

		VERIFY3P(offline_vd, !=, NULL);
		spa_config_enter(spa, SCL_STATE_ALL, FTAG, RW_WRITER);
		offline_vd->vdev_offline = B_TRUE;
		vdev_close(offline_vd);
		vdev_propagate_state(offline_vd->vdev_parent);
		spa_config_exit(spa, SCL_STATE_ALL, FTAG);
		spa->spa_async_mirror_import.ami_skip_offline = 0;
	}
	return (0);
}

/* Turn an unfulfilled lagging request off after saved state is selected. */
static void
async_mirror_import_request_check(spa_t *spa, vdev_async_mirror_t *am,
    const async_mirror_entries_t *ame, uint64_t *wantp,
    boolean_t multiple_properties)
{
	uint64_t want = *wantp;

	if (spa->spa_async_mirror_import.ami_off != 0) {
		*wantp = 0;
	} else if (want != 0 && !multiple_properties &&
	    am->am_lagging == NULL) {
		vdev_t *lag = vdev_lookup_by_guid(spa->spa_root_vdev, want);

		if (lag != NULL && lag->vdev_ops->vdev_op_leaf) {
			if (!lag->vdev_offline &&
			    (vdev_is_dead(lag) || !vdev_readable(lag) ||
			    !vdev_dtl_empty(lag, DTL_MISSING) ||
			    !vdev_dtl_empty(lag, DTL_PARTIAL))) {
				async_mirror_mark_dtl(spa, lag, TXG_INITIAL,
				    spa->spa_uberblock.ub_txg -
				    TXG_INITIAL + 1);
				async_mirror_note_resync(spa, lag);
				async_mirror_import_off(spa, lag,
				    "it was missing or incomplete at import; a "
				    "normal resilver is required");
			} else {
				const char *reason;

				if (ame->ame_frozen)
					reason = "the imported freeze ended";
				else if (ame->ame_off)
					reason = "the switch-off entry says "
					    "the child "
					    "had left the async mirror";
				else if (ame->ame_nrecs != 0)
					reason = "the saved async mirror "
					    "could not "
					    "resume at import";
				else
					reason = "the async mirror could not "
					    "start "
					    "at import";

				async_mirror_import_off(spa, lag, reason);
			}
			*wantp = 0;
		} else {
			spa_load_note(spa,
			    "async mirror: lagging leaf guid %llu was not "
			    "found as a leaf; the async mirror did not start",
			    (u_longlong_t)want);
		}
	}
}

/* Clear a replacement reservation only after its leaf is current. */
static void
async_mirror_import_replace_check(spa_t *spa, vdev_async_mirror_t *am,
    const async_mirror_entries_t *ame)
{
	if (ame->ame_replace_guid == 0)
		return;

	vdev_t *replace = vdev_lookup_by_guid(spa->spa_root_vdev,
	    ame->ame_replace_guid);
	boolean_t stale = (replace == NULL ||
	    !replace->vdev_ops->vdev_op_leaf);

	for (vdev_t *pvd = stale ? NULL : replace->vdev_parent;
	    pvd != NULL; pvd = pvd->vdev_parent) {
		if (pvd->vdev_ops == &vdev_replacing_ops) {
			stale = B_TRUE;
			break;
		}
	}
	if (!stale && am->am_lagging == replace &&
	    async_mirror_copying(am->am_state)) {
		stale = B_TRUE;
	} else if (!stale && am->am_lagging == replace &&
	    (am->am_state == AMS_FAILED || am->am_state == AMS_FROZEN)) {
		stale = B_FALSE;
	} else if (!stale) {
		stale = (!vdev_is_dead(replace) &&
		    vdev_readable(replace) && vdev_writeable(replace) &&
		    vdev_dtl_empty(replace, DTL_MISSING) &&
		    vdev_dtl_empty(replace, DTL_PARTIAL));
	}
	if (stale) {
		am->am_replace_cleanup_guid = ame->ame_replace_guid;
		am->am_replace_guid = 0;
	}
}

/* Set lineage and the import mark before labels or the copy thread change. */
static void
async_mirror_import_mark(spa_t *spa, vdev_async_mirror_t *am,
    const async_mirror_entries_t *ame, uint64_t want)
{
	if (am->am_lagging != NULL && spa_async_mirror_lineage(spa) == 0)
		async_mirror_lineage_start(spa);
	async_mirror_lineage_update(spa,
	    ame->ame_zap != 0 || ame->ame_frozen || want != 0);
	/* Keep the leaf marker consistent with the selected applied txg. */
	async_mirror_mark_at_load(spa, am->am_lagging, am->am_applied_txg);
}

/* Refresh the lagging leaf labels before import starts the copy thread. */
static void
async_mirror_import_label_refresh(spa_t *spa, vdev_async_mirror_t *am)
{
	if (am->am_lagging == NULL || !vdev_writeable(am->am_lagging) ||
	    async_mirror_label_has_legacy_mark(spa, am->am_lagging))
		return;

	vdev_t *lag = am->am_lagging;
	uint64_t old = 0;
	ASSERT(!spa->spa_sync_on);

	/* Applied txg is safe; deferred blocks keep a lower read fence. */
	spa_config_enter(spa, SCL_STATE_ALL, FTAG, RW_WRITER);
	nvlist_t *label = vdev_label_read_config(lag, UINT64_MAX,
	    VDEV_TRUSTED_LABELS(lag));
	spa_config_exit(spa, SCL_STATE_ALL, FTAG);
	if (label != NULL) {
		(void) nvlist_lookup_uint64(label, ZPOOL_CONFIG_POOL_TXG, &old);
		nvlist_free(label);
	} else {
		zfs_dbgmsg("async mirror: no label of the lagging child guid "
		    "%llu can be read; its labels are written at txg %llu",
		    (u_longlong_t)lag->vdev_guid,
		    (u_longlong_t)am->am_applied_txg);
	}
	/* Preserve the rewind floor when selecting a label txg. */
	uint64_t label_txg = (old == 0 || old >= am->am_applied_txg) ?
	    am->am_applied_txg : old + 1;

	/* Hold the vdev configuration stable while the labels are written. */
	spa_config_enter(spa, SCL_CONFIG | SCL_STATE, FTAG, RW_READER);
	int error = vdev_async_mirror_label_sync(lag, label_txg,
	    am->am_applied_txg);
	spa_config_exit(spa, SCL_CONFIG | SCL_STATE, FTAG);
	if (error != 0) {
		spa_load_note(spa,
		    "async mirror: label refresh failed for "
		    "child guid %llu, error %d; child stays online and "
		    "needs full resilver",
		    (u_longlong_t)lag->vdev_guid, error);
		async_mirror_mark_at_load(spa, NULL, 0);
		async_mirror_load_state(am, NULL, NULL, AMS_DRAINING);
		am->am_applied_txg = UINT64_MAX;
		async_mirror_mark_dtl(spa, lag, TXG_INITIAL,
		    spa->spa_uberblock.ub_txg - TXG_INITIAL + 1);
		async_mirror_note_resync(spa, lag);
		async_mirror_import_off(spa, lag,
		    "its label refresh failed at import; a normal resilver "
		    "is required");
	}
}

/* Pick the lagging child during import before sync starts. */
int
vdev_async_mirror_activate(spa_t *spa)
{
	async_mirror_entries_t ame;
	vdev_async_mirror_t *am = NULL;
	uint64_t want;
	boolean_t multiple_properties;
	int error;

	if (!spa_writeable(spa))
		return (0);

	error = async_mirror_import_prepare(spa);
	if (error != 0)
		return (error);

	error = async_mirror_entries_load(spa, &ame);
	if (error != 0)
		goto out;

	want = async_mirror_wanted(spa, &multiple_properties);
	am = async_mirror_create(spa);
	am->am_zap_obj = ame.ame_zap;
	am->am_defer_zap = ame.ame_dzap;
	am->am_entry_guid = ame.ame_child_guid;
	am->am_off_entry = ame.ame_off;
	am->am_frozen_entry = ame.ame_frozen;
	am->am_replace_guid = ame.ame_replace_guid;
	if ((ame.ame_child.amc_flags &
	    ASYNC_MIRROR_CHILD_DTL_PENDING) != 0) {
		am->am_dtl_child_guid = ame.ame_child_guid;
		am->am_dtl_child_clearing = B_FALSE;
		am->am_dtl_txg = ame.ame_child.amc_reserved[
		    ASYNC_MIRROR_CHILD_DTL_TXG_WORD];
		am->am_dtl_range_txg = spa->spa_uberblock.ub_txg;
		am->am_fail_from = spa->spa_uberblock.ub_txg;
	}

	/* Check every record before anything is changed. */
	error = async_mirror_load_index(spa, am, ame.ame_recs, ame.ame_nrecs);
	if (error != 0)
		goto out;
	async_mirror_import_select_state(spa, am, &ame, want);
	/* Apply sync mode after saved state has been resolved. */
	async_mirror_import_request_check(spa, am, &ame, &want,
	    multiple_properties);
	async_mirror_import_replace_check(spa, am, &ame);

	async_mirror_import_mark(spa, am, &ame, want);
	async_mirror_import_label_refresh(spa, am);

	/* Keep state while active, draining, or requested by a leaf. */
	if (am->am_state == AMS_OFF && want == 0 &&
	    am->am_replace_guid == 0 && am->am_replace_cleanup_guid == 0 &&
	    am->am_dtl_child_guid == 0)
		goto out;

	spa_async_mirror_stats_init(spa);
	spa->spa_async_mirror = am;
	mutex_enter(&am->am_lock);
	async_mirror_update_stats(am);
	if (async_mirror_copying(am->am_state)) {
		ASSERT3P(am->am_leader->vdev_parent, ==,
		    am->am_lagging->vdev_parent);
		am->am_thread = thread_create(NULL, 0, async_mirror_thread, am,
		    0, &p0, TS_RUN, minclsyspri);
		zfs_dbgmsg("async mirror: active, lagging child guid %llu at "
		    "txg %llu", (u_longlong_t)am->am_lagging->vdev_guid,
		    (u_longlong_t)am->am_applied_txg);
	}
	mutex_exit(&am->am_lock);
	am = NULL;
out:
	if (error != 0) {
		spa_load_note(spa, "async mirror: cannot load the records of "
		    "the async mirror: error %d", error);
	}
	if (am != NULL) {
		async_mirror_free_index(am);
		async_mirror_destroy(am);
	}
	if (ame.ame_recs != NULL)
		vmem_free(ame.ame_recs,
		    ame.ame_nrecs * sizeof (*ame.ame_recs));
	if (ame.ame_defs != NULL)
		vmem_free(ame.ame_defs,
		    ame.ame_ndefs * sizeof (*ame.ame_defs));
	return (error);
}

/*
 * Called from spa_unload() after the sync thread has stopped: copy what is
 * left, so a clean export leaves both children at the same txg.
 */
void
vdev_async_mirror_stop(spa_t *spa)
{
	vdev_async_mirror_t *am = spa->spa_async_mirror;

	ASSERT(!spa->spa_sync_on);

	if (am == NULL)
		return;

	mutex_enter(&am->am_lock);
	am->am_stop = B_TRUE;
	cv_broadcast(&am->am_cv);
	while (am->am_thread != NULL)
		cv_wait(&am->am_drain_cv, &am->am_lock);
	mutex_exit(&am->am_lock);

	/* spa_async_suspend() cancelled these threads before this. */
	if (am->am_release_zthr != NULL) {
		zthr_destroy(am->am_release_zthr);
		am->am_release_zthr = NULL;
	}
	if (am->am_giveup_zthr != NULL) {
		zthr_destroy(am->am_giveup_zthr);
		am->am_giveup_zthr = NULL;
	}
}

/* Free state after spa_unload() waits for pool I/O. */
void
vdev_async_mirror_fini(spa_t *spa)
{
	vdev_async_mirror_t *am = spa->spa_async_mirror;

	if (am == NULL)
		return;

	async_mirror_free_index(am);
	if (am->am_claim_rt != NULL) {
		zfs_range_tree_vacate(am->am_claim_rt, NULL, NULL);
		zfs_range_tree_destroy(am->am_claim_rt);
	}
	/* The vdevs outlive the state: take the state off the leaf. */
	mutex_enter(&am->am_lock);
	async_mirror_set_state(am, AMS_OFF);
	mutex_exit(&am->am_lock);

	spa->spa_async_mirror = NULL;
	spa->spa_async_mirror_stats.sams_active = 0;
	spa->spa_async_mirror_stats.sams_data_deferred = 0;
	spa_async_mirror_stats_destroy(spa);
	async_mirror_destroy(am);
}

ZFS_MODULE_PARAM(zfs_vdev_async_mirror, zfs_vdev_async_mirror_,
	read_retries, UINT, ZMOD_RW,
	"Times to try the copy of a txg again after a read error");

ZFS_MODULE_PARAM(zfs_vdev_async_mirror, zfs_vdev_async_mirror_,
	suspend_copy, INT, ZMOD_RW,
	"Stop the copy to the lagging child (for testing)");

ZFS_MODULE_PARAM(zfs_vdev_async_mirror, zfs_vdev_async_mirror_,
	suspend_release, INT, ZMOD_RW,
	"Stop the release of the records of the copy (for testing)");

ZFS_MODULE_PARAM(zfs_vdev_async_mirror, zfs_vdev_async_mirror_,
	inject_dtl_delay, UINT, ZMOD_RW,
	"Withhold child labels after a failure DTL for this many txgs "
	"(for testing only)");

ZFS_MODULE_PARAM(zfs_vdev_async_mirror, zfs_vdev_async_mirror_,
	inject_dtl_label_flush, UINT, ZMOD_RW,
	"Drop DTL label writes and fail the child flush (for testing only)");

ZFS_MODULE_PARAM(zfs_vdev_async_mirror, zfs_vdev_async_mirror_,
	copy_delay_ms, UINT, ZMOD_RW,
	"Milliseconds that the copy to the lagging child waits before each "
	"block (for testing)");

ZFS_MODULE_PARAM(zfs_vdev_async_mirror, zfs_vdev_async_mirror_,
	max_pending_bytes, U64, ZMOD_RW,
	"Slow writes as the bytes that wait to be copied to the lagging child "
	"approach this limit, and wait above it");

ZFS_MODULE_PARAM(zfs_vdev_async_mirror, zfs_vdev_async_mirror_,
	max_pending_txgs, U64, ZMOD_RW,
	"Slow writes as the txgs that wait to be copied to the lagging child "
	"approach this limit, and wait above it");

ZFS_MODULE_PARAM(zfs_vdev_async_mirror, zfs_vdev_async_mirror_,
	copy_limit, U64, ZMOD_RW,
	"Maximum bytes of the copy to the lagging child in flight");

ZFS_MODULE_PARAM(zfs_vdev_async_mirror, zfs_vdev_async_mirror_,
	sort_limit, U64, ZMOD_RW,
	"Maximum bytes of memory for the blocks that the copy to the lagging "
	"child sorts before it issues them");

ZFS_MODULE_PARAM(zfs_vdev_async_mirror, zfs_vdev_async_mirror_,
	release_limit, U64, ZMOD_RW,
	"Maximum number of held ranges that one release sync task may "
	"release (0 = no limit; for testing)");

ZFS_MODULE_PARAM(zfs_vdev_async_mirror, zfs_vdev_async_mirror_,
	release_ms, UINT, ZMOD_RW,
	"Maximum time in milliseconds that one release sync task may take "
	"(0 = no limit)");

ZFS_MODULE_PARAM(zfs_vdev_async_mirror, zfs_vdev_async_mirror_,
	max_held_percent, UINT, ZMOD_RW,
	"Slow writes as the held space that waits for its release approaches "
	"this percentage of the pool (0 = off)");

ZFS_MODULE_PARAM(zfs_vdev_async_mirror, zfs_vdev_async_mirror_,
	held_limit_percent, UINT, ZMOD_RW,
	"Freeze the lagging child when the held space that waits for it "
	"reaches this percentage of the pool (0 = off)");

ZFS_MODULE_PARAM(zfs_vdev_async_mirror, zfs_vdev_async_mirror_,
	held_limit_secs, UINT, ZMOD_RW,
	"Seconds before the child is frozen if held space stays at its limit "
	"without progress, or a normal copy does not pass the newest txg "
	"during low space");

ZFS_MODULE_PARAM(zfs_vdev_async_mirror, zfs_vdev_async_mirror_,
	held_free_percent, UINT, ZMOD_RW,
	"Freeze the lagging child when held space waits for it and the "
	"datasets have less than this percentage of the pool left (0 = off)");

ZFS_MODULE_PARAM(zfs_vdev_async_mirror, zfs_vdev_async_mirror_,
	stall_secs, UINT, ZMOD_RW,
	"Freeze the lagging child when its copy makes no progress for this "
	"many seconds while writes wait for it (0 = off)");

ZFS_MODULE_PARAM(zfs_vdev_async_mirror, zfs_vdev_async_mirror_,
	load_max_bytes, U64, ZMOD_RW,
	"Maximum bytes that an import allocates for the list of the records "
	"or of the deferred blocks (0 = no limit)");

#define	UB_ERASE_EMPTY		0
#define	UB_ERASE_UNREAD	UINT64_MAX

static void
vdev_async_mirror_erase_read_done(zio_t *zio)
{
	uint64_t *state = zio->io_private;
	uberblock_t *ub = abd_to_buf(zio->io_abd);

	if (zio->io_error == 0) {
		*state = (uberblock_verify(ub) == 0 && ub->ub_txg != 0) ?
		    ub->ub_txg : UB_ERASE_EMPTY;
	} else if (zio->io_error == ECKSUM) {
		*state = UB_ERASE_EMPTY;
	} else {
		*state = UB_ERASE_UNREAD;
	}
	abd_free(zio->io_abd);
}

static void
vdev_async_mirror_erase_read(vdev_t *vd, uint64_t *state, int flags)
{
	int count = VDEV_UBERBLOCK_COUNT(vd);
	zio_t *zio = zio_root(vd->vdev_spa, NULL, NULL, flags);

	for (int l = 0; l < VDEV_LABELS; l++) {
		for (int n = 0; n < count; n++) {
			state[l * count + n] = UB_ERASE_UNREAD;
			vdev_label_read(zio, vd, l,
			    abd_alloc_linear(VDEV_UBERBLOCK_SIZE(vd), B_TRUE),
			    VDEV_UBERBLOCK_OFFSET(vd, n),
			    VDEV_UBERBLOCK_SIZE(vd),
			    vdev_async_mirror_erase_read_done,
			    &state[l * count + n],
			    flags | ZIO_FLAG_SPECULATIVE);
		}
	}
	(void) zio_wait(zio);
}

static int
vdev_async_mirror_erase_write(vdev_t *vd, uint64_t *state, boolean_t phase1,
    uint64_t max_txg, abd_t *zeros, int flags)
{
	int count = VDEV_UBERBLOCK_COUNT(vd);
	int error;
	zio_t *zio = zio_root(vd->vdev_spa, NULL, NULL, flags);

	for (int l = 0; l < VDEV_LABELS; l++) {
		for (int n = 0; n < count; n++) {
			uint64_t s = state[l * count + n];

			if ((phase1 && (s == UB_ERASE_EMPTY ||
			    s == UB_ERASE_UNREAD || s > max_txg)) ||
			    (!phase1 && s == UB_ERASE_EMPTY))
				continue;
			vdev_label_write(zio, vd, l, zeros,
			    VDEV_UBERBLOCK_OFFSET(vd, n),
			    VDEV_UBERBLOCK_SIZE(vd), NULL, NULL, flags);
			state[l * count + n] = UB_ERASE_EMPTY;
		}
	}
	error = zio_wait(zio);
	if (error == 0) {
		zio = zio_root(vd->vdev_spa, NULL, NULL, flags);
		zio_flush_leaf(zio, vd);
		error = zio_wait(zio);
	}
	return (error);
}

int
vdev_uberblock_erase(vdev_t *vd, uint64_t max_txg)
{
	spa_t *spa = vd->vdev_spa;
	int flags = ZIO_FLAG_CONFIG_WRITER | ZIO_FLAG_CANFAIL;
	int count = VDEV_UBERBLOCK_COUNT(vd);
	size_t size = VDEV_LABELS * count * sizeof (uint64_t);
	uint64_t *state;
	abd_t *zeros;
	int error = 0;

	ASSERT(vd->vdev_ops->vdev_op_leaf);
	ASSERT(!spa->spa_sync_on);
	if (!spa_feature_is_active(spa, SPA_FEATURE_ASYNC_MIRROR))
		return (SET_ERROR(ENOTSUP));

	state = kmem_alloc(size, KM_SLEEP);
	zeros = abd_alloc_linear(VDEV_UBERBLOCK_SIZE(vd), B_TRUE);
	abd_zero(zeros, VDEV_UBERBLOCK_SIZE(vd));
	spa_config_enter(spa, SCL_STATE_ALL, FTAG, RW_READER);

	vdev_async_mirror_erase_read(vd, state, flags);
	for (int i = 0; i < VDEV_LABELS * count; i++) {
		if (state[i] == UB_ERASE_UNREAD) {
			error = SET_ERROR(EIO);
			break;
		}
	}
	if (error == 0)
		error = vdev_async_mirror_erase_write(vd, state, B_TRUE,
		    max_txg, zeros, flags);
	if (error == 0)
		error = vdev_async_mirror_erase_write(vd, state, B_FALSE,
		    max_txg, zeros, flags);
	if (error == 0) {
		vdev_async_mirror_erase_read(vd, state, flags);
		for (int i = 0; i < VDEV_LABELS * count; i++) {
			if (state[i] != UB_ERASE_EMPTY) {
				error = SET_ERROR(EIO);
				break;
			}
		}
	}

	spa_config_exit(spa, SCL_STATE_ALL, FTAG);
	abd_free(zeros);
	kmem_free(state, size);
	return (error);
}

boolean_t
vdev_async_mirror_resilver_wanted(vdev_t *vd)
{
	if (vd->vdev_children == 0) {
		return (vdev_resilver_needed(vd, NULL, NULL) &&
		    !vdev_async_mirror_dtl_fence(vd));
	}
	for (int c = 0; c < vd->vdev_children; c++) {
		if (vdev_async_mirror_resilver_wanted(vd->vdev_child[c]))
			return (B_TRUE);
	}
	return (B_FALSE);
}

void
vdev_async_mirror_construct_zaps(vdev_t *vd, dmu_tx_t *tx)
{
	if (!vd->vdev_ops->vdev_op_leaf ||
	    !vd->vdev_async_mirror_pending)
		return;

	spa_t *spa = vd->vdev_spa;
	uint64_t value = (vd->vdev_async_mirror ==
	    VDEV_ASYNC_MIRROR_LAGGING) ? VDEV_WRITE_MODE_BEHIND :
	    VDEV_WRITE_MODE_SYNC;
	int error;

	VERIFY3U(vd->vdev_leaf_zap, !=, 0);
	VERIFY3U(value, ==, VDEV_WRITE_MODE_BEHIND);
	VERIFY0(zap_update(spa->spa_meta_objset, vd->vdev_leaf_zap,
	    vdev_prop_to_name(VDEV_PROP_WRITE_MODE), sizeof (uint64_t), 1,
	    &value, tx));
	error = zap_remove(spa->spa_meta_objset, vd->vdev_leaf_zap,
	    vdev_prop_to_name(VDEV_PROP_READ_MODE), tx);
	VERIFY(error == 0 || error == ENOENT);
	vd->vdev_read_mode = VDEV_READ_MODE_FALLBACK;
	vd->vdev_async_mirror_pending = B_FALSE;

	if (vd->vdev_async_mirror_replace_pending) {
		uint64_t guid;

		if (vdev_async_mirror_replace_pending(spa, &guid) == 0)
			vdev_async_mirror_replace_clear_sync(spa, guid, tx);
		vd->vdev_async_mirror_replace_pending = B_FALSE;
	}
}

void
vdev_async_mirror_vdev_load(vdev_t *vd)
{
	int error = vdev_prop_get_int(vd, VDEV_PROP_READ_MODE,
	    &vd->vdev_read_mode);

	if (error && error != ENOENT)
		vdev_dbgmsg(vd, "vdev_load: cannot read the read_mode property "
		    "[error=%d]", error);

	if (spa_feature_is_enabled(vd->vdev_spa, SPA_FEATURE_ASYNC_MIRROR)) {
		error = vdev_prop_get_int(vd, VDEV_PROP_WRITE_MODE,
		    &vd->vdev_async_mirror);
		if (error && error != ENOENT)
			vdev_dbgmsg(vd, "vdev_load: cannot read the write_mode "
			    "property [error=%d]", error);
		if (vd->vdev_async_mirror == VDEV_ASYNC_MIRROR_LAGGING)
			vd->vdev_read_mode = VDEV_READ_MODE_FALLBACK;
	}
}

int
vdev_async_mirror_replace_attach_check(spa_t *spa, uint64_t guid,
    vdev_t *vd, int replacing, vdev_async_mirror_replace_ctx_t *ctx)
{
	uint64_t pending_guid = 0;
	int error = vdev_async_mirror_replace_pending(spa, &pending_guid);

	if (error != 0 && error != ENOENT)
		return (error);
	if (error == 0 && pending_guid == guid) {
		ctx->vamrc_pending = B_TRUE;
		if (!replacing || vd->vdev_parent == NULL ||
		    vd->vdev_parent->vdev_ops != &vdev_mirror_ops ||
		    vd->vdev_parent->vdev_children != 2 ||
		    vd->vdev_parent != vd->vdev_top)
			return (SET_ERROR(EBUSY));
		ctx->vamrc_replace = B_TRUE;
	}

	if (vdev_async_mirror_is_child(vd) ||
	    vdev_async_mirror_failed_lagging(vd)) {
		if (!replacing || !vdev_async_mirror_failed_lagging(vd))
			return (SET_ERROR(ZFS_ERR_ASYNC_MIRROR_ACTIVE));
		ctx->vamrc_replace = B_TRUE;
	}
	return (0);
}

int
vdev_async_mirror_replace_attach_prepare(spa_t *spa, uint64_t guid,
    vdev_t *oldvd, vdev_t *newvd, int replacing, int rebuild, uint64_t txg,
    vdev_t *newrootvd, vdev_async_mirror_replace_ctx_t *ctx)
{
	int error;

	if (rebuild && ctx->vamrc_replace &&
	    vdev_async_mirror_failed_lagging(oldvd))
		return (spa_vdev_exit(spa, newrootvd, txg,
		    ZFS_ERR_ASYNC_MIRROR_REBUILD));
	if (ctx->vamrc_replace && newvd->vdev_isspare)
		return (spa_vdev_exit(spa, newrootvd, txg,
		    ZFS_ERR_ASYNC_MIRROR_ACTIVE));
	if (ctx->vamrc_replace)
		newvd->vdev_async_mirror_replace_pending = B_TRUE;
	if (replacing && (error = vdev_async_mirror_replace_check(oldvd,
	    newvd)) != 0)
		return (spa_vdev_exit(spa, newrootvd, txg, error));
	if (!ctx->vamrc_replace)
		return (0);

	if (!ctx->vamrc_pending) {
		(void) spa_vdev_exit(spa, newrootvd, txg, 0);
		error = vdev_async_mirror_replace_begin(spa, guid);
		if (error != 0)
			return (error);
		error = vdev_async_mirror_replace_claim(spa, guid);
		if (error != 0)
			return (error);
		ctx->vamrc_marker_owned = B_TRUE;
		ctx->vamrc_claimed = B_TRUE;

		spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);
		oldvd = spa_lookup_by_guid(spa, guid, B_FALSE);
		boolean_t is_async_child = (oldvd != NULL &&
		    vdev_async_mirror_is_child(oldvd));
		spa_config_exit(spa, SCL_CONFIG, FTAG);
		if (oldvd == NULL) {
			vdev_async_mirror_replace_release(spa, guid);
			return (SET_ERROR(ENODEV));
		}
		if (is_async_child) {
			error = vdev_async_mirror_replace_off(spa, guid);
			if (error == 0)
				error = vdev_async_mirror_wait_off(spa, guid);
		}
		return (error == 0 ? ERESTART : error);
	}

	if (!ctx->vamrc_claimed) {
		error = vdev_async_mirror_replace_claim(spa, guid);
		if (error != 0)
			return (spa_vdev_exit(spa, newrootvd, txg, error));
		ctx->vamrc_marker_owned = B_TRUE;
		ctx->vamrc_claimed = B_TRUE;
	}
	if (vdev_async_mirror_is_child(oldvd)) {
		(void) spa_vdev_exit(spa, newrootvd, txg, 0);
		error = vdev_async_mirror_replace_off(spa, guid);
		if (error == 0)
			error = vdev_async_mirror_wait_off(spa, guid);
		return (error == 0 ? ERESTART : error);
	}
	return (0);
}

void
vdev_async_mirror_replace_attach_commit(spa_t *spa, uint64_t guid,
    vdev_async_mirror_replace_ctx_t *ctx)
{
	if (ctx->vamrc_claimed)
		vdev_async_mirror_replace_release(spa, guid);
	if (ctx->vamrc_replace)
		vdev_async_mirror_failed_clear(spa, guid);
}

void
vdev_async_mirror_replace_attach_abort(spa_t *spa, uint64_t guid,
    vdev_async_mirror_replace_ctx_t *ctx)
{
	if (ctx->vamrc_marker_owned) {
		vdev_async_mirror_replace_release(spa, guid);
		(void) vdev_async_mirror_replace_abort(spa, guid);
	}
}
int
vdev_async_mirror_label_sync(vdev_t *vd, uint64_t txg, uint64_t child_txg)
{
	spa_t *spa;
	int flags;
	int error = 0;

	if (vd == NULL || !vd->vdev_ops->vdev_op_leaf ||
	    !vd->vdev_async_lagging || txg == 0 || txg > child_txg ||
	    child_txg > spa_last_synced_txg(vd->vdev_spa))
		return (SET_ERROR(EINVAL));

	spa = vd->vdev_spa;
	flags = ZIO_FLAG_CONFIG_WRITER | ZIO_FLAG_CANFAIL;
	ASSERT(spa_config_held(spa, SCL_CONFIG | SCL_STATE, RW_READER) ==
	    (SCL_CONFIG | SCL_STATE));
	for (int retry = 0; retry < 2; retry++) {
		for (int l = 0; l < 2; l++) {
			uint64_t good_writes = 0;
			zio_t *zio = zio_root(spa, NULL, NULL, flags);

			vdev_label_sync(zio, &good_writes, vd, l, txg, flags,
			    B_TRUE);
			error = zio_wait(zio);
			if (error == 0 && good_writes == 0)
				error = SET_ERROR(EIO);
			if (error != 0)
				break;
			zio = zio_root(spa, NULL, NULL, flags);
			zio_flush_leaf(zio, vd);
			error = zio_wait(zio);
			if (error == 0)
				vdev_async_mirror_label_flushed(vd, txg);
			if (error != 0)
				break;
		}
		if (error == 0)
			return (0);
		if (retry == 0)
			flags |= ZIO_FLAG_IO_RETRY;
	}
	zfs_dbgmsg("async mirror: label refresh failed for leaf guid %llu, "
	    "txg %llu, error %d", (u_longlong_t)vd->vdev_guid,
	    (u_longlong_t)txg, error);
	return (error);
}

static boolean_t
vdev_async_mirror_replacing(vdev_t *vd)
{
	if (vd->vdev_ops->vdev_op_leaf &&
	    vd->vdev_async_mirror == VDEV_ASYNC_MIRROR_LAGGING) {
		for (vdev_t *pvd = vd->vdev_parent; pvd != NULL;
		    pvd = pvd->vdev_parent) {
			if (pvd->vdev_ops == &vdev_replacing_ops ||
			    pvd->vdev_ops == &vdev_spare_ops)
				return (B_TRUE);
		}
	}
	for (uint64_t c = 0; c < vd->vdev_children; c++) {
		if (vdev_async_mirror_replacing(vd->vdev_child[c]))
			return (B_TRUE);
	}
	return (B_FALSE);
}

static const char *
vdev_async_mirror_config_state(vdev_t *vd)
{
	if (vd->vdev_ops->vdev_op_leaf) {
		const char *state = vdev_async_mirror_state(vd);

			return (strcmp(state, "off") == 0 ||
			    strcmp(state, "leader") == 0 ? NULL : state);
	}
	for (uint64_t c = 0; c < vd->vdev_children; c++) {
		const char *state =
		    vdev_async_mirror_config_state(vd->vdev_child[c]);

		if (state != NULL)
			return (state);
	}
	return ("off");
}

void
vdev_async_mirror_config_generate(spa_t *spa, vdev_t *rvd,
    nvlist_t *nvroot, boolean_t getstats)
{
	if (!getstats || rvd != spa->spa_root_vdev)
		return;
	if (vdev_async_mirror_replacing(rvd))
		fnvlist_add_boolean(nvroot,
		    ZPOOL_CONFIG_ASYNC_MIRROR_REPLACING);
	if (!vdev_async_mirror_active(spa) &&
	    vdev_async_mirror_failed_guid(spa) == 0)
		return;

	spa_async_mirror_stats_t *sams = &spa->spa_async_mirror_stats;
	nvlist_t *stats = fnvlist_alloc();

	fnvlist_add_uint64(stats, ZPOOL_CONFIG_ASYNC_MIRROR_APPLIED_TXG,
	    sams->sams_applied_txg);
	fnvlist_add_uint64(stats, ZPOOL_CONFIG_ASYNC_MIRROR_SYNCED_TXG,
	    sams->sams_synced_txg);
	fnvlist_add_uint64(stats, ZPOOL_CONFIG_ASYNC_MIRROR_PENDING,
	    sams->sams_pending_bytes);
	fnvlist_add_uint64(stats, ZPOOL_CONFIG_ASYNC_MIRROR_PENDING_TXGS,
	    sams->sams_pending_txgs);
	fnvlist_add_uint64(stats, ZPOOL_CONFIG_ASYNC_MIRROR_LAG_TXGS,
	    sams->sams_synced_txg > sams->sams_applied_txg ?
	    sams->sams_synced_txg - sams->sams_applied_txg : 0);
	fnvlist_add_string(stats, ZPOOL_CONFIG_ASYNC_MIRROR_STATE,
	    vdev_async_mirror_config_state(rvd));
	if (sams->sams_held_limit != 0) {
		fnvlist_add_boolean(stats,
		    ZPOOL_CONFIG_ASYNC_MIRROR_HELD_LIMIT);
		fnvlist_add_string(stats,
		    ZPOOL_CONFIG_ASYNC_MIRROR_GIVEUP_REASON,
		    vdev_async_mirror_giveup_reason(sams->sams_giveup_reason));
	}
	if (sams->sams_freeze_read != 0)
		fnvlist_add_boolean(stats,
		    ZPOOL_CONFIG_ASYNC_MIRROR_FREEZE_READ);
	fnvlist_add_nvlist(nvroot, ZPOOL_CONFIG_ASYNC_MIRROR_STATS, stats);
	fnvlist_free(stats);
}

void
vdev_async_mirror_vdev_config_generate(vdev_t *vd, nvlist_t *nvl,
    boolean_t getstats)
{
	if (vd->vdev_async_lagging != 0)
		fnvlist_add_uint64(nvl, ZPOOL_CONFIG_ASYNC_MIRROR_LAGGING,
		    vd->vdev_async_lagging);
	if (getstats && (vd->vdev_async_lagging != 0 ||
	    (vd->vdev_guid != 0 && vd->vdev_guid ==
	    vdev_async_mirror_failed_guid(vd->vdev_spa))))
		fnvlist_add_string(nvl, ZPOOL_CONFIG_ASYNC_MIRROR_STATE,
		    vdev_async_mirror_state(vd));
}

void
vdev_async_mirror_metaslab_sync(spa_t *spa, vdev_t *vd, metaslab_t *msp,
    zfs_range_tree_t *alloctree, uint64_t txg, dmu_tx_t *tx)
{
	ASSERT(MUTEX_HELD(&msp->ms_lock));
	if (msp->ms_holding != NULL &&
	    !zfs_range_tree_is_empty(msp->ms_holding)) {
		ASSERT(vdev_async_mirror_in_use(spa));
		mutex_exit(&msp->ms_lock);
		vdev_async_mirror_sync_write(spa, vd, msp->ms_holding,
		    SM_FREE, tx);
		mutex_enter(&msp->ms_lock);
		zfs_range_tree_vacate(msp->ms_holding, NULL, NULL);
	}
	if (spa->spa_async_mirror != NULL && vdev_async_mirror_tracks(vd)) {
		mutex_exit(&msp->ms_lock);
		vdev_async_mirror_sync_list(spa, vd, alloctree,
		    msp->ms_async_mirror_nolist[txg & TXG_MASK], tx);
		mutex_enter(&msp->ms_lock);
	}
}

zfs_range_tree_t *
metaslab_async_mirror_rt_create(metaslab_t *msp, const char *name)
{
	metaslab_group_t *mg = msp->ms_group;
	uint64_t start, shift;
	zfs_range_seg_type_t type = metaslab_calculate_range_tree_type(
	    mg->mg_vd, msp, &start, &shift);

	ASSERT(MUTEX_HELD(&msp->ms_lock));
	return (zfs_range_tree_create_flags(NULL, type, NULL, start, shift,
	    ZFS_RT_F_DYN_NAME, metaslab_rt_name(mg, msp, name)));
}

void
vdev_async_mirror_metaslab_sync_done(metaslab_t *msp, uint64_t txg)
{
	zfs_range_tree_t **nolistp =
	    &msp->ms_async_mirror_nolist[txg & TXG_MASK];

	if (*nolistp != NULL) {
		zfs_range_tree_vacate(*nolistp, NULL, NULL);
		zfs_range_tree_destroy(*nolistp);
		*nolistp = NULL;
	}
}

static void
vdev_async_mirror_metaslab_nolist_add(spa_t *spa, const dva_t *dva,
    uint64_t txg)
{
	vdev_t *vd = vdev_lookup_top(spa, DVA_GET_VDEV(dva));
	uint64_t offset = DVA_GET_OFFSET(dva);

	if (!vdev_is_concrete(vd) || vd->vdev_islog ||
	    (!vdev_async_mirror_tracks(vd) &&
	    (vd->vdev_ops != &vdev_mirror_ops || vd->vdev_children != 2)))
		return;

	metaslab_t *msp = vd->vdev_ms[offset >> vd->vdev_ms_shift];
	zfs_range_tree_t **rtp = &msp->ms_async_mirror_nolist[txg & TXG_MASK];

	mutex_enter(&msp->ms_lock);
	if (*rtp == NULL) {
		*rtp = metaslab_async_mirror_rt_create(msp,
		    "ms_async_mirror_nolist");
	}
	zfs_range_tree_add(*rtp, offset, DVA_GET_ASIZE(dva));
	mutex_exit(&msp->ms_lock);
}

void
vdev_async_mirror_metaslab_nolist_alloc(spa_t *spa, const blkptr_t *bp,
    const dva_t *dva, int ndvas, uint64_t txg, int flags)
{
	if ((flags & METASLAB_ZIL) == 0 ||
	    BP_GET_TYPE(bp) != DMU_OT_INTENT_LOG ||
	    spa->spa_async_mirror == NULL)
		return;
	for (int d = 0; d < ndvas; d++)
		vdev_async_mirror_metaslab_nolist_add(spa, &dva[d], txg);
}

void
vdev_async_mirror_metaslab_unalloc(metaslab_t *msp, uint64_t txg,
    uint64_t offset, uint64_t size)
{
	zfs_range_tree_t *nolist =
	    msp->ms_async_mirror_nolist[txg & TXG_MASK];

	if (nolist != NULL)
		zfs_range_tree_clear(nolist, offset, size);
}

boolean_t
vdev_async_mirror_metaslab_hold_free(spa_t *spa, vdev_t *vd,
    metaslab_t *msp, uint64_t offset, uint64_t size,
    metaslab_free_kind_t kind)
{
	if (spa->spa_async_mirror == NULL ||
	    !vdev_async_mirror_should_hold(vd, kind))
		return (B_FALSE);
	if (msp->ms_holding == NULL) {
		msp->ms_holding = metaslab_async_mirror_rt_create(msp,
		    "ms_holding");
	}
	zfs_range_tree_add(msp->ms_holding, offset, size);
	return (B_TRUE);
}

metaslab_free_kind_t
vdev_async_mirror_free_kind(spa_t *spa, const blkptr_t *bp)
{
	uint64_t birth = BP_GET_PHYSICAL_BIRTH(bp);

	if (BP_GET_TYPE(bp) != DMU_OT_INTENT_LOG)
		return (METASLAB_FREE_BLOCK);
	return (birth < spa_first_txg(spa) ||
	    birth <= spa->spa_claim_max_txg ? METASLAB_FREE_ZIL_CLAIMED :
	    METASLAB_FREE_ZIL);
}

void
vdev_async_mirror_metaslab_check_free(metaslab_t *msp, uint64_t offset,
    uint64_t size)
{
	if (msp->ms_holding != NULL)
		zfs_range_tree_verify_not_present(msp->ms_holding, offset,
		    size);
}

static void
vdev_async_mirror_scan_assess_leaves(dsl_pool_t *dp, vdev_t *vd)
{
	if (vd->vdev_ops->vdev_op_leaf) {
		dsl_scan_assess_vdev(dp, vd);
		return;
	}
	for (uint64_t c = 0; c < vd->vdev_children; c++)
		vdev_async_mirror_scan_assess_leaves(dp, vd->vdev_child[c]);
}

void
vdev_async_mirror_scan_setup_sync(dsl_scan_t *scn, dmu_tx_t *tx, spa_t *spa)
{
	if (scn->scn_phys.scn_max_txg > tx->tx_txg) {
		scn->scn_phys.scn_max_txg = tx->tx_txg;
		vdev_async_mirror_scan_assess_leaves(scn->scn_dp,
		    spa->spa_root_vdev);
	}
}
/*
 * Async mirror stats.
 */
#define	SPA_AM_KSTAT_FIELDS(X) \
	X(active) X(failed) X(applied_txg) X(synced_txg) X(dtl_txg) \
	X(dtl_child_txg) X(repair_inflight) X(pending_bytes) \
	X(pending_txgs) X(held_bytes) X(copied_bytes) X(last_lag_ms) \
	X(max_lag_ms) X(throttled_ms) X(resync_from) X(data_errors) \
	X(read_retries) X(frozen) X(frozen_txg) X(data_deferred) \
	X(data_recovered) X(copy_rest_bytes) X(copy_rest_ranges) \
	X(held_limit) X(giveups) X(giveup_reason)

#define	SPA_AM_KSTAT_TEMPLATE(field) \
	{ #field, KSTAT_DATA_UINT64 },
static kstat_named_t spa_async_mirror_kstat_template[] = {
	SPA_AM_KSTAT_FIELDS(SPA_AM_KSTAT_TEMPLATE)
};
#undef SPA_AM_KSTAT_TEMPLATE

#define	SPA_AM_KSTAT_OFFSET(field) \
	offsetof(spa_async_mirror_stats_t, sams_##field),
static const size_t spa_async_mirror_kstat_offsets[] = {
	SPA_AM_KSTAT_FIELDS(SPA_AM_KSTAT_OFFSET)
};
#undef SPA_AM_KSTAT_OFFSET

void
spa_async_mirror_stats_update(spa_t *spa)
{
	spa_history_kstat_t *shk = &spa->spa_stats.async_mirror;
	kstat_t *ksp = shk->kstat;
	spa_async_mirror_stats_t *sams = &spa->spa_async_mirror_stats;
	kstat_named_t *am_stats;

	if (ksp == NULL)
		return;

	am_stats = ksp->ks_data;
	for (uint_t i = 0;
	    i < ARRAY_SIZE(spa_async_mirror_kstat_offsets); i++) {
		uint64_t *value = (uint64_t *)((char *)sams +
		    spa_async_mirror_kstat_offsets[i]);

		atomic_store_64(&am_stats[i].value.ui64, *value);
	}
}

/*
 * The async_mirror kstat exists only while the pool has the state of an
 * asynchronous mirror: vdev_async_mirror.c creates it before it publishes
 * the state, and destroys it when it frees the state.
 */
void
spa_async_mirror_stats_init(spa_t *spa)
{
	spa_history_kstat_t *shk = &spa->spa_stats.async_mirror;

	ASSERT0P(shk->kstat);
	mutex_init(&shk->lock, NULL, MUTEX_DEFAULT, NULL);

	char *name = kmem_asprintf("zfs/%s", spa_name(spa));
	kstat_t *ksp = kstat_create(name, 0, "async_mirror", "misc",
	    KSTAT_TYPE_NAMED, ARRAY_SIZE(spa_async_mirror_kstat_template),
	    KSTAT_FLAG_VIRTUAL);

	shk->kstat = ksp;
	if (ksp) {
		ksp->ks_lock = &shk->lock;
		ksp->ks_data = kmem_alloc(
		    sizeof (spa_async_mirror_kstat_template), KM_SLEEP);
		memcpy(ksp->ks_data, &spa_async_mirror_kstat_template,
		    sizeof (spa_async_mirror_kstat_template));
		kstat_install(ksp);
	}

	kmem_strfree(name);
	spa_async_mirror_stats_update(spa);
}

void
spa_async_mirror_stats_destroy(spa_t *spa)
{
	spa_history_kstat_t *shk = &spa->spa_stats.async_mirror;
	kstat_t *ksp = shk->kstat;

	if (ksp) {
		shk->kstat = NULL;
		kmem_free(ksp->ks_data,
		    sizeof (spa_async_mirror_kstat_template));
		kstat_delete(ksp);
	}

	mutex_destroy(&shk->lock);
}
#undef SPA_AM_KSTAT_FIELDS


boolean_t
vdev_async_mirror_accept_exported_label(const vdev_t *vd, uint64_t state)
{
	return (vd->vdev_async_lagging && state == POOL_STATE_EXPORTED);
}

boolean_t
vdev_async_mirror_dtl_below_scan(uint64_t dtl_min_txg, uint64_t scan_min_txg)
{
	return (dtl_min_txg < scan_min_txg);
}

int
vdev_async_mirror_fault_check(vdev_t *vd, vdev_aux_t aux)
{
	if (aux != VDEV_AUX_EXTERNAL && aux != VDEV_AUX_EXTERNAL_PERSIST)
		return (0);
	if (vdev_async_mirror_is_child(vd))
		return (SET_ERROR(ZFS_ERR_ASYNC_MIRROR_ACTIVE));
	return (vdev_async_mirror_topology_check(vd));
}

int
vdev_async_mirror_online_restore(spa_t *spa, vdev_t *vd, vdev_t *tvd,
    boolean_t wasoffline, boolean_t was_tmpoffline)
{
	if (!wasoffline || vd->vdev_aux ||
	    vdev_async_mirror_online_check(spa, vd) == 0)
		return (0);

	vd->vdev_offline = B_TRUE;
	vd->vdev_tmpoffline = was_tmpoffline;
	vdev_reopen(tvd);
	return (SET_ERROR(ZFS_ERR_ASYNC_MIRROR_DIVERGED));
}

int
vdev_async_mirror_offline_check(vdev_t *vd)
{
	if (vdev_async_mirror_is_child(vd))
		return (SET_ERROR(ZFS_ERR_ASYNC_MIRROR_ACTIVE));
	if (!vd->vdev_offline && !vd->vdev_tmpoffline && vdev_readable(vd))
		return (vdev_async_mirror_topology_check(vd));
	return (0);
}

void
vdev_async_mirror_vdev_free(vdev_t *vd)
{
	if (vd->vdev_ops->vdev_op_leaf)
		vdev_async_mirror_failed_clear(vd->vdev_spa, vd->vdev_guid);
}

void
vdev_async_mirror_import_reset(spa_t *spa)
{
	spa->spa_async_mirror_import.ami_off = 0;
	spa->spa_async_mirror_import.ami_off_reason = NULL;
	spa->spa_async_mirror_import.ami_off_all = B_FALSE;
}

void
vdev_async_mirror_import_keep(spa_t *spa, nvlist_t *config)
{
	nvlist_t *policy;

	if (nvlist_lookup_nvlist(config, ZPOOL_LOAD_POLICY, &policy) == 0)
		(void) nvlist_lookup_uint64(policy,
		    ZPOOL_LOAD_ASYNC_MIRROR_KEEP,
		    &spa->spa_async_mirror_import.ami_keep);
}

void
vdev_async_mirror_import_done(spa_t *spa)
{
	spa->spa_async_mirror_import.ami_keep = 0;
}

void
vdev_async_mirror_load_done(spa_t *spa)
{
	spa->spa_async_mirror_unchecked = B_FALSE;
}

void
vdev_async_mirror_import_restore_offline(spa_t *spa, vdev_t *rvd)
{
	vdev_t *vd;

	spa->spa_async_mirror_import.ami_skip_offline = 0;
	if (spa->spa_async_mirror_import.ami_keep == 0 ||
	    spa->spa_async_mirror_skip == 0)
		return;
	vd = vdev_lookup_by_guid(rvd, spa->spa_async_mirror_skip);
	if (vd != NULL && vd->vdev_offline) {
		spa->spa_async_mirror_import.ami_skip_offline = vd->vdev_guid;
		vd->vdev_offline = B_FALSE;
	}
}

int
vdev_async_mirror_activate_if_enabled(spa_t *spa)
{
	if (!spa_feature_is_enabled(spa, SPA_FEATURE_ASYNC_MIRROR))
		return (0);
	return (vdev_async_mirror_activate(spa));
}

int
vdev_async_mirror_add_check(spa_t *spa, vdev_t *vd)
{
	if (!vdev_async_mirror_active(spa))
		return (0);
	for (uint64_t c = 0; c < vd->vdev_children; c++) {
		if (!vd->vdev_child[c]->vdev_islog)
			return (SET_ERROR(ZFS_ERR_ASYNC_MIRROR_ACTIVE));
	}
	return (0);
}

boolean_t
vdev_async_mirror_resilver_pending(spa_t *spa)
{
	boolean_t needed;

	spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);
	needed = vdev_async_mirror_resilver_wanted(spa->spa_root_vdev);
	spa_config_exit(spa, SCL_CONFIG, FTAG);
	return (needed);
}
