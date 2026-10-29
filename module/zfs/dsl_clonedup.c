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
 * Copyright (c) 2026, Patrick Domack. All rights reserved.
 */

/*
 * Offline deduplication through block cloning ("zpool clonedup").
 *
 * A run has three phases.  The first two are dsl_scan walks in
 * syncing context: phase one indexes the L0 data blocks born since
 * the last completed run, phase two walks the rest of the pool and
 * looks each older block up in that index.  Neither phase reads
 * data.  Phase three runs in an open-context zthr and, for every
 * group of blocks with equal checksum and properties, byte-verifies
 * and redirects the duplicates onto one surviving block with
 * dmu_brt_clone().  Nothing but the progress struct survives a run;
 * the BRT already tracks the sharing.
 *
 * The index is an AVL tree of dsl_clonedup_entry_t, about a hundred
 * bytes per new block.  It is capped by zfs_clonedup_scan_mem_max, or
 * by the same limit a scrub gives its own queues.  The cap also
 * covers the sources the apply adds later.  When an insert or a match
 * would take the index past it, the run splits the checksum space in
 * two, drops the entries outside the half it is working, and handles
 * the other halves in later passes.
 */

#include <sys/dsl_clonedup.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_scan.h>
#include <sys/dsl_dataset.h>
#include <sys/dmu_tx.h>
#include <sys/dnode.h>
#include <sys/zap.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/vdev_impl.h>
#include <sys/zio_checksum.h>
#include <sys/blkptr.h>
#include <sys/brt.h>
#include <sys/arc.h>
#include <sys/zio.h>
#include <sys/txg.h>
#include <sys/vdev.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_dir.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_traverse.h>
#include <cityhash.h>

/*
 * Bytes the index may use.  0 derives the cap the way a scrub
 * sizes its queues, with a 64 MiB floor; an explicit value is used
 * as given.
 */
static uint64_t zfs_clonedup_scan_mem_max = 0;

#define	DCL_MIN_MEM		(64ULL << 20)
#define	DCL_MAX_PARTITION_SHIFT	32

static kmem_cache_t *dsl_clonedup_entry_cache;

void
dsl_clonedup_global_init(void)
{
	dsl_clonedup_entry_cache = kmem_cache_create(
	    "dsl_clonedup_entry", sizeof (dsl_clonedup_entry_t), 0,
	    NULL, NULL, NULL, NULL, NULL, 0);
}

void
dsl_clonedup_global_fini(void)
{
	kmem_cache_destroy(dsl_clonedup_entry_cache);
}

static int
dsl_clonedup_entry_compare(const void *a, const void *b)
{
	const dsl_clonedup_entry_t *x = a;
	const dsl_clonedup_entry_t *y = b;
	int c;

	if ((c = TREE_CMP(x->dce_key, y->dce_key)) != 0)
		return (c);
	if ((c = TREE_CMP(x->dce_prop, y->dce_prop)) != 0)
		return (c);
	c = TREE_CMP(DVA_GET_VDEV(&x->dce_dva),
	    DVA_GET_VDEV(&y->dce_dva));
	if (c != 0)
		return (c);
	c = TREE_CMP(DVA_GET_OFFSET(&x->dce_dva),
	    DVA_GET_OFFSET(&y->dce_dva));
	if (c != 0)
		return (c);
	if ((c = TREE_CMP(x->dce_birth, y->dce_birth)) != 0)
		return (c);
	if ((c = TREE_CMP(x->dce_objset, y->dce_objset)) != 0)
		return (c);
	if ((c = TREE_CMP(x->dce_object, y->dce_object)) != 0)
		return (c);
	return (TREE_CMP(x->dce_blkid, y->dce_blkid));
}

/*
 * Sort order of the apply tree.  The destination dataset leads so a
 * batch stays open across consecutive candidates, because
 * dsl_clonedup_batch_flush() costs a transaction and a previous-txg
 * wait on every dataset change.  The wrapper below orders each
 * dataset's entries by source.  A walk can index one locator twice,
 * and the second visit can find a new block there, so the block's
 * DVA and birth end the order and no two entries compare equal.
 */
static int
dsl_clonedup_apply_cmp(const void *a, const void *b, boolean_t bysrc)
{
	const dsl_clonedup_entry_t *x = a;
	const dsl_clonedup_entry_t *y = b;
	int c;

	if ((c = TREE_CMP(x->dce_objset, y->dce_objset)) != 0)
		return (c);
	if (bysrc) {
		/* a batch holds one source dataset as well */
		c = TREE_CMP(x->dce_src->dcs_objset,
		    y->dce_src->dcs_objset);
		if (c != 0)
			return (c);
		c = TREE_CMP(x->dce_src->dcs_object,
		    y->dce_src->dcs_object);
		if (c != 0)
			return (c);
		c = TREE_CMP(x->dce_src->dcs_blkid,
		    y->dce_src->dcs_blkid);
		if (c != 0)
			return (c);
	}
	if ((c = TREE_CMP(x->dce_object, y->dce_object)) != 0)
		return (c);
	if ((c = TREE_CMP(x->dce_blkid, y->dce_blkid)) != 0)
		return (c);
	c = TREE_CMP(DVA_GET_VDEV(&x->dce_dva),
	    DVA_GET_VDEV(&y->dce_dva));
	if (c != 0)
		return (c);
	c = TREE_CMP(DVA_GET_OFFSET(&x->dce_dva),
	    DVA_GET_OFFSET(&y->dce_dva));
	if (c != 0)
		return (c);
	return (TREE_CMP(x->dce_birth, y->dce_birth));
}

static int
dsl_clonedup_apply_compare_src(const void *a, const void *b)
{
	return (dsl_clonedup_apply_cmp(a, b, B_TRUE));
}

/*
 * What the apply has learned about one group.  A group's destinations
 * lie far apart in the apply tree, so the apply keeps this record per
 * group, by key and prop.  dcg_counted says dclp_groups holds the
 * group.  dcg_lost is set once the apply has found the group's source
 * stale, and names the live copy that took its place.  A record and
 * its dcg_lost together take less than the apply entry whose apply
 * creates them, so they never hold more than the entries they
 * outlive held.
 */
typedef struct dsl_clonedup_lost {
	dva_t		dcx_dva;	/* the stale source */
	uint64_t	dcx_birth;
	dsl_clonedup_src_t dcx_src;	/* what replaced it */
} dsl_clonedup_lost_t;

typedef struct dsl_clonedup_gsrc {
	avl_node_t	dcg_node;
	uint64_t	dcg_key;
	uint64_t	dcg_prop;
	dsl_clonedup_lost_t *dcg_lost;	/* or NULL */
	boolean_t	dcg_counted;
} dsl_clonedup_gsrc_t;

static int
dsl_clonedup_gsrc_compare(const void *a, const void *b)
{
	const dsl_clonedup_gsrc_t *x = a;
	const dsl_clonedup_gsrc_t *y = b;
	int c;

	if ((c = TREE_CMP(x->dcg_key, y->dcg_key)) != 0)
		return (c);
	return (TREE_CMP(x->dcg_prop, y->dcg_prop));
}

/*
 * Load the saved progress struct.  A missing entry is a pool that
 * never ran clonedup.  Any other problem is logged and treated the
 * same way, so a damaged entry can never keep a pool from importing.
 */
static void
dsl_clonedup_load_phys(dsl_pool_t *dp, dsl_clonedup_phys_t *phys)
{
	objset_t *mos = dp->dp_meta_objset;
	uint64_t intsize;
	uint64_t num;
	uint64_t *buf;
	int err;

	memset(phys, 0, sizeof (*phys));
	err = zap_length(mos, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_CLONEDUP_SCAN, &intsize, &num);
	if (err == ENOENT)
		return;
	if (err == 0 && (intsize != sizeof (uint64_t) || num == 0))
		err = SET_ERROR(EINVAL);
	if (err != 0)
		goto bad;

	buf = kmem_zalloc(num * sizeof (uint64_t), KM_SLEEP);
	err = zap_lookup(mos, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_CLONEDUP_SCAN, sizeof (uint64_t), num, buf);
	if (err == 0) {
		memcpy(phys, buf,
		    MIN(num, DSL_CLONEDUP_PHYS_NUMINTS) *
		    sizeof (uint64_t));
	}
	kmem_free(buf, num * sizeof (uint64_t));
	if (err != 0)
		goto bad;

	if (phys->dclp_version != DSL_CLONEDUP_PHYS_VERSION) {
		zfs_dbgmsg("clonedup: %s has phys version %llu, "
		    "ignoring saved state", spa_name(dp->dp_spa),
		    (u_longlong_t)phys->dclp_version);
		memset(phys, 0, sizeof (*phys));
		return;
	}
	if (phys->dclp_partition_shift > DCL_MAX_PARTITION_SHIFT ||
	    phys->dclp_partition >=
	    1ULL << phys->dclp_partition_shift ||
	    phys->dclp_phase > POOL_CLONEDUP_APPLY) {
		err = SET_ERROR(EINVAL);
		goto bad;
	}
	return;

bad:
	zfs_dbgmsg("clonedup: %s: cannot read saved state, error %d",
	    spa_name(dp->dp_spa), err);
	memset(phys, 0, sizeof (*phys));
}

static const char *const dsl_clonedup_kstat_names[DCK_NUM] = {
	"verify_reads", "verify_bytes", "clones",
	"yields", "yield_waits", "index_walks", "match_walks",
	"dst_mounted", "dst_zvol", "dst_owned", "batches",
	"key_collisions", "cksum_collisions",
	"mem_peak", "canceled_runs", "walk_resumes",
};

#define	DCL_BUMP(dcl, id, n)	wmsum_add(&(dcl)->dcl_wsums[id], (n))

static int
dsl_clonedup_kstat_update(kstat_t *ksp, int rw)
{
	dsl_clonedup_t *dcl = ksp->ks_private;

	if (rw == KSTAT_WRITE)
		return (SET_ERROR(EACCES));
	for (int i = 0; i < DCK_NUM; i++) {
		dcl->dcl_kstat[i].value.ui64 =
		    wmsum_value(&dcl->dcl_wsums[i]);
	}
	return (0);
}

static void
dsl_clonedup_kstat_create(dsl_clonedup_t *dcl)
{
	char *name;
	kstat_t *ksp;

	for (int i = 0; i < DCK_NUM; i++) {
		wmsum_init(&dcl->dcl_wsums[i], 0);
		(void) strlcpy(dcl->dcl_kstat[i].name,
		    dsl_clonedup_kstat_names[i], KSTAT_STRLEN);
		dcl->dcl_kstat[i].data_type = KSTAT_DATA_UINT64;
	}
	name = kmem_asprintf("zfs/%s", spa_name(dcl->dcl_dp->dp_spa));
	ksp = kstat_create(name, 0, "clonedup", "misc",
	    KSTAT_TYPE_NAMED, DCK_NUM, KSTAT_FLAG_VIRTUAL);
	if (ksp != NULL) {
		ksp->ks_data = dcl->dcl_kstat;
		ksp->ks_private = dcl;
		ksp->ks_update = dsl_clonedup_kstat_update;
		kstat_install(ksp);
	}
	dcl->dcl_ksp = ksp;
	kmem_strfree(name);
}

static void
dsl_clonedup_kstat_destroy(dsl_clonedup_t *dcl)
{
	if (dcl->dcl_ksp != NULL)
		kstat_delete(dcl->dcl_ksp);
	dcl->dcl_ksp = NULL;
	for (int i = 0; i < DCK_NUM; i++)
		wmsum_fini(&dcl->dcl_wsums[i]);
}

/*
 * What a dry run has taken, so that it takes each thing once, as a
 * real run does.  A DCD_DIR entry holds the growth that
 * snapshot-held redirects have brought a dsl_dir.  A real run's
 * growth reaches the disk, where its next quota check reads it.  A
 * dry run's stays here until the next run starts.
 */
#define	DCD_DIR		2	/* dd_object */

typedef struct dsl_clonedup_dry {
	avl_node_t	dcd_node;
	uint64_t	dcd_key[4];	/* kind, then ids */
	uint64_t	dcd_grow;	/* DCD_DIR: bytes booked */
} dsl_clonedup_dry_t;

static int
dsl_clonedup_dry_compare(const void *a, const void *b)
{
	const dsl_clonedup_dry_t *x = a;
	const dsl_clonedup_dry_t *y = b;
	int c = 0;

	for (int i = 0; i < 4 && c == 0; i++)
		c = TREE_CMP(x->dcd_key[i], y->dcd_key[i]);
	return (c);
}

static void
dsl_clonedup_dry_key(dsl_clonedup_dry_t *d, uint64_t kind, uint64_t a,
    uint64_t b, uint64_t c)
{
	d->dcd_key[0] = kind;
	d->dcd_key[1] = a;
	d->dcd_key[2] = b;
	d->dcd_key[3] = c;
}

static void
dsl_clonedup_dry_clear(dsl_clonedup_t *dcl)
{
	dsl_clonedup_dry_t *d;

	while ((d = avl_first(&dcl->dcl_dry)) != NULL) {
		avl_remove(&dcl->dcl_dry, d);
		kmem_free(d, sizeof (*d));
	}
}

/*
 * The walk's answers from dsl_clonedup_unchanged(), one per dsl_dir,
 * for the txg in dcl_unch_txg.
 */
typedef struct dsl_clonedup_unch {
	avl_node_t	dcn_node;
	uint64_t	dcn_dir;	/* dsl_dir object */
	boolean_t	dcn_unchanged;
} dsl_clonedup_unch_t;

static int
dsl_clonedup_unch_compare(const void *a, const void *b)
{
	const dsl_clonedup_unch_t *x = a;
	const dsl_clonedup_unch_t *y = b;

	return (TREE_CMP(x->dcn_dir, y->dcn_dir));
}

static void
dsl_clonedup_unch_vacate(dsl_clonedup_t *dcl)
{
	dsl_clonedup_unch_t *u;
	void *cookie = NULL;

	while ((u = avl_destroy_nodes(&dcl->dcl_unch,
	    &cookie)) != NULL)
		kmem_free(u, sizeof (*u));
}

int
dsl_clonedup_init(dsl_pool_t *dp)
{
	dsl_clonedup_t *dcl;

	ASSERT0P(dp->dp_clonedup);
	dcl = kmem_zalloc(sizeof (*dcl), KM_SLEEP);
	dcl->dcl_dp = dp;
	mutex_init(&dcl->dcl_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&dcl->dcl_yield_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&dcl->dcl_yield_cv, NULL, CV_DEFAULT, NULL);
	cv_init(&dcl->dcl_apply_cv, NULL, CV_DEFAULT, NULL);
	avl_create(&dcl->dcl_dry, dsl_clonedup_dry_compare,
	    sizeof (dsl_clonedup_dry_t),
	    offsetof(dsl_clonedup_dry_t, dcd_node));
	avl_create(&dcl->dcl_unch, dsl_clonedup_unch_compare,
	    sizeof (dsl_clonedup_unch_t),
	    offsetof(dsl_clonedup_unch_t, dcn_node));
	dsl_clonedup_load_phys(dp, &dcl->dcl_phys);
	dcl->dcl_phys_written = dcl->dcl_phys;
	dsl_clonedup_kstat_create(dcl);
	dp->dp_clonedup = dcl;
	return (0);
}

static void dsl_clonedup_index_destroy(dsl_clonedup_t *dcl);

void
dsl_clonedup_fini(dsl_pool_t *dp)
{
	dsl_clonedup_t *dcl = dp->dp_clonedup;

	if (dcl == NULL)
		return;
	dsl_clonedup_index_destroy(dcl);
	dsl_clonedup_dry_clear(dcl);
	avl_destroy(&dcl->dcl_dry);
	dsl_clonedup_unch_vacate(dcl);
	avl_destroy(&dcl->dcl_unch);
	dsl_clonedup_kstat_destroy(dcl);
	cv_destroy(&dcl->dcl_apply_cv);
	cv_destroy(&dcl->dcl_yield_cv);
	mutex_destroy(&dcl->dcl_yield_lock);
	mutex_destroy(&dcl->dcl_lock);
	kmem_free(dcl, sizeof (*dcl));
	dp->dp_clonedup = NULL;
}

uint64_t
dsl_clonedup_last_txg(dsl_pool_t *dp)
{
	dsl_clonedup_t *dcl = dp->dp_clonedup;

	if (dcl == NULL)
		return (0);
	return (dcl->dcl_phys.dclp_last_txg);
}

/* A scan sync goes on with a walk where an earlier one stopped. */
void
dsl_clonedup_walk_resumed(dsl_pool_t *dp)
{
	DCL_BUMP(dp->dp_clonedup, DCK_WALK_RESUMES, 1);
}

/*
 * A restart after an export or a crash begins the current partition
 * again and keeps nothing finer than these fields, so nothing finer
 * is worth a write.  The counters reach the disk with the state
 * change that ends the run.
 */
static boolean_t
dsl_clonedup_phys_moved(const dsl_clonedup_phys_t *a,
    const dsl_clonedup_phys_t *b)
{
	return (a->dclp_state != b->dclp_state ||
	    a->dclp_flags != b->dclp_flags ||
	    a->dclp_phase != b->dclp_phase ||
	    a->dclp_partition_shift != b->dclp_partition_shift ||
	    a->dclp_partition != b->dclp_partition ||
	    a->dclp_min_txg != b->dclp_min_txg ||
	    a->dclp_max_txg != b->dclp_max_txg ||
	    a->dclp_last_txg != b->dclp_last_txg);
}

void
dsl_clonedup_sync_state(dsl_clonedup_t *dcl, dmu_tx_t *tx)
{
	dsl_pool_t *dp = dcl->dcl_dp;
	dsl_clonedup_phys_t *phys;
	boolean_t moved;

	ASSERT(dmu_tx_is_syncing(tx));
	/* most txgs of a run move nothing worth a write */
	mutex_enter(&dcl->dcl_lock);
	moved = !(dcl->dcl_phys.dclp_flags & DSF_CLONEDUP_DRYRUN) &&
	    dsl_clonedup_phys_moved(&dcl->dcl_phys,
	    &dcl->dcl_phys_written);
	mutex_exit(&dcl->dcl_lock);
	if (!moved)
		return;
	phys = kmem_alloc(sizeof (*phys), KM_SLEEP);
	mutex_enter(&dcl->dcl_lock);
	dcl->dcl_phys.dclp_version = DSL_CLONEDUP_PHYS_VERSION;
	*phys = dcl->dcl_phys;
	mutex_exit(&dcl->dcl_lock);
	VERIFY0(zap_update(dp->dp_meta_objset,
	    DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_CLONEDUP_SCAN,
	    sizeof (uint64_t), DSL_CLONEDUP_PHYS_NUMINTS,
	    phys, tx));
	dcl->dcl_phys_written = *phys;
	kmem_free(phys, sizeof (*phys));
}

/*
 * Index memory management.
 */
static uint64_t
dsl_clonedup_mem_max(dsl_clonedup_t *dcl)
{
	uint64_t max = zfs_clonedup_scan_mem_max;

	if (max == 0) {
		max = dsl_scan_mem_lim(dcl->dcl_dp->dp_spa);
		max = MAX(max, DCL_MIN_MEM);
	}
	return (max);
}

static void
dsl_clonedup_index_create(dsl_clonedup_t *dcl)
{
	ASSERT(MUTEX_HELD(&dcl->dcl_lock));
	ASSERT(!dcl->dcl_index_active);
	avl_create(&dcl->dcl_index, dsl_clonedup_entry_compare,
	    sizeof (dsl_clonedup_entry_t),
	    offsetof(dsl_clonedup_entry_t, dce_node));
	avl_create(&dcl->dcl_apply, dsl_clonedup_apply_compare_src,
	    sizeof (dsl_clonedup_entry_t),
	    offsetof(dsl_clonedup_entry_t, dce_node));
	avl_create(&dcl->dcl_gsrc, dsl_clonedup_gsrc_compare,
	    sizeof (dsl_clonedup_gsrc_t),
	    offsetof(dsl_clonedup_gsrc_t, dcg_node));
	dcl->dcl_apply_ready = B_FALSE;
	dcl->dcl_take_birth = 0;
	dcl->dcl_kept_birth = 0;
	dcl->dcl_apply_shared = 0;
	dcl->dcl_apply_total = 0;
	dcl->dcl_nentries = 0;
	dcl->dcl_nsrc = 0;
	dcl->dcl_nrepeat = 0;
	dcl->dcl_mem_used = 0;
	dcl->dcl_mem_max = dsl_clonedup_mem_max(dcl);
	dcl->dcl_index_active = B_TRUE;
}

static void
dsl_clonedup_entry_free(dsl_clonedup_t *dcl, dsl_clonedup_entry_t *e)
{
	if (e->dce_src != NULL) {
		kmem_free(e->dce_src, sizeof (*e->dce_src));
		dcl->dcl_nsrc--;
		dcl->dcl_mem_used -= sizeof (dsl_clonedup_src_t);
	}
	kmem_cache_free(dsl_clonedup_entry_cache, e);
	dcl->dcl_nentries--;
	dcl->dcl_mem_used -= sizeof (*e);
}

static void
dsl_clonedup_index_destroy(dsl_clonedup_t *dcl)
{
	dsl_clonedup_entry_t *e;
	dsl_clonedup_gsrc_t *gs;
	void *cookie = NULL;

	if (!dcl->dcl_index_active)
		return;
	while ((e = avl_destroy_nodes(&dcl->dcl_index,
	    &cookie)) != NULL)
		dsl_clonedup_entry_free(dcl, e);
	avl_destroy(&dcl->dcl_index);
	cookie = NULL;
	while ((e = avl_destroy_nodes(&dcl->dcl_apply,
	    &cookie)) != NULL)
		dsl_clonedup_entry_free(dcl, e);
	avl_destroy(&dcl->dcl_apply);
	cookie = NULL;
	while ((gs = avl_destroy_nodes(&dcl->dcl_gsrc,
	    &cookie)) != NULL) {
		if (gs->dcg_lost != NULL) {
			dcl->dcl_mem_used -= sizeof (*gs->dcg_lost);
			kmem_free(gs->dcg_lost,
			    sizeof (*gs->dcg_lost));
		}
		dcl->dcl_mem_used -= sizeof (*gs);
		kmem_free(gs, sizeof (*gs));
	}
	avl_destroy(&dcl->dcl_gsrc);
	dcl->dcl_apply_ready = B_FALSE;
	ASSERT0(dcl->dcl_nentries);
	ASSERT0(dcl->dcl_nsrc);
	dcl->dcl_index_active = B_FALSE;
	atomic_inc_64(&dcl->dcl_gen);
}

static inline uint64_t
dsl_clonedup_partition_of(uint64_t key, uint64_t shift)
{
	return (shift == 0 ? 0 : key >> (64 - shift));
}

static boolean_t
dsl_clonedup_same_group(const dsl_clonedup_entry_t *a,
    const dsl_clonedup_entry_t *b)
{
	return (a->dce_key == b->dce_key &&
	    a->dce_prop == b->dce_prop);
}

/* Does an entry inserted at where join a group in the index? */
static boolean_t
dsl_clonedup_index_repeat(dsl_clonedup_t *dcl,
    const dsl_clonedup_entry_t *probe, avl_index_t where)
{
	dsl_clonedup_entry_t *n;

	n = avl_nearest(&dcl->dcl_index, where, AVL_BEFORE);
	if (n != NULL && dsl_clonedup_same_group(n, probe))
		return (B_TRUE);
	n = avl_nearest(&dcl->dcl_index, where, AVL_AFTER);
	return (n != NULL && dsl_clonedup_same_group(n, probe));
}

/*
 * Bytes n index entries need by the end of the order pass, r of them
 * after the first of their group.  The pass gives each copy it moves
 * into the apply tree a source of its own, and frees a group's phase
 * two source and every copy it does not move.  So a group of k
 * entries grows by k - 1 sources at most.
 */
static uint64_t
dsl_clonedup_mem_need(uint64_t n, uint64_t r)
{
	return (n * sizeof (dsl_clonedup_entry_t) +
	    r * sizeof (dsl_clonedup_src_t));
}

/*
 * Whether add more bytes would take the index past its cap, counting
 * the sources the order pass adds later.
 */
static boolean_t
dsl_clonedup_mem_over(dsl_clonedup_t *dcl, uint64_t add)
{
	ASSERT(MUTEX_HELD(&dcl->dcl_lock));
	return (dcl->dcl_mem_used + add +
	    dsl_clonedup_mem_need(0, dcl->dcl_nrepeat) >
	    dcl->dcl_mem_max);
}

/* Raise DCK_MEM_PEAK to what the index holds now. */
static void
dsl_clonedup_mem_note(dsl_clonedup_t *dcl)
{
	uint64_t now = dcl->dcl_mem_used;

	ASSERT(MUTEX_HELD(&dcl->dcl_lock));
	if (now > dcl->dcl_mem_peak) {
		DCL_BUMP(dcl, DCK_MEM_PEAK, now - dcl->dcl_mem_peak);
		dcl->dcl_mem_peak = now;
	}
}

/*
 * Double the partition count and drop every entry that falls outside
 * the half of the current partition being kept.  The other half is
 * revisited by a later pass of this run.
 */
static void
dsl_clonedup_split(dsl_clonedup_t *dcl)
{
	dsl_clonedup_phys_t *p = &dcl->dcl_phys;
	dsl_clonedup_entry_t *e;
	dsl_clonedup_entry_t *next;
	dsl_clonedup_entry_t *last = NULL;
	uint64_t before = dcl->dcl_nentries;

	ASSERT(MUTEX_HELD(&dcl->dcl_lock));
	if (p->dclp_partition_shift >= DCL_MAX_PARTITION_SHIFT)
		return;
	p->dclp_partition_shift++;
	p->dclp_partition <<= 1;
	dcl->dcl_nrepeat = 0;
	for (e = avl_first(&dcl->dcl_index); e != NULL; e = next) {
		next = AVL_NEXT(&dcl->dcl_index, e);
		if (dsl_clonedup_partition_of(e->dce_key,
		    p->dclp_partition_shift) != p->dclp_partition) {
			avl_remove(&dcl->dcl_index, e);
			dsl_clonedup_entry_free(dcl, e);
			p->dclp_blocks_indexed--;
			continue;
		}
		if (last != NULL && dsl_clonedup_same_group(last, e))
			dcl->dcl_nrepeat++;
		last = e;
	}
	dcl->dcl_splits++;
	zfs_dbgmsg("clonedup: %s: index over %llu bytes, split into "
	    "%llu partitions, kept %llu of %llu entries",
	    spa_name(dcl->dcl_dp->dp_spa),
	    (u_longlong_t)dcl->dcl_mem_max,
	    (u_longlong_t)(1ULL << p->dclp_partition_shift),
	    (u_longlong_t)dcl->dcl_nentries, (u_longlong_t)before);
}

/*
 * Block keys.
 */
static uint64_t
dsl_clonedup_bp_key(const blkptr_t *bp)
{
	const zio_cksum_t *c = &bp->blk_cksum;

	return (cityhash4(c->zc_word[0], c->zc_word[1], c->zc_word[2],
	    c->zc_word[3]));
}

static uint64_t
dsl_clonedup_bp_prop(const blkptr_t *bp)
{
	uint64_t p = bp->blk_prop & DCE_PROP_BP_MASK;

	/*
	 * A clone installs the source pointer whole, so copies with
	 * unequal DVA counts never pair.  Grouping them apart lets
	 * the copies with each count pair among themselves.
	 */
	DCE_PROP_SET_NDVAS(p, BP_GET_NDVAS(bp));
	return (p);
}

/*
 * Whether a block pointer can take part in a run at all.  The dataset
 * level rules (snapshots, the clonedup property) are the caller's.
 * A block whose DVA[0] is on a special-class vdev also gets
 * DCE_F_SPECIAL in *flagsp, and one whose DVA[0] is on a removing or
 * indirect vdev gets DCE_F_REMOVED.  Such a block can be redirected
 * but never survives.  spa_remap_blkptr() never remaps a cloned
 * block pointer, so a survivor there would keep its indirect mapping
 * for good.  The caller holds a config lock, which vdev_lookup_top()
 * needs.
 *
 * A gang block takes part like any other.  Its checksum and sizes
 * are those of its whole data, the asize of each DVA counts the gang
 * tree, and reads, clones and frees go through the tree.  Its DVA[0]
 * names the gang header, so the vdev tests below look at the header.
 */
boolean_t
dsl_clonedup_bp_eligible(spa_t *spa, const blkptr_t *bp,
    const dnode_phys_t *dnp, const zbookmark_phys_t *zb,
    uint8_t *flagsp)
{
	enum zio_checksum ck;
	vdev_t *vd;

	if (zb->zb_level != 0 || dnp == NULL)
		return (B_FALSE);
	if (zb->zb_blkid == DMU_SPILL_BLKID ||
	    zb->zb_blkid == DMU_BONUS_BLKID)
		return (B_FALSE);
	if (dnp->dn_type != DMU_OT_PLAIN_FILE_CONTENTS &&
	    dnp->dn_type != DMU_OT_ZVOL)
		return (B_FALSE);
	if (BP_IS_HOLE(bp) || BP_IS_EMBEDDED(bp) ||
	    BP_IS_REDACTED(bp) || BP_GET_DEDUP(bp) ||
	    BP_IS_PROTECTED(bp) || BP_IS_METADATA(bp) ||
	    BP_GET_NDVAS(bp) == 0)
		return (B_FALSE);

	ck = BP_GET_CHECKSUM(bp);
	if (ck >= ZIO_CHECKSUM_FUNCTIONS || ck == ZIO_CHECKSUM_OFF ||
	    ck == ZIO_CHECKSUM_NOPARITY)
		return (B_FALSE);

	ASSERT(spa_config_held(spa, SCL_ALL, RW_READER) != 0);
	vd = vdev_lookup_top(spa, DVA_GET_VDEV(&bp->blk_dva[0]));
	if (vd == NULL)
		return (B_FALSE);
	if (vd->vdev_ops == &vdev_indirect_ops || vd->vdev_removing)
		*flagsp |= DCE_F_REMOVED;
	if (vd->vdev_alloc_bias == VDEV_BIAS_SPECIAL)
		*flagsp |= DCE_F_SPECIAL;
	return (B_TRUE);
}

static void
dsl_clonedup_entry_fill(dsl_clonedup_entry_t *e, const blkptr_t *bp,
    const zbookmark_phys_t *zb, const dnode_phys_t *dnp,
    uint8_t flags)
{
	memset(e, 0, sizeof (*e));
	e->dce_key = dsl_clonedup_bp_key(bp);
	e->dce_prop = dsl_clonedup_bp_prop(bp);
	e->dce_dva = bp->blk_dva[0];
	e->dce_birth = BP_GET_PHYSICAL_BIRTH(bp);
	e->dce_objset = zb->zb_objset;
	e->dce_object = zb->zb_object;
	e->dce_blkid = zb->zb_blkid;
	e->dce_blkszsec = dnp->dn_datablkszsec;
	e->dce_dntype = dnp->dn_type;
	e->dce_flags = flags;
	if (dnp->dn_type == DMU_OT_ZVOL)
		e->dce_flags |= DCE_F_ZVOL;
}

static void
dsl_clonedup_src_fill(dsl_clonedup_src_t *s, const blkptr_t *bp,
    const zbookmark_phys_t *zb, const dnode_phys_t *dnp,
    uint8_t flags)
{
	memset(s, 0, sizeof (*s));
	s->dcs_dva = bp->blk_dva[0];
	s->dcs_birth = BP_GET_PHYSICAL_BIRTH(bp);
	s->dcs_objset = zb->zb_objset;
	s->dcs_object = zb->zb_object;
	s->dcs_blkid = zb->zb_blkid;
	s->dcs_blkszsec = dnp->dn_datablkszsec;
	s->dcs_dntype = dnp->dn_type;
	s->dcs_flags = flags;
	if (dnp->dn_type == DMU_OT_ZVOL)
		s->dcs_flags |= DCE_F_ZVOL;
}

/*
 * Phase one: remember a block born inside the run's window.  A block
 * seen twice is found by its full locator and ignored.  The walk can
 * see a block twice when a snapshot it is inside is destroyed.  It
 * finishes the snapshot's range in the next dataset, then walks that
 * dataset again.  A block cloned in between is new at a locator the
 * first pass indexed, and gets an entry of its own.
 */
static void
dsl_clonedup_insert(dsl_clonedup_t *dcl, const blkptr_t *bp,
    const zbookmark_phys_t *zb, const dnode_phys_t *dnp,
    uint8_t flags)
{
	dsl_clonedup_phys_t *p = &dcl->dcl_phys;
	dsl_clonedup_entry_t probe;
	dsl_clonedup_entry_t *e;
	avl_index_t where;
	boolean_t repeat;

	dsl_clonedup_entry_fill(&probe, bp, zb, dnp, flags);
	if (dsl_clonedup_partition_of(probe.dce_key,
	    p->dclp_partition_shift) != p->dclp_partition)
		return;
	if (avl_find(&dcl->dcl_index, &probe, &where) != NULL)
		return;
	/*
	 * Split before the entry goes in, so the index never holds
	 * more than its cap.  A split drops whole groups, so it
	 * cannot change whether the probe joins one.
	 */
	repeat = dsl_clonedup_index_repeat(dcl, &probe, where);
	while (dsl_clonedup_mem_over(dcl,
	    dsl_clonedup_mem_need(1, repeat)) &&
	    p->dclp_partition_shift < DCL_MAX_PARTITION_SHIFT) {
		dsl_clonedup_split(dcl);
		if (dsl_clonedup_partition_of(probe.dce_key,
		    p->dclp_partition_shift) != p->dclp_partition)
			return;
		(void) avl_find(&dcl->dcl_index, &probe, &where);
	}

	e = kmem_cache_alloc(dsl_clonedup_entry_cache, KM_SLEEP);
	*e = probe;
	avl_insert(&dcl->dcl_index, e, where);
	dcl->dcl_nentries++;
	if (repeat)
		dcl->dcl_nrepeat++;
	dcl->dcl_mem_used += sizeof (*e);
	dsl_clonedup_mem_note(dcl);
	p->dclp_blocks_indexed++;
}

static boolean_t dsl_clonedup_src_better(
    const dsl_clonedup_src_t *a, const dsl_clonedup_src_t *b);

/*
 * Phase two: an older block with the same key and properties as a
 * group is recorded as that group's source.  Candidates are ranked
 * the way the apply ranks them, so which one wins does not depend on
 * the order the walk reaches them in.  A block on a removing or
 * indirect vdev is never recorded, since it serves no group.
 */
static void
dsl_clonedup_match(dsl_clonedup_t *dcl, const blkptr_t *bp,
    const zbookmark_phys_t *zb, const dnode_phys_t *dnp,
    uint8_t flags)
{
	dsl_clonedup_phys_t *p = &dcl->dcl_phys;
	dsl_clonedup_entry_t probe;
	dsl_clonedup_entry_t *e;
	dsl_clonedup_src_t cand;
	avl_index_t where;

	if (flags & DCE_F_REMOVED)
		return;
	memset(&probe, 0, sizeof (probe));
	probe.dce_key = dsl_clonedup_bp_key(bp);
	probe.dce_prop = dsl_clonedup_bp_prop(bp);
	if (dsl_clonedup_partition_of(probe.dce_key,
	    p->dclp_partition_shift) != p->dclp_partition)
		return;

	e = avl_find(&dcl->dcl_index, &probe, &where);
	if (e == NULL)
		e = avl_nearest(&dcl->dcl_index, where, AVL_AFTER);
	if (e == NULL || e->dce_key != probe.dce_key ||
	    e->dce_prop != probe.dce_prop)
		return;

	dsl_clonedup_src_fill(&cand, bp, zb, dnp, flags);
	if (e->dce_src != NULL) {
		if (dsl_clonedup_src_better(&cand, e->dce_src))
			*e->dce_src = cand;
		return;
	}
	/*
	 * Split first, as dsl_clonedup_insert() does.  e shares the
	 * probe's key, so e survives any split the probe survives.
	 */
	while (dsl_clonedup_mem_over(dcl, sizeof (cand)) &&
	    p->dclp_partition_shift < DCL_MAX_PARTITION_SHIFT) {
		dsl_clonedup_split(dcl);
		if (dsl_clonedup_partition_of(probe.dce_key,
		    p->dclp_partition_shift) != p->dclp_partition)
			return;
	}
	e->dce_src = kmem_zalloc(sizeof (*e->dce_src), KM_SLEEP);
	*e->dce_src = cand;
	dcl->dcl_nsrc++;
	dcl->dcl_mem_used += sizeof (dsl_clonedup_src_t);
	dsl_clonedup_mem_note(dcl);
}

/*
 * Whether the scan's walk can use a block, judged from its birth and
 * key alone.  The scan callback asks before it looks the block up in
 * the BRT and takes dcl_lock.  Only the walk's own thread changes the
 * fields read here, so no lock is needed.
 */
boolean_t
dsl_clonedup_wanted(dsl_clonedup_t *dcl, const blkptr_t *bp)
{
	dsl_clonedup_phys_t *p = &dcl->dcl_phys;
	uint64_t birth = BP_GET_PHYSICAL_BIRTH(bp);

	switch (p->dclp_phase) {
	case POOL_CLONEDUP_INDEX:
		if (birth <= p->dclp_min_txg)
			return (B_FALSE);
		break;
	case POOL_CLONEDUP_MATCH:
		if (birth > p->dclp_min_txg)
			return (B_FALSE);
		break;
	default:
		return (B_FALSE);
	}
	return (dcl->dcl_index_active &&
	    dsl_clonedup_partition_of(dsl_clonedup_bp_key(bp),
	    p->dclp_partition_shift) == p->dclp_partition);
}

void
dsl_clonedup_visit(dsl_clonedup_t *dcl, const blkptr_t *bp,
    const zbookmark_phys_t *zb, const dnode_phys_t *dnp,
    uint8_t flags)
{
	dsl_clonedup_phys_t *p = &dcl->dcl_phys;
	uint64_t birth = BP_GET_PHYSICAL_BIRTH(bp);

	mutex_enter(&dcl->dcl_lock);
	p->dclp_blocks_examined++;
	if (dcl->dcl_index_active) {
		switch (p->dclp_phase) {
		case POOL_CLONEDUP_INDEX:
			if (birth > p->dclp_min_txg)
				dsl_clonedup_insert(dcl, bp, zb, dnp,
				    flags);
			break;
		case POOL_CLONEDUP_MATCH:
			if (birth <= p->dclp_min_txg)
				dsl_clonedup_match(dcl, bp, zb, dnp,
				    flags);
			break;
		default:
			break;
		}
	}
	mutex_exit(&dcl->dcl_lock);
}

/*
 * Run and phase control, driven from dsl_scan_sync().
 */
/*
 * Start a run.  A restart after import keeps the partition being
 * worked and the start time.  Every run keeps dclp_last_txg and
 * dclp_scrub_end; everything else starts over.
 */
void
dsl_clonedup_run_setup(dsl_clonedup_t *dcl, uint64_t flags,
    uint64_t min_txg, uint64_t max_txg, boolean_t restart,
    dmu_tx_t *tx)
{
	dsl_clonedup_phys_t *p = &dcl->dcl_phys;
	dsl_clonedup_phys_t *saved;

	saved = kmem_alloc(sizeof (*saved), KM_SLEEP);
	mutex_enter(&dcl->dcl_lock);
	*saved = *p;
	dsl_clonedup_index_destroy(dcl);
	dsl_clonedup_dry_clear(dcl);
	dcl->dcl_dbg_left = 16;
	dcl->dcl_apply_total = 0;
	memset(p, 0, sizeof (*p));
	p->dclp_version = DSL_CLONEDUP_PHYS_VERSION;
	p->dclp_last_txg = saved->dclp_last_txg;
	p->dclp_scrub_end = saved->dclp_scrub_end;
	if (restart) {
		p->dclp_partition_shift = saved->dclp_partition_shift;
		p->dclp_partition = saved->dclp_partition;
		p->dclp_start_time = saved->dclp_start_time;
	}
	p->dclp_state = DSS_SCANNING;
	p->dclp_flags = flags;
	p->dclp_phase = POOL_CLONEDUP_INDEX;
	DCL_BUMP(dcl, DCK_INDEX_WALKS, 1);
	p->dclp_min_txg = min_txg;
	/*
	 * The finished partitions were walked up to the old bound,
	 * so it stays the bound the next run opens at.  The walks
	 * after a restart use the scan's own, later one.
	 */
	p->dclp_max_txg = restart ? saved->dclp_max_txg : max_txg;
	if (!restart)
		p->dclp_start_time = gethrestime_sec();
	dsl_clonedup_index_create(dcl);
	mutex_exit(&dcl->dcl_lock);
	kmem_free(saved, sizeof (*saved));
	dsl_clonedup_sync_state(dcl, tx);
}

/*
 * End a run.  restart is set for the restart after an import, which
 * ends the run only to start it again at once.
 */
void
dsl_clonedup_run_done(dsl_clonedup_t *dcl, boolean_t complete,
    boolean_t restart, dmu_tx_t *tx)
{
	dsl_clonedup_phys_t *p = &dcl->dcl_phys;
	spa_t *spa = dcl->dcl_dp->dp_spa;

	mutex_enter(&dcl->dcl_lock);
	p->dclp_state = complete ? DSS_FINISHED : DSS_CANCELED;
	if (!complete && !restart)
		DCL_BUMP(dcl, DCK_CANCELED_RUNS, 1);
	p->dclp_end_time = gethrestime_sec();
	p->dclp_phase = POOL_CLONEDUP_NONE;
	/*
	 * A quick run never looked at older data, so the window stays
	 * open for the next default run to match these blocks.
	 */
	if (complete && !(p->dclp_flags &
	    (DSF_CLONEDUP_DRYRUN | DSF_CLONEDUP_QUICK))) {
		p->dclp_last_txg = p->dclp_max_txg;
		spa->spa_clonedup_last_txg = p->dclp_last_txg;
	}
	dsl_clonedup_index_destroy(dcl);
	mutex_exit(&dcl->dcl_lock);
	dsl_clonedup_sync_state(dcl, tx);
}

/*
 * zpool status recommends a scrub after a sequential rebuild only
 * when the scan record holds a scrub that finished before the
 * rebuild ended, or holds nothing.  A clonedup run takes over the
 * record.  So a run that takes over another scan's record notes what
 * zpool status would have read from it.  That is the end time of a
 * finished scrub, 0 for an empty record, and UINT64_MAX for any other
 * record, which never calls for a scrub.  Every later run keeps the
 * value.
 */
void
dsl_clonedup_note_scrub(dsl_clonedup_t *dcl,
    const dsl_scan_phys_t *sp)
{
	uint64_t end = UINT64_MAX;

	if (sp->scn_state == DSS_NONE)
		end = 0;
	else if (sp->scn_state == DSS_FINISHED &&
	    sp->scn_func == POOL_SCAN_SCRUB)
		end = sp->scn_end_time;
	mutex_enter(&dcl->dcl_lock);
	dcl->dcl_phys.dclp_scrub_end = end;
	mutex_exit(&dcl->dcl_lock);
}

static dsl_clonedup_next_t
dsl_clonedup_partition_done_locked(dsl_clonedup_t *dcl,
    uint64_t *min_txgp)
{
	dsl_clonedup_phys_t *p = &dcl->dcl_phys;

	ASSERT(MUTEX_HELD(&dcl->dcl_lock));
	dsl_clonedup_index_destroy(dcl);
	if (p->dclp_partition + 1 <
	    (1ULL << p->dclp_partition_shift)) {
		p->dclp_partition++;
		p->dclp_phase = POOL_CLONEDUP_INDEX;
		DCL_BUMP(dcl, DCK_INDEX_WALKS, 1);
		dsl_clonedup_index_create(dcl);
		*min_txgp = p->dclp_min_txg;
		return (DCLN_WALK);
	}
	return (DCLN_FINISH);
}

dsl_clonedup_next_t
dsl_clonedup_partition_done(dsl_clonedup_t *dcl, uint64_t *min_txgp,
    dmu_tx_t *tx)
{
	dsl_clonedup_next_t next;

	mutex_enter(&dcl->dcl_lock);
	next = dsl_clonedup_partition_done_locked(dcl, min_txgp);
	mutex_exit(&dcl->dcl_lock);
	dsl_clonedup_sync_state(dcl, tx);
	return (next);
}

dsl_clonedup_next_t
dsl_clonedup_walk_done(dsl_clonedup_t *dcl, uint64_t *min_txgp,
    dmu_tx_t *tx)
{
	dsl_clonedup_phys_t *p = &dcl->dcl_phys;
	dsl_clonedup_next_t next;

	dsl_clonedup_unch_vacate(dcl);
	mutex_enter(&dcl->dcl_lock);
	switch (p->dclp_phase) {
	case POOL_CLONEDUP_INDEX:
		if (dcl->dcl_nentries == 0) {
			next = dsl_clonedup_partition_done_locked(dcl,
			    min_txgp);
		} else if ((p->dclp_flags &
		    (DSF_CLONEDUP_FULL | DSF_CLONEDUP_QUICK)) ||
		    p->dclp_min_txg == 0) {
			p->dclp_phase = POOL_CLONEDUP_APPLY;
			next = DCLN_APPLY;
		} else {
			p->dclp_phase = POOL_CLONEDUP_MATCH;
			DCL_BUMP(dcl, DCK_MATCH_WALKS, 1);
			*min_txgp = 0;
			next = DCLN_WALK;
		}
		break;
	case POOL_CLONEDUP_MATCH:
		p->dclp_phase = POOL_CLONEDUP_APPLY;
		next = DCLN_APPLY;
		break;
	default:
		/*
		 * An unexpected phase is not worth a panic: end the
		 * run and leave the data alone.
		 */
		zfs_dbgmsg("clonedup: %s: walk finished in phase "
		    "%llu, ending the run",
		    spa_name(dcl->dcl_dp->dp_spa),
		    (u_longlong_t)p->dclp_phase);
		next = DCLN_FINISH;
		break;
	}
	mutex_exit(&dcl->dcl_lock);
	dsl_clonedup_sync_state(dcl, tx);
	return (next);
}

/*
 * The record of the next partition, or of the end of the run, goes
 * out in txg.  A clone in a later txg would reach the disk after the
 * record that covers it.  A batch whose source no writer can reach
 * skips the wait for the previous txg, so it can commit ahead of the
 * sync.  Wait until txg reaches the newest one.
 */
boolean_t
dsl_clonedup_apply_done(dsl_clonedup_t *dcl, uint64_t txg)
{
	boolean_t done;

	mutex_enter(&dcl->dcl_lock);
	if (dcl->dcl_apply_txg > txg) {
		mutex_exit(&dcl->dcl_lock);
		return (B_FALSE);
	}
	done = dcl->dcl_phys.dclp_phase == POOL_CLONEDUP_APPLY &&
	    (!dcl->dcl_index_active || dcl->dcl_nentries == 0) &&
	    dcl->dcl_apply_busy == 0;
	mutex_exit(&dcl->dcl_lock);
	return (done);
}

/*
 * Apply phase: verify and clone, in the open-context zthr.
 */

static int zfs_clonedup_apply_enabled = 1;

/* Blocks the apply workers together put in one txg, at most. */
static uint_t zfs_clonedup_apply_blocks_per_txg = 8192;

/*
 * Apply workers.  0 derives one per four cores.  Either way the
 * count is held to DCL_MAX_WORKERS.
 */
static uint_t zfs_clonedup_apply_threads = 0;

/*
 * zfs_clonedup_apply_blocks_per_txg also caps one transaction and one
 * take from the index.  Each clone reserves brt_sync_dirty_est() of
 * sync space, and a small pool cannot reserve that for a whole txg of
 * clones, so dsl_clonedup_batch_max() sizes a transaction from the
 * pool.  DCL_BATCH_MIN keeps the smallest pool making progress, and
 * DCL_GROUP_MAX caps a take.
 */
#define	DCL_BATCH_MIN		64
#define	DCL_GROUP_MAX		1024

/*
 * A destination that is being mounted, unmounted or opened as a
 * volume answers EBUSY for a few ticks.  The open is retried this
 * often, this far apart.
 */
#define	DCL_OPEN_RETRIES	100
#define	DCL_OPEN_RETRY_MS	10

/* Debug: sleep this many ms before locking and queueing a block. */
static uint_t zfs_clonedup_apply_txg_delay = 0;

/* Debug: sleep this many ms before a worker's final flush. */
static uint_t zfs_clonedup_apply_flush_delay = 0;

/*
 * How long an administrative operation waits for the apply thread to
 * let go of the datasets it holds.  After that the operation goes
 * ahead, and fails with EBUSY if the apply still holds what it
 * needs, as it would if a send or a mount held the dataset.
 */
static uint_t zfs_clonedup_yield_timeout_ms = 10000;

typedef struct dsl_clonedup_group {
	dsl_clonedup_entry_t **g_entries;	/* the caller's */
	uint_t		g_count;
	uint_t		g_max;
	dsl_clonedup_src_t *g_src;	/* from phase two, or NULL */
	uint64_t	g_key;
	uint64_t	g_prop;
	boolean_t	g_counted;	/* the caller counts it */
	boolean_t	g_live;		/* a destination needs work */
	int		g_lost;		/* why it is its own source */
} dsl_clonedup_group_t;

typedef struct dsl_clonedup_stats {
	uint64_t	s_groups;
	uint64_t	s_candidates;
	uint64_t	s_applied;
	uint64_t	s_saved;
	uint64_t	s_saved_snapheld;
	uint64_t	s_stale;
	uint64_t	s_dirty;
	uint64_t	s_differs;
	uint64_t	s_busy;
	uint64_t	s_policy;
	uint64_t	s_errors;
} dsl_clonedup_stats_t;

typedef struct dsl_clonedup_dsinfo {
	uint64_t	di_head;	/* head of the dsl_dir */
	uint64_t	di_mode;	/* clonedup property */
	boolean_t	di_snapshot;
	boolean_t	di_inconsistent;
} dsl_clonedup_dsinfo_t;

/*
 * A dataset's clonedup facts are the same for every candidate in it,
 * and reading them means holding the dataset and walking the dsl_dir
 * chain for the clonedup property, so each worker caches them.  The
 * cache is keyed on the dataset object and emptied when the syncing
 * txg moves, so a property change takes effect within a txg.  In
 * index order every dataset holding a candidate is live at once, so
 * DCL_DSINFO_MAX is a memory bound only; past it the rest is read
 * uncached.
 */
#define	DCL_DSINFO_MAX	4096

typedef struct dsl_clonedup_dscache {
	avl_node_t		dc_node;
	uint64_t		dc_dsobj;
	int			dc_err;
	dsl_clonedup_dsinfo_t	dc_info;
} dsl_clonedup_dscache_t;

static int
dsl_clonedup_dscache_compare(const void *a, const void *b)
{
	const dsl_clonedup_dscache_t *x = a;
	const dsl_clonedup_dscache_t *y = b;

	return (TREE_CMP(x->dc_dsobj, y->dc_dsobj));
}

static void
dsl_clonedup_dscache_vacate(dsl_clonedup_worker_t *w)
{
	dsl_clonedup_dscache_t *c;
	void *cookie = NULL;

	while ((c = avl_destroy_nodes(&w->dcw_dscache,
	    &cookie)) != NULL)
		kmem_free(c, sizeof (*c));
}

static void
dsl_clonedup_dscache_init(dsl_clonedup_worker_t *w)
{
	avl_create(&w->dcw_dscache, dsl_clonedup_dscache_compare,
	    sizeof (dsl_clonedup_dscache_t),
	    offsetof(dsl_clonedup_dscache_t, dc_node));
	w->dcw_dscache_txg = 0;
	w->dcw_dscache_ready = B_TRUE;
}

static void
dsl_clonedup_dscache_fini(dsl_clonedup_worker_t *w)
{
	if (!w->dcw_dscache_ready)
		return;
	dsl_clonedup_dscache_vacate(w);
	avl_destroy(&w->dcw_dscache);
	w->dcw_dscache_ready = B_FALSE;
}

/* Does the live bp still name the block the index recorded? */
static boolean_t
dsl_clonedup_bp_matches(const blkptr_t *bp, const dva_t *dva,
    uint64_t birth)
{
	if (BP_IS_HOLE(bp) || BP_IS_EMBEDDED(bp))
		return (B_FALSE);
	return (BP_GET_PHYSICAL_BIRTH(bp) == birth &&
	    DVA_EQUAL(&bp->blk_dva[0], dva));
}

/*
 * How many bytes of verify reads may be outstanding.  This is the
 * scrub's own budget, so a clonedup run loads a device no harder
 * than a scrub of the same pool and answers to the same tunables.
 */
static uint64_t
dsl_clonedup_verify_limit(dsl_clonedup_t *dcl)
{
	dsl_scan_t *scn = dcl->dcl_dp->dp_scan;
	uint64_t lim = scn != NULL ? scn->scn_maxinflight_bytes : 0;

	/* dsl_scan_init() sets it at import; this only guards zero */
	return (lim != 0 ? lim : 32ULL << 20);
}

/*
 * A pair whose contents differ either shares only the 64-bit key,
 * cityhash4 of the checksum, which costs a wasted read, or shares the
 * full checksum, which is a collision in the checksum itself.  The
 * second is counted and logged apart, since a match under a checksum
 * strong enough for dedup is cloned without a read.
 */
static void
dsl_clonedup_count_collision(dsl_clonedup_t *dcl,
    dsl_clonedup_worker_t *w, const dsl_clonedup_pend_t *pd)
{
	if (!ZIO_CHECKSUM_EQUAL(pd->dcp_sbp.blk_cksum,
	    pd->dcp_dbp.blk_cksum)) {
		DCL_BUMP(dcl, DCK_KEY_COLLISIONS, 1);
		return;
	}
	DCL_BUMP(dcl, DCK_CKSUM_COLLISIONS, 1);
	zfs_dbgmsg("clonedup: checksum collision: objset %llu object "
	    "%llu blkid %llu against objset %llu object %llu blkid "
	    "%llu, checksum %d",
	    (u_longlong_t)w->dcw_batch.dcb_dsobj,
	    (u_longlong_t)pd->dcp_dobject,
	    (u_longlong_t)pd->dcp_blkid,
	    (u_longlong_t)w->dcw_batch.dcb_srcobj,
	    (u_longlong_t)pd->dcp_sobject,
	    (u_longlong_t)pd->dcp_sblkid,
	    (int)BP_GET_CHECKSUM(&pd->dcp_dbp));
}

/* Give the budget back the moment the read lands, as a scrub does. */
static void
dsl_clonedup_verify_done(zio_t *zio)
{
	spa_t *spa = zio->io_spa;
	int *errp = zio->io_private;

	*errp = zio->io_error;
	mutex_enter(&spa->spa_scrub_lock);
	ASSERT3U(spa->spa_scrub_inflight, >=, zio->io_lsize);
	spa->spa_scrub_inflight -= zio->io_lsize;
	cv_broadcast(&spa->spa_scrub_io_cv);
	mutex_exit(&spa->spa_scrub_lock);
}

/* The reads of one verify pass, issued under one parent zio. */
typedef struct dsl_clonedup_pass {
	zio_t		*dp_pio;
	uint_t		dp_first;
	uint_t		dp_end;
} dsl_clonedup_pass_t;

/* One read, the shape scan_exec_io() uses: raw abd, no ARC. */
static void
dsl_clonedup_verify_read(dsl_clonedup_t *dcl, zio_t *pio,
    const blkptr_t *bp, uint64_t object, uint64_t blkid,
    uint64_t dsobj, uint64_t limit, abd_t **abdp, int *errp)
{
	int zf = ZIO_FLAG_CANFAIL | ZIO_FLAG_SPECULATIVE |
	    ZIO_FLAG_SCAN_THREAD | ZIO_FLAG_RAW_COMPRESS;
	spa_t *spa = dcl->dcl_dp->dp_spa;
	uint64_t size = BP_GET_PSIZE(bp);
	zbookmark_phys_t zb;

	mutex_enter(&spa->spa_scrub_lock);
	while (spa->spa_scrub_inflight >= limit)
		cv_wait(&spa->spa_scrub_io_cv, &spa->spa_scrub_lock);
	spa->spa_scrub_inflight += size;
	mutex_exit(&spa->spa_scrub_lock);

	*abdp = abd_alloc_for_io(size, B_FALSE);
	*errp = 0;
	DCL_BUMP(dcl, DCK_VERIFY_READS, 1);
	DCL_BUMP(dcl, DCK_VERIFY_BYTES, size);
	SET_BOOKMARK(&zb, dsobj, object, 0, blkid);
	zio_nowait(zio_read(pio, spa, bp, *abdp, size,
	    dsl_clonedup_verify_done, errp, ZIO_PRIORITY_SCRUB, zf,
	    &zb));
}

static void
dsl_clonedup_verify_one(dsl_clonedup_t *dcl, dsl_clonedup_worker_t *w,
    dsl_clonedup_pend_t *pd, abd_t *sabd, int serr,
    dsl_clonedup_stats_t *st)
{
	uint64_t size = BP_GET_PSIZE(&pd->dcp_dbp);

	/*
	 * dsl_clonedup_verify_issue() gave every pend of the pass a
	 * buffer, and the pass's first pend the source read.
	 */
	ASSERT3P(sabd, !=, NULL);
	ASSERT3P(pd->dcp_dabd, !=, NULL);
	if (serr != 0 || pd->dcp_derr != 0) {
		st->s_errors++;
		pd->dcp_drop = B_TRUE;
	} else if (BP_GET_PSIZE(&pd->dcp_sbp) != size ||
	    BP_GET_LSIZE(&pd->dcp_sbp) !=
	    BP_GET_LSIZE(&pd->dcp_dbp) ||
	    abd_cmp(sabd, pd->dcp_dabd) != 0) {
		st->s_differs++;
		dsl_clonedup_count_collision(dcl, w, pd);
		pd->dcp_drop = B_TRUE;
	}
}

/*
 * Issue one pass and return where the next one starts.  A pass takes
 * half the inflight budget, so two passes in flight stay within it.
 */
static uint_t
dsl_clonedup_verify_issue(dsl_clonedup_t *dcl,
    dsl_clonedup_worker_t *w, uint_t i, uint_t n, uint64_t limit,
    dsl_clonedup_pass_t *ps)
{
	dsl_clonedup_batch_t *b = &w->dcw_batch;
	spa_t *spa = dcl->dcl_dp->dp_spa;
	dsl_clonedup_pend_t *owner = NULL;
	uint64_t held = 0;
	uint64_t cap = MAX(1, limit / 2);

	ps->dp_pio = zio_root(spa, NULL, NULL, ZIO_FLAG_CANFAIL);
	ps->dp_first = i;
	for (; i < n; i++) {
		dsl_clonedup_pend_t *pd = &b->dcb_pend[i];
		boolean_t newsrc;
		uint64_t need;

		pd->dcp_sabd = pd->dcp_dabd = NULL;
		pd->dcp_sowner = B_FALSE;
		pd->dcp_serr = pd->dcp_derr = 0;
		if (pd->dcp_trusted)
			continue;
		newsrc = owner == NULL ||
		    !zfs_dedupe_bp_same_block(&pd->dcp_sbp,
		    &owner->dcp_sbp);
		need = BP_GET_PSIZE(&pd->dcp_dbp);
		if (newsrc)
			need += BP_GET_PSIZE(&pd->dcp_sbp);
		if (held != 0 && held + need > cap)
			break;
		held += need;

		if (newsrc) {
			dsl_clonedup_verify_read(dcl, ps->dp_pio,
			    &pd->dcp_sbp, pd->dcp_sobject,
			    pd->dcp_sblkid, b->dcb_srcobj, limit,
			    &pd->dcp_sabd, &pd->dcp_serr);
			pd->dcp_sowner = B_TRUE;
			owner = pd;
		}
		dsl_clonedup_verify_read(dcl, ps->dp_pio,
		    &pd->dcp_dbp, pd->dcp_dobject,
		    pd->dcp_blkid, b->dcb_dsobj, limit,
		    &pd->dcp_dabd, &pd->dcp_derr);
	}
	ps->dp_end = i;
	return (i);
}

/* Wait for one pass to land and compare everything it read. */
static void
dsl_clonedup_verify_reap(dsl_clonedup_t *dcl,
    dsl_clonedup_worker_t *w, dsl_clonedup_pass_t *ps,
    dsl_clonedup_stats_t *st)
{
	dsl_clonedup_batch_t *b = &w->dcw_batch;
	abd_t *sabd = NULL;
	int serr = 0;

	(void) zio_wait(ps->dp_pio);
	ps->dp_pio = NULL;

	for (uint_t j = ps->dp_first; j < ps->dp_end; j++) {
		dsl_clonedup_pend_t *pd = &b->dcb_pend[j];

		if (pd->dcp_trusted)
			continue;
		if (pd->dcp_sowner) {
			if (sabd != NULL)
				abd_free(sabd);
			sabd = pd->dcp_sabd;
			serr = pd->dcp_serr;
		}
		dsl_clonedup_verify_one(dcl, w, pd, sabd, serr, st);
		abd_free(pd->dcp_dabd);
		pd->dcp_dabd = NULL;
		if (pd->dcp_drop) {
			zfs_clonedup_src_unlock(pd->dcp_slock);
			pd->dcp_slock = NULL;
		}
	}
	if (sabd != NULL)
		abd_free(sabd);
}

/*
 * Compare every queued pair.  Reads go out at scrub priority and
 * count against spa_scrub_inflight, the budget a scrub paces itself
 * with, up to scn_maxinflight_bytes.  Two passes of half that budget
 * each are kept in flight, so the disk has work while the compare
 * runs and the compare has work while the reads land.  A group's
 * copies share one source read: the index hands them out together,
 * so the previous pend's buffer serves until the source changes.
 */
static void
dsl_clonedup_batch_verify(dsl_clonedup_t *dcl,
    dsl_clonedup_worker_t *w, dsl_clonedup_stats_t *st)
{
	dsl_clonedup_batch_t *b = &w->dcw_batch;
	uint64_t limit = dsl_clonedup_verify_limit(dcl);
	uint_t n = b->dcb_count;
	uint_t i = 0;
	uint_t keep = 0;
	dsl_clonedup_pass_t ps[2];
	uint_t cur = 0;

	memset(ps, 0, sizeof (ps));
	if (i < n)
		i = dsl_clonedup_verify_issue(dcl, w, i, n, limit,
		    &ps[0]);
	while (ps[cur].dp_pio != NULL) {
		uint_t nxt = cur ^ 1;

		if (i < n)
			i = dsl_clonedup_verify_issue(dcl, w, i, n,
			    limit, &ps[nxt]);
		dsl_clonedup_verify_reap(dcl, w, &ps[cur], st);
		cur = nxt;
	}

	for (i = 0; i < n; i++) {
		dsl_clonedup_pend_t *pd = &b->dcb_pend[i];

		if (pd->dcp_drop) {
			zfs_clonedup_dst_unlock(pd->dcp_lock);
			continue;
		}
		st->s_candidates++;
		if (keep != i)
			b->dcb_pend[keep] = *pd;
		keep++;
	}
	b->dcb_count = keep;
}

static int
dsl_clonedup_ds_info_read(dsl_pool_t *dp, uint64_t dsobj,
    dsl_clonedup_dsinfo_t *di)
{
	dsl_dataset_t *ds;
	int err;

	memset(di, 0, sizeof (*di));
	dsl_pool_config_enter(dp, FTAG);
	err = dsl_dataset_hold_obj(dp, dsobj, FTAG, &ds);
	if (err == 0) {
		di->di_head =
		    dsl_dir_phys(ds->ds_dir)->dd_head_dataset_obj;
		di->di_snapshot = ds->ds_is_snapshot;
		di->di_inconsistent =
		    (dsl_dataset_phys(ds)->ds_flags &
		    DS_FLAG_INCONSISTENT) != 0;
		di->di_mode = ZFS_CLONEDUP_OFF;
		(void) dsl_prop_get_int_ds(ds,
		    zfs_prop_to_name(ZFS_PROP_CLONEDUP),
		    &di->di_mode);
		dsl_dataset_rele(ds, FTAG);
	}
	dsl_pool_config_exit(dp, FTAG);
	return (err);
}

static int
dsl_clonedup_ds_info(dsl_clonedup_t *dcl, dsl_clonedup_worker_t *w,
    uint64_t dsobj, dsl_clonedup_dsinfo_t *di)
{
	dsl_pool_t *dp = dcl->dcl_dp;
	uint64_t txg = spa_syncing_txg(dp->dp_spa);
	dsl_clonedup_dscache_t *c;
	dsl_clonedup_dscache_t probe;
	avl_index_t where;
	int err;

	if (!w->dcw_dscache_ready)
		return (dsl_clonedup_ds_info_read(dp, dsobj, di));

	if (w->dcw_dscache_txg != txg) {
		dsl_clonedup_dscache_vacate(w);
		w->dcw_dscache_txg = txg;
	}
	probe.dc_dsobj = dsobj;
	c = avl_find(&w->dcw_dscache, &probe, &where);
	if (c != NULL) {
		*di = c->dc_info;
		return (c->dc_err);
	}
	err = dsl_clonedup_ds_info_read(dp, dsobj, di);
	if (avl_numnodes(&w->dcw_dscache) < DCL_DSINFO_MAX) {
		c = kmem_alloc(sizeof (*c), KM_SLEEP);
		c->dc_dsobj = dsobj;
		c->dc_err = err;
		c->dc_info = *di;
		avl_insert(&w->dcw_dscache, c, where);
	}
	return (err);
}

/*
 * Pools whose apply can hold a dataset, from the start of
 * dsl_clonedup_apply_groups() until it has let go of everything,
 * and operations that began while there were none.  Such an
 * operation pauses no pool, and an apply that starts before it ends
 * waits for it as for a pause, see dsl_clonedup_yield_idle().
 */
static uint64_t dsl_clonedup_appliers;
static uint64_t dsl_clonedup_unpaused;
/* the pause of such an operation, which names no pool */
#define	DSL_CLONEDUP_IDLE	((dsl_clonedup_t *)1UL)

/*
 * The apply thread publishes the datasets it holds, or is about to
 * hold, under dcl_yield_lock.  An operation that a long hold would
 * make fail (destroy, promote, rollback, receive, mount, volume open)
 * brackets itself with dsl_clonedup_yield_begin() and _end(): begin
 * asks the thread to let go of everything and waits for that, and
 * the thread holds nothing again until the last such operation has
 * ended.  The pause is global rather than per dataset because promote
 * cannot name the snapshots it is about to move without repeating
 * its own work.  It counts operations, not txgs, because the
 * thread's own txg waits push txgs through in milliseconds on a
 * quiet pool.  An owner that holds a lock the thread could need to
 * let go cannot wait, and uses dsl_clonedup_yield_nudge() instead.
 */
static boolean_t
dsl_clonedup_yield_pending(dsl_clonedup_t *dcl)
{
	return (atomic_load_32(&dcl->dcl_yield_pause) != 0 ||
	    atomic_load_64(&dsl_clonedup_unpaused) != 0 ||
	    ddi_get_lbolt64() < (int64_t)atomic_load_64(
	    (volatile uint64_t *)&dcl->dcl_yield_lease));
}

/* zthr_iscancelled() is for the zthr's own thread alone */
static boolean_t
dsl_clonedup_stop(dsl_clonedup_t *dcl, zthr_t *zthr)
{
	if (atomic_load_32(&dcl->dcl_apply_stop))
		return (B_TRUE);
	if (zthr == NULL || !zthr_iscurthread(zthr) ||
	    !zthr_iscancelled(zthr))
		return (B_FALSE);
	atomic_store_32(&dcl->dcl_apply_stop, B_TRUE);
	return (B_TRUE);
}

/*
 * Wait out the yields, then publish dsobj in *heldp.  Waiting while
 * the other slot is published would hold what the yielding operation
 * is about to need, so that case is ERESTART instead: the caller
 * releases everything and comes back.
 */
static int
dsl_clonedup_hold_begin(dsl_clonedup_t *dcl, dsl_clonedup_worker_t *w,
    uint64_t *heldp, uint64_t dsobj)
{
	boolean_t waited = B_FALSE;

	mutex_enter(&dcl->dcl_yield_lock);
	while (dsl_clonedup_yield_pending(dcl)) {
		if (dsl_clonedup_stop(dcl, dcl->dcl_zthr)) {
			mutex_exit(&dcl->dcl_yield_lock);
			return (SET_ERROR(EINTR));
		}
		if (w->dcw_held_src != 0 ||
		    w->dcw_held_dst != 0) {
			mutex_exit(&dcl->dcl_yield_lock);
			return (SET_ERROR(ERESTART));
		}
		if (!waited) {
			waited = B_TRUE;
			DCL_BUMP(dcl, DCK_YIELD_WAITS, 1);
			zfs_dbgmsg("clonedup hold %llu waits for %u "
			    "operations and a lease of %lld ticks",
			    (u_longlong_t)dsobj, dcl->dcl_yield_pause,
			    (longlong_t)MAX(0, dcl->dcl_yield_lease -
			    ddi_get_lbolt64()));
		}
		(void) cv_timedwait(&dcl->dcl_yield_cv,
		    &dcl->dcl_yield_lock, ddi_get_lbolt() + hz);
	}
	*heldp = dsobj;
	mutex_exit(&dcl->dcl_yield_lock);
	return (0);
}

static void
dsl_clonedup_hold_end(dsl_clonedup_t *dcl, uint64_t *heldp)
{
	mutex_enter(&dcl->dcl_yield_lock);
	*heldp = 0;
	cv_broadcast(&dcl->dcl_yield_cv);
	mutex_exit(&dcl->dcl_yield_lock);
}

/* how many workers hold, or are acquiring, a dataset */
static uint_t
dsl_clonedup_any_held(dsl_clonedup_t *dcl)
{
	uint_t n = 0;

	for (uint_t i = 0; i < dcl->dcl_nworkers; i++) {
		dsl_clonedup_worker_t *w = dcl->dcl_workers[i];

		if (w != NULL &&
		    (w->dcw_held_src != 0 || w->dcw_held_dst != 0))
			n++;
	}
	return (n);
}

/*
 * How many workers hold a destination.  Sources are shared, so
 * dsl_clonedup_any_held() cannot tell two workers sharing a
 * destination from two holding the same source.
 */
static uint_t
dsl_clonedup_dst_held(dsl_clonedup_t *dcl)
{
	uint_t n = 0;

	for (uint_t i = 0; i < dcl->dcl_nworkers; i++) {
		dsl_clonedup_worker_t *w = dcl->dcl_workers[i];

		if (w != NULL && w->dcw_held_dst != 0)
			n++;
	}
	return (n);
}

static const char dsl_clonedup_yield_tag[] = "clonedup yield";

/*
 * Ask the apply to let go of everything, and wait until it has or
 * until zfs_clonedup_yield_timeout_ms.  The caller holds a reference
 * on spa and no lock the apply could need to let go.  The pause lasts
 * until the caller hands the return value to
 * dsl_clonedup_yield_end().  Until then a reference of the pause's
 * own keeps the pool from being exported, so the end reaches the same
 * pause.  NULL is a pool with no clonedup state and nothing to end.
 */
dsl_clonedup_t *
dsl_clonedup_yield_begin(spa_t *spa)
{
	dsl_pool_t *dp = spa_get_dsl(spa);
	dsl_clonedup_t *dcl;
	clock_t deadline;

	if (dp == NULL || (dcl = dp->dp_clonedup) == NULL)
		return (NULL);

	spa_open_ref(spa, dsl_clonedup_yield_tag);
	mutex_enter(&dcl->dcl_yield_lock);
	atomic_inc_32(&dcl->dcl_yield_pause);
	if (!dsl_clonedup_any_held(dcl)) {
		mutex_exit(&dcl->dcl_yield_lock);
		return (dcl);
	}
	zfs_dbgmsg("clonedup yield: %u worker(s) holding, %u of them "
	    "a destination", dsl_clonedup_any_held(dcl),
	    dsl_clonedup_dst_held(dcl));
	DCL_BUMP(dcl, DCK_YIELDS, 1);
	dsl_clonedup_apply_wakeup(spa);
	deadline = ddi_get_lbolt() +
	    MSEC_TO_TICK(zfs_clonedup_yield_timeout_ms);
	while (dsl_clonedup_any_held(dcl)) {
		if (cv_timedwait(&dcl->dcl_yield_cv,
		    &dcl->dcl_yield_lock, deadline) == -1)
			break;
	}
	zfs_dbgmsg("clonedup yield done: %u worker(s) holding",
	    dsl_clonedup_any_held(dcl));
	mutex_exit(&dcl->dcl_yield_lock);
	return (dcl);
}

void
dsl_clonedup_yield_end(dsl_clonedup_t *dcl)
{
	if (dcl == NULL)
		return;
	if (dcl == DSL_CLONEDUP_IDLE) {
		atomic_dec_64(&dsl_clonedup_unpaused);
		return;
	}

	mutex_enter(&dcl->dcl_yield_lock);
	ASSERT3U(dcl->dcl_yield_pause, >, 0);
	if (dcl->dcl_yield_pause > 0)
		atomic_dec_32(&dcl->dcl_yield_pause);
	cv_broadcast(&dcl->dcl_yield_cv);
	mutex_exit(&dcl->dcl_yield_lock);
	spa_close(dcl->dcl_dp->dp_spa, dsl_clonedup_yield_tag);
}

/*
 * A pause for an operation that starts while no pool's apply can
 * hold a dataset.  It looks up no pool and takes no pool's lock.  An
 * apply that starts before the caller hands the pause to
 * dsl_clonedup_yield_end() holds nothing until then.  NULL means an
 * apply is running, and the caller needs its pool's own pause.
 */
dsl_clonedup_t *
dsl_clonedup_yield_idle(void)
{
	atomic_inc_64(&dsl_clonedup_unpaused);
	membar_sync();
	if (atomic_load_64(&dsl_clonedup_appliers) == 0)
		return (DSL_CLONEDUP_IDLE);
	atomic_dec_64(&dsl_clonedup_unpaused);
	return (NULL);
}

/*
 * The same, with the pool found from a dataset name.  spa_open()
 * takes spa_namespace_lock unless the caller holds it already.
 * While no apply runs, dsl_clonedup_yield_idle() answers instead.
 */
dsl_clonedup_t *
dsl_clonedup_yield_begin_name(const char *name)
{
	dsl_clonedup_t *dcl;
	spa_t *spa;

	if ((dcl = dsl_clonedup_yield_idle()) != NULL)
		return (dcl);
	if (spa_open(name, &spa, FTAG) != 0)
		return (NULL);
	dcl = dsl_clonedup_yield_begin(spa);
	spa_close(spa, FTAG);
	return (dcl);
}

/*
 * For an owner that holds a lock the apply could need to let go, a
 * volume's state lock or spa_namespace_lock, and so cannot wait in
 * dsl_clonedup_yield_begin().  Keep the workers off for a moment
 * without waiting for them.  The request lapses by itself.  The
 * caller drops its locks, tries again, and nudges again for as long
 * as the dataset stays busy.  B_TRUE says a worker still holds a
 * dataset, so a retry can succeed.  After
 * zfs_clonedup_yield_timeout_ms of nudges the answer is B_FALSE, as
 * for a yield that times out.
 */
boolean_t
dsl_clonedup_yield_nudge(spa_t *spa)
{
	dsl_pool_t *dp = spa_get_dsl(spa);
	dsl_clonedup_t *dcl;
	int64_t now = ddi_get_lbolt64();
	boolean_t lapsed;
	boolean_t held;

	if (dp == NULL || (dcl = dp->dp_clonedup) == NULL)
		return (B_FALSE);

	mutex_enter(&dcl->dcl_yield_lock);
	lapsed = now >= dcl->dcl_yield_lease;
	if (lapsed)
		dcl->dcl_yield_since = now;
	held = dsl_clonedup_any_held(dcl) &&
	    now - dcl->dcl_yield_since <
	    MSEC_TO_TICK(zfs_clonedup_yield_timeout_ms);
	if (held) {
		if (lapsed)
			DCL_BUMP(dcl, DCK_YIELDS, 1);
		atomic_store_64(
		    (volatile uint64_t *)&dcl->dcl_yield_lease,
		    now + MSEC_TO_TICK(100));
	}
	mutex_exit(&dcl->dcl_yield_lock);
	if (held)
		dsl_clonedup_apply_wakeup(spa);
	return (held);
}

boolean_t
dsl_clonedup_src_held(dsl_pool_t *dp, uint64_t dsobj)
{
	dsl_clonedup_t *dcl = dp->dp_clonedup;
	boolean_t held = B_FALSE;

	/*
	 * The caller owns the dataset.  An apply that starts after
	 * that finds the owner under ds_lock before it relies on a
	 * source having none, so only an apply that is running can
	 * hold the dataset unowned.
	 */
	if (dcl == NULL ||
	    atomic_load_64(&dsl_clonedup_appliers) == 0)
		return (B_FALSE);
	mutex_enter(&dcl->dcl_yield_lock);
	for (uint_t i = 0; i < dcl->dcl_nworkers && !held; i++) {
		dsl_clonedup_worker_t *w = dcl->dcl_workers[i];

		held = w != NULL && w->dcw_held_src == dsobj;
	}
	mutex_exit(&dcl->dcl_yield_lock);
	return (held);
}

static void
dsl_clonedup_src_unpin(dsl_clonedup_worker_t *w)
{
	zfs_clonedup_src_unpin(w->dcw_spin);
	w->dcw_spin = NULL;
	w->dcw_spin_for = 0;
}

/*
 * Source datasets are long-held so they cannot be destroyed between
 * reading the source bp and cloning it.  zfs_clonedup_src_lock()
 * keeps a source block in place while its verify read is out, and
 * the previous-txg wait in dsl_clonedup_batch_flush() closes the
 * writer race.
 */
static void
dsl_clonedup_src_rele(dsl_clonedup_t *dcl, dsl_clonedup_worker_t *w)
{
	dsl_clonedup_src_unpin(w);
	if (w->dcw_src_ds != NULL) {
		dsl_dataset_long_rele(w->dcw_src_ds, dcl);
		dsl_dataset_rele(w->dcw_src_ds, dcl);
	}
	w->dcw_src_ds = NULL;
	w->dcw_src_os = NULL;
	w->dcw_src_objset = 0;
	dsl_clonedup_hold_end(dcl, &w->dcw_held_src);
}

static int
dsl_clonedup_src_hold(dsl_clonedup_t *dcl, dsl_clonedup_worker_t *w,
    uint64_t dsobj, objset_t **osp)
{
	dsl_pool_t *dp = dcl->dcl_dp;
	dsl_dataset_t *ds = NULL;
	objset_t *os = NULL;
	int err;

	if (w->dcw_src_ds != NULL &&
	    w->dcw_src_objset == dsobj) {
		*osp = w->dcw_src_os;
		return (0);
	}
	dsl_clonedup_src_rele(dcl, w);
	err = dsl_clonedup_hold_begin(dcl, w, &w->dcw_held_src,
	    dsobj);
	if (err != 0)
		return (err);

	dsl_pool_config_enter(dp, FTAG);
	err = dsl_dataset_hold_obj(dp, dsobj, dcl, &ds);
	if (err == 0) {
		dsl_dataset_long_hold(ds, dcl);
		err = dmu_objset_from_ds(ds, &os);
		if (err != 0) {
			dsl_dataset_long_rele(ds, dcl);
			dsl_dataset_rele(ds, dcl);
		}
	}
	dsl_pool_config_exit(dp, FTAG);
	if (err != 0) {
		dsl_clonedup_hold_end(dcl, &w->dcw_held_src);
		return (err);
	}

	w->dcw_src_ds = ds;
	w->dcw_src_os = os;
	w->dcw_src_objset = dsobj;
	*osp = os;
	return (0);
}

/*
 * The first few refusals of a run go to the debug log so that a run
 * which cloned nothing can say why.
 */
static void
dsl_clonedup_dbg(dsl_clonedup_t *dcl, const char *what,
    uint64_t dsobj, uint64_t object, uint64_t blkid,
    const dsl_clonedup_loc_t *src, int err, int res)
{
	uint32_t left;

	do {
		left = dcl->dcl_dbg_left;
		if (left == 0)
			return;
	} while (atomic_cas_32(&dcl->dcl_dbg_left, left, left - 1) !=
	    left);
	zfs_dbgmsg("clonedup %s: objset %llu object %llu blkid %llu "
	    "from objset %llu object %llu blkid %llu err %d res %d",
	    what, (u_longlong_t)dsobj, (u_longlong_t)object,
	    (u_longlong_t)blkid,
	    (u_longlong_t)(src != NULL ? src->l_objset : 0),
	    (u_longlong_t)(src != NULL ? src->l_object : 0),
	    (u_longlong_t)(src != NULL ? src->l_blkid : 0), err, res);
}

static void dsl_clonedup_batch_flush(dsl_clonedup_t *dcl,
    dsl_clonedup_worker_t *w);

static void
dsl_clonedup_dst_rele(dsl_clonedup_t *dcl, dsl_clonedup_worker_t *w)
{
	if (w->dcw_dst != NULL)
		zfs_clonedup_dst_close(w->dcw_dst);
	w->dcw_dst = NULL;
	w->dcw_dst_objset = 0;
	w->dcw_dst_object = 0;
	dsl_clonedup_hold_end(dcl, &w->dcw_held_dst);
}

/*
 * The handles a batch has parked, hashed by object, since a batch
 * can hold thousands of them and most lookups miss.  Chains hang off
 * the slot array so nothing else has to move.
 */
static uint_t
dsl_clonedup_dst_bucket(const dsl_clonedup_batch_t *b, uint64_t obj)
{
	return ((uint_t)((obj * 0x9E3779B97F4A7C15ULL) >> 32) &
	    b->dcb_dstmask);
}

static void
dsl_clonedup_dst_link(dsl_clonedup_batch_t *b, uint_t slot)
{
	uint_t h = dsl_clonedup_dst_bucket(b, b->dcb_dstobj[slot]);

	b->dcb_dstnext[slot] = b->dcb_dsthash[h];
	b->dcb_dsthash[h] = slot + 1;
}

static void
dsl_clonedup_dst_unlink(dsl_clonedup_batch_t *b, uint_t slot)
{
	uint_t h = dsl_clonedup_dst_bucket(b, b->dcb_dstobj[slot]);
	uint32_t *pp = &b->dcb_dsthash[h];

	while (*pp != 0) {
		uint_t cur = *pp - 1;

		if (cur == slot) {
			*pp = b->dcb_dstnext[slot];
			return;
		}
		pp = &b->dcb_dstnext[cur];
	}
}

static boolean_t
dsl_clonedup_dst_find(const dsl_clonedup_batch_t *b, uint64_t obj,
    uint_t *slotp)
{
	uint32_t e = b->dcb_dsthash[dsl_clonedup_dst_bucket(b, obj)];

	while (e != 0) {
		uint_t slot = e - 1;

		if (b->dcb_dstobj[slot] == obj) {
			*slotp = slot;
			return (B_TRUE);
		}
		e = b->dcb_dstnext[slot];
	}
	return (B_FALSE);
}

static int
dsl_clonedup_dst_hold(dsl_clonedup_t *dcl, dsl_clonedup_worker_t *w,
    uint64_t dsobj, uint64_t object, zfs_clonedup_dst_t **dstp)
{
	dsl_clonedup_batch_t *b = &w->dcw_batch;
	int err;
	int tries;

	if (w->dcw_dst != NULL &&
	    w->dcw_dst_objset == dsobj &&
	    w->dcw_dst_object == object) {
		*dstp = w->dcw_dst;
		return (0);
	}
	/*
	 * A handle parked for the flush is still open, so look there
	 * before opening another.  In source order the destinations
	 * cycle among the copies of one file, and each would
	 * otherwise pay a fresh zfs_zget.  A batch holds one dataset,
	 * so the object alone identifies the handle.
	 */
	if (b->dcb_count > 0 && b->dcb_dsobj == dsobj) {
		uint_t i;

		if (dsl_clonedup_dst_find(b, object, &i)) {
			zfs_clonedup_dst_t *hit = b->dcb_dsts[i];

			dsl_clonedup_dst_unlink(b, i);
			if (w->dcw_dst != NULL) {
				b->dcb_dsts[i] = w->dcw_dst;
				b->dcb_dstobj[i] = w->dcw_dst_object;
				dsl_clonedup_dst_link(b, i);
			} else {
				uint_t last = --b->dcb_ndst;

				if (last != i) {
					dsl_clonedup_dst_unlink(b,
					    last);
					b->dcb_dsts[i] =
					    b->dcb_dsts[last];
					b->dcb_dstobj[i] =
					    b->dcb_dstobj[last];
					dsl_clonedup_dst_link(b, i);
				}
			}
			w->dcw_dst = hit;
			w->dcw_dst_objset = dsobj;
			w->dcw_dst_object = object;
			*dstp = hit;
			return (0);
		}
	}
	if (b->dcb_count > 0 && (b->dcb_dsobj != dsobj ||
	    b->dcb_ndst == b->dcb_max))
		dsl_clonedup_batch_flush(dcl, w);
	if (b->dcb_count > 0) {
		/*
		 * Same dataset, another object: a queued block still
		 * needs the open handle, so park it for the flush and
		 * leave the dataset published to the yield handshake.
		 * A dataset is owned only once and a filesystem
		 * entered once, so another object shares the parked
		 * handle's ownership or entry.
		 */
		if (w->dcw_dst != NULL) {
			b->dcb_dstobj[b->dcb_ndst] =
			    w->dcw_dst_object;
			b->dcb_dsts[b->dcb_ndst] = w->dcw_dst;
			dsl_clonedup_dst_link(b, b->dcb_ndst++);
			w->dcw_dst = NULL;
			w->dcw_dst_objset = 0;
			w->dcw_dst_object = 0;
		}
	} else {
		if (w->dcw_spin_for != dsobj)
			dsl_clonedup_src_unpin(w);
		w->dcw_spin_for = 0;
		dsl_clonedup_dst_rele(dcl, w);
		err = dsl_clonedup_hold_begin(dcl, w,
		    &w->dcw_held_dst, dsobj);
		if (err != 0)
			return (err);
	}
	for (tries = 0; ; tries++) {
		if (b->dcb_ndst > 0) {
			err = zfs_clonedup_dst_share(b->dcb_dsts[0],
			    object, &w->dcw_dst);
		} else {
			err = zfs_clonedup_dst_open(
			    dcl->dcl_dp->dp_spa, dsobj, object,
			    &w->dcw_dst);
		}
		/*
		 * The hold is dropped for the wait: the operation
		 * that answered EBUSY may be waiting for it.
		 */
		if (err != EBUSY || b->dcb_count != 0 ||
		    tries >= DCL_OPEN_RETRIES ||
		    dsl_clonedup_stop(dcl, dcl->dcl_zthr))
			break;
		dsl_clonedup_hold_end(dcl, &w->dcw_held_dst);
		delay(MSEC_TO_TICK(DCL_OPEN_RETRY_MS));
		err = dsl_clonedup_hold_begin(dcl, w,
		    &w->dcw_held_dst, dsobj);
		if (err != 0)
			return (err);
	}
	if (err != 0) {
		dsl_clonedup_dbg(dcl, "open", dsobj, object, 0, NULL,
		    err, -1);
		if (b->dcb_count == 0)
			dsl_clonedup_hold_end(dcl, &w->dcw_held_dst);
		return (err);
	}
	DCL_BUMP(dcl, zfs_clonedup_dst_kind(w->dcw_dst), 1);
	w->dcw_dst_objset = dsobj;
	w->dcw_dst_object = object;
	*dstp = w->dcw_dst;
	return (0);
}

/* Release everything when asked to; the source is re-held later. */
static void
dsl_clonedup_yield_release(dsl_clonedup_t *dcl,
    dsl_clonedup_worker_t *w)
{
	if (!dsl_clonedup_yield_pending(dcl))
		return;
	dsl_clonedup_batch_flush(dcl, w);
	dsl_clonedup_dst_rele(dcl, w);
	dsl_clonedup_src_rele(dcl, w);
}

static void
dsl_clonedup_entry_to_src(const dsl_clonedup_entry_t *e,
    dsl_clonedup_src_t *s)
{
	memset(s, 0, sizeof (*s));
	s->dcs_dva = e->dce_dva;
	s->dcs_birth = e->dce_birth;
	s->dcs_objset = e->dce_objset;
	s->dcs_object = e->dce_object;
	s->dcs_blkid = e->dce_blkid;
	s->dcs_blkszsec = e->dce_blkszsec;
	s->dcs_dntype = e->dce_dntype;
	s->dcs_flags = e->dce_flags;
}

/*
 * Source preference: a block that is not a gang block, so the
 * redirects free the gang trees; then a block on a special-class
 * vdev; then a snapshot-held or source-only block, which cannot be
 * rewritten anyway; then a block whose BRT range held an entry when
 * the walk saw it, an entry that may be another block's; then the
 * older block; then the lowest locator.  A block on a removing or
 * indirect vdev ranks below every other, since it never survives,
 * see dsl_clonedup_bp_eligible().
 */
static int
dsl_clonedup_src_rank(const dsl_clonedup_src_t *s)
{
	int rank = 0;

	if (s->dcs_flags & DCE_F_REMOVED)
		return (-1);
	if (!DVA_GET_GANG(&s->dcs_dva))
		rank += 8;
	if (s->dcs_flags & DCE_F_SPECIAL)
		rank += 4;
	if (s->dcs_flags & (DCE_F_SNAPHELD | DCE_F_SRCONLY))
		rank += 2;
	if (s->dcs_flags & DCE_F_MAYBE_SHARED)
		rank += 1;
	return (rank);
}

static boolean_t
dsl_clonedup_src_better_ranked(int ra, const dsl_clonedup_src_t *a,
    int rb, const dsl_clonedup_src_t *b)
{
	if (ra != rb)
		return (ra > rb);
	if (a->dcs_birth != b->dcs_birth)
		return (a->dcs_birth < b->dcs_birth);
	if (a->dcs_objset != b->dcs_objset)
		return (a->dcs_objset < b->dcs_objset);
	if (a->dcs_object != b->dcs_object)
		return (a->dcs_object < b->dcs_object);
	return (a->dcs_blkid < b->dcs_blkid);
}

static boolean_t
dsl_clonedup_src_better(const dsl_clonedup_src_t *a,
    const dsl_clonedup_src_t *b)
{
	return (dsl_clonedup_src_better_ranked(
	    dsl_clonedup_src_rank(a), a,
	    dsl_clonedup_src_rank(b), b));
}

/*
 * Whether src may serve the entries of g from index start on.  A src
 * on a removing or indirect vdev serves none.  A gang src may not
 * serve a copy that is not a gang block, since that copy survives.  A
 * src off the special class may not serve a copy on it, since the
 * copy on the special class survives.  The one exception is a gang
 * copy on the special class under a src that is not a gang block.
 * The src serves the rest of the group, and that copy keeps its own
 * block, see dsl_clonedup_src_keeps().
 */
static boolean_t
dsl_clonedup_src_serves(const dsl_clonedup_group_t *g, uint_t start,
    const dsl_clonedup_src_t *src)
{
	boolean_t gang = DVA_GET_GANG(&src->dcs_dva);
	boolean_t special = (src->dcs_flags & DCE_F_SPECIAL) != 0;

	if (src->dcs_flags & DCE_F_REMOVED)
		return (B_FALSE);
	for (uint_t i = start; i < g->g_count; i++) {
		const dsl_clonedup_entry_t *e = g->g_entries[i];
		boolean_t egang = DVA_GET_GANG(&e->dce_dva);

		if (gang && !egang)
			return (B_FALSE);
		if (!special && (e->dce_flags & DCE_F_SPECIAL) &&
		    egang == gang)
			return (B_FALSE);
	}
	return (B_TRUE);
}

/*
 * Whether e keeps its own block under src.  No copy on the special
 * class is redirected off it.  The ranks and
 * dsl_clonedup_src_serves() leave such a copy under a src off the
 * special class only when the copy is a gang block and the src is
 * not.
 */
static boolean_t
dsl_clonedup_src_keeps(const dsl_clonedup_src_t *src,
    const dsl_clonedup_entry_t *e)
{
	return ((e->dce_flags & DCE_F_SPECIAL) != 0 &&
	    !(src->dcs_flags & DCE_F_SPECIAL));
}

/*
 * Pick the surviving copy for a group.  Tier 0 reuses the source of
 * the previous take of the same group, so a group larger than one
 * take still ends up sharing one block.  Tier 1 is the older block
 * the match walk found, tier 2 the best of the entries from index
 * start on.  Tiers 0 and 1 are passed over when
 * dsl_clonedup_src_serves() refuses their source.  Returns the tier
 * used, so a caller that finds the block gone can ask for the next
 * one.
 */
static int
dsl_clonedup_group_choose_src(dsl_clonedup_worker_t *w,
    const dsl_clonedup_group_t *g, uint_t start, int tier,
    dsl_clonedup_src_t *src)
{
	int used = 2;

	if (tier <= 0 && w->dcw_group_src_valid &&
	    w->dcw_group_key == g->g_key &&
	    w->dcw_group_prop == g->g_prop &&
	    dsl_clonedup_src_serves(g, start, &w->dcw_group_src)) {
		*src = w->dcw_group_src;
		used = 0;
	} else if (tier <= 1 && g->g_src != NULL &&
	    dsl_clonedup_src_serves(g, start, g->g_src)) {
		*src = *g->g_src;
		used = 1;
	} else {
		dsl_clonedup_src_t cand;
		int rsrc;
		int rcand;

		dsl_clonedup_entry_to_src(g->g_entries[start], src);
		rsrc = dsl_clonedup_src_rank(src);
		for (uint_t i = start + 1; i < g->g_count; i++) {
			dsl_clonedup_entry_to_src(g->g_entries[i],
			    &cand);
			rcand = dsl_clonedup_src_rank(&cand);
			if (dsl_clonedup_src_better_ranked(rcand,
			    &cand, rsrc, src)) {
				*src = cand;
				rsrc = rcand;
			}
		}
	}
	w->dcw_group_src = *src;
	w->dcw_group_key = g->g_key;
	w->dcw_group_prop = g->g_prop;
	w->dcw_group_src_valid = B_TRUE;
	return (used);
}

/*
 * The entry whose block was chosen as source turned out stale.  Move
 * it to index start so the next choice and the apply loop skip it.
 */
static void
dsl_clonedup_group_drop_src(dsl_clonedup_group_t *g, uint_t start,
    const dsl_clonedup_src_t *src)
{
	for (uint_t i = start; i < g->g_count; i++) {
		dsl_clonedup_entry_t *e = g->g_entries[i];

		if (DVA_EQUAL(&e->dce_dva, &src->dcs_dva) &&
		    e->dce_birth == src->dcs_birth) {
			g->g_entries[i] = g->g_entries[start];
			g->g_entries[start] = e;
			return;
		}
	}
}

/*
 * How many entries of one group the apply weighs together.  A group
 * is copies of one block and needs two of them to form a pair, so it
 * floors at two however small the per-txg cap is.  The tunable can
 * change at any time, so dsl_clonedup_order_index() reads it once,
 * sizes its borrowed buffer from it and hands the same value to every
 * take of the pass.
 */
static uint_t
dsl_clonedup_group_max(void)
{
	return (MIN(MAX(2, zfs_clonedup_apply_blocks_per_txg),
	    DCL_GROUP_MAX));
}

/* Is b another reference to a's block? */
static boolean_t
dsl_clonedup_same_ref(const dsl_clonedup_entry_t *a,
    const dsl_clonedup_entry_t *b)
{
	return (b != NULL && DVA_EQUAL(&a->dce_dva, &b->dce_dva) &&
	    a->dce_birth == b->dce_birth);
}

/*
 * e is the first reference to its block that the takes reach, and
 * next follows it in the index.  A block's references sort together,
 * so the rest are all still there.  Saved counts the block at one of
 * them, and this flags the others.  A block that a snapshot holds
 * through any reference frees nothing until the snapshot goes, so
 * the first snapshot-held reference carries the count, and the block
 * lands in saved_snapheld whatever order the objsets sort in.
 */
static void
dsl_clonedup_count_at(dsl_clonedup_t *dcl, dsl_clonedup_entry_t *e,
    dsl_clonedup_entry_t *next)
{
	dsl_clonedup_entry_t *c = e;
	dsl_clonedup_entry_t *n;
	boolean_t kept;

	ASSERT(MUTEX_HELD(&dcl->dcl_lock));
	/*
	 * The run never rewrites a source-only reference, so one
	 * keeps the block whatever happens to the rest, and none of
	 * them counts.  dsl_clonedup_group_take() notes one that
	 * sorts before e.
	 */
	kept = e->dce_birth == dcl->dcl_kept_birth &&
	    DVA_EQUAL(&e->dce_dva, &dcl->dcl_kept_dva);
	for (n = next; !kept && dsl_clonedup_same_ref(e, n);
	    n = AVL_NEXT(&dcl->dcl_index, n))
		kept = (n->dce_flags & DCE_F_SRCONLY) != 0;
	if (kept)
		c = NULL;
	if (!(e->dce_flags & DCE_F_SNAPHELD)) {
		for (n = next; c == e && dsl_clonedup_same_ref(e, n);
		    n = AVL_NEXT(&dcl->dcl_index, n)) {
			if ((n->dce_flags & (DCE_F_SNAPHELD |
			    DCE_F_SRCONLY)) == DCE_F_SNAPHELD)
				c = n;
		}
	}
	if (c != e)
		e->dce_flags |= DCE_F_REPEAT;
	for (n = next; dsl_clonedup_same_ref(e, n);
	    n = AVL_NEXT(&dcl->dcl_index, n)) {
		if (n != c && !(n->dce_flags & DCE_F_SRCONLY))
			n->dce_flags |= DCE_F_REPEAT;
	}
}

/*
 * Take the next group of equal-content candidates out of the index
 * into buf, which has room for max of them.  The entries now belong
 * to the caller.
 */
static boolean_t
dsl_clonedup_group_take(dsl_clonedup_t *dcl, dsl_clonedup_worker_t *w,
    dsl_clonedup_group_t *g, dsl_clonedup_entry_t **buf, uint_t max)
{
	dsl_clonedup_entry_t *e;
	dsl_clonedup_entry_t *next;

	memset(g, 0, sizeof (*g));
	g->g_max = max;

	mutex_enter(&dcl->dcl_lock);
	if (!dcl->dcl_index_active || w->dcw_gen != dcl->dcl_gen ||
	    (e = avl_first(&dcl->dcl_index)) == NULL) {
		mutex_exit(&dcl->dcl_lock);
		return (B_FALSE);
	}
	g->g_key = e->dce_key;
	g->g_prop = e->dce_prop;
	g->g_entries = buf;
	while (e != NULL && e->dce_key == g->g_key &&
	    e->dce_prop == g->g_prop && g->g_count < g->g_max) {
		next = AVL_NEXT(&dcl->dcl_index, e);
		avl_remove(&dcl->dcl_index, e);
		dcl->dcl_nentries--;
		dcl->dcl_mem_used -= sizeof (*e);
		if (e->dce_src != NULL) {
			dcl->dcl_nsrc--;
			dcl->dcl_mem_used -=
			    sizeof (dsl_clonedup_src_t);
			if (g->g_src == NULL)
				g->g_src = e->dce_src;
			else
				kmem_free(e->dce_src,
				    sizeof (*e->dce_src));
			e->dce_src = NULL;
		}
		if (e->dce_flags & DCE_F_SRCONLY) {
			dcl->dcl_kept_dva = e->dce_dva;
			dcl->dcl_kept_birth = e->dce_birth;
		}
		/*
		 * References to one physical block share DVA[0] and
		 * physical birth, as zfs_dedupe_bp_same_block()
		 * checks, so they sort together and the takes reach
		 * them in a row.  At the first of them that can be a
		 * destination, dsl_clonedup_count_at() picks the one
		 * that carries the count.
		 */
		if (!(e->dce_flags & DCE_F_SRCONLY)) {
			dva_t *last = &dcl->dcl_take_dva;

			if (e->dce_birth != dcl->dcl_take_birth ||
			    !DVA_EQUAL(&e->dce_dva, last))
				dsl_clonedup_count_at(dcl, e, next);
			*last = e->dce_dva;
			dcl->dcl_take_birth = e->dce_birth;
		}
		g->g_entries[g->g_count++] = e;
		e = next;
	}
	dcl->dcl_apply_busy++;
	mutex_exit(&dcl->dcl_lock);
	return (B_TRUE);
}

static void
dsl_clonedup_group_free(dsl_clonedup_group_t *g)
{
	for (uint_t i = 0; i < g->g_count; i++) {
		if (g->g_entries[i] != NULL)
			kmem_cache_free(dsl_clonedup_entry_cache,
			    g->g_entries[i]);
	}
	if (g->g_src != NULL)
		kmem_free(g->g_src, sizeof (*g->g_src));
	memset(g, 0, sizeof (*g));
}

/*
 * Count n blocks in tx's txg, or commit tx, wait for the next txg
 * to open on its own, and return B_FALSE.
 */
boolean_t
dsl_clonedup_apply_paced(dmu_tx_t *tx, uint_t n)
{
	dsl_pool_t *dp = dmu_tx_pool(tx);
	dsl_clonedup_t *dcl = dp->dp_clonedup;
	uint64_t cap = MAX(1, zfs_clonedup_apply_blocks_per_txg);
	uint64_t txg = dmu_tx_get_txg(tx);
	boolean_t room;
	boolean_t newest;

	mutex_enter(&dcl->dcl_lock);
	if (txg > dcl->dcl_apply_txg) {
		dcl->dcl_apply_txg = txg;
		dcl->dcl_apply_in_txg = 0;
	}
	newest = txg == dcl->dcl_apply_txg;
	room = newest && (dcl->dcl_apply_in_txg == 0 ||
	    dcl->dcl_apply_in_txg + n <= cap);
	if (room)
		dcl->dcl_apply_in_txg += n;
	mutex_exit(&dcl->dcl_lock);
	if (room)
		return (B_TRUE);
	dmu_tx_commit(tx);
	if (newest)
		txg_wait_open(dp, txg + 1, B_FALSE);
	return (B_FALSE);
}

/*
 * dcl_gen only grows.  A worker that reads it a moment late learns of
 * the change at its next look, as it would from a locked read made
 * just before the change.
 */
static boolean_t
dsl_clonedup_stale(dsl_clonedup_t *dcl, dsl_clonedup_worker_t *w)
{
	return (w->dcw_gen != atomic_load_64(&dcl->dcl_gen));
}

static void
dsl_clonedup_stats_add(dsl_clonedup_t *dcl, dsl_clonedup_worker_t *w,
    const dsl_clonedup_stats_t *st)
{
	dsl_clonedup_phys_t *p = &dcl->dcl_phys;

	ASSERT(MUTEX_HELD(&dcl->dcl_lock));
	if (w->dcw_gen != dcl->dcl_gen)
		return;
	p->dclp_groups += st->s_groups;
	p->dclp_candidates += st->s_candidates;
	p->dclp_applied += st->s_applied;
	p->dclp_bytes_saved += st->s_saved;
	p->dclp_bytes_saved_snapheld += st->s_saved_snapheld;
	p->dclp_skipped_stale += st->s_stale;
	p->dclp_skipped_dirty += st->s_dirty;
	p->dclp_skipped_differs += st->s_differs;
	p->dclp_skipped_busy += st->s_busy;
	p->dclp_skipped_policy += st->s_policy;
	p->dclp_errors += st->s_errors;
}

static void
dsl_clonedup_stats_fold(dsl_clonedup_t *dcl, dsl_clonedup_worker_t *w,
    const dsl_clonedup_stats_t *st)
{
	mutex_enter(&dcl->dcl_lock);
	dsl_clonedup_stats_add(dcl, w, st);
	mutex_exit(&dcl->dcl_lock);
}

static void
dsl_clonedup_stats_sum(dsl_clonedup_stats_t *a,
    const dsl_clonedup_stats_t *b)
{
	a->s_groups += b->s_groups;
	a->s_candidates += b->s_candidates;
	a->s_applied += b->s_applied;
	a->s_saved += b->s_saved;
	a->s_saved_snapheld += b->s_saved_snapheld;
	a->s_stale += b->s_stale;
	a->s_dirty += b->s_dirty;
	a->s_differs += b->s_differs;
	a->s_busy += b->s_busy;
	a->s_policy += b->s_policy;
	a->s_errors += b->s_errors;
}

/*
 * What one transaction may hold.  Each clone reserves
 * brt_sync_dirty_est() of sync space, and a full
 * zfs_clonedup_apply_blocks_per_txg of them is more than a small
 * pool can reserve.  So a batch takes at most a quarter of the
 * dirty budget and a 64th of what the pool can still allocate: a
 * large pool still reaches the tunable, a small one gets a batch
 * it can afford.
 *
 * dmu_tx_hold_clone_by_dnode() also counts a level-1 indirect block
 * of memory for each clone, and dmu_tx_assign() retries for as long
 * as a transaction's memory is more than the ARC target, forever
 * once it is more than the ARC maximum.  So a batch holds no more
 * level-1 blocks than dmu_free_long_range() puts in one
 * transaction.
 */
static uint_t
dsl_clonedup_batch_max(dsl_clonedup_t *dcl)
{
	uint64_t want = MAX(1, zfs_clonedup_apply_blocks_per_txg);
	uint64_t ind = DMU_MAX_ACCESS >> (DN_MAX_INDBLKSHIFT + 1);
	uint64_t room;

	room = MIN(zfs_dirty_data_max / 4,
	    dsl_pool_adjustedsize(dcl->dcl_dp,
	    ZFS_SPACE_CHECK_NORMAL) / 64) /
	    brt_sync_dirty_est(dcl->dcl_dp->dp_spa);
	room = MIN(MAX(DCL_BATCH_MIN, room), ind);
	return ((uint_t)MIN(want, room));
}

static void
dsl_clonedup_batch_init(dsl_clonedup_t *dcl, dsl_clonedup_worker_t *w)
{
	dsl_clonedup_batch_t *b = &w->dcw_batch;
	uint_t nb;

	b->dcb_max = dsl_clonedup_batch_max(dcl);
	b->dcb_count = 0;
	b->dcb_ndst = 0;
	b->dcb_pend = vmem_alloc(sizeof (*b->dcb_pend) * b->dcb_max,
	    KM_SLEEP);
	b->dcb_dsts = vmem_alloc(sizeof (*b->dcb_dsts) * b->dcb_max,
	    KM_SLEEP);
	b->dcb_dstobj = vmem_alloc(sizeof (*b->dcb_dstobj) *
	    b->dcb_max, KM_SLEEP);
	nb = 64;
	while (nb < b->dcb_max)
		nb <<= 1;
	b->dcb_dstmask = nb - 1;
	b->dcb_dsthash = vmem_zalloc(sizeof (*b->dcb_dsthash) * nb,
	    KM_SLEEP);
	b->dcb_dstnext = vmem_alloc(sizeof (*b->dcb_dstnext) *
	    b->dcb_max, KM_SLEEP);
}

static void
dsl_clonedup_batch_fini(dsl_clonedup_worker_t *w)
{
	dsl_clonedup_batch_t *b = &w->dcw_batch;

	if (b->dcb_pend != NULL)
		vmem_free(b->dcb_pend,
		    sizeof (*b->dcb_pend) * b->dcb_max);
	if (b->dcb_dsts != NULL)
		vmem_free(b->dcb_dsts,
		    sizeof (*b->dcb_dsts) * b->dcb_max);
	if (b->dcb_dstobj != NULL)
		vmem_free(b->dcb_dstobj,
		    sizeof (*b->dcb_dstobj) * b->dcb_max);
	if (b->dcb_dsthash != NULL)
		vmem_free(b->dcb_dsthash,
		    sizeof (*b->dcb_dsthash) * (b->dcb_dstmask + 1));
	if (b->dcb_dstnext != NULL)
		vmem_free(b->dcb_dstnext,
		    sizeof (*b->dcb_dstnext) * b->dcb_max);
	b->dcb_pend = NULL;
	b->dcb_dsts = NULL;
	b->dcb_dstobj = NULL;
	b->dcb_dsthash = NULL;
	b->dcb_dstnext = NULL;
	b->dcb_dstmask = 0;
	b->dcb_max = 0;
	b->dcb_count = 0;
	b->dcb_ndst = 0;
}

/*
 * Whether a snapshot of os has passed its check, which sets
 * ds_trysnap_txg in open context, and may still sync in a txg before
 * txg.  Once on disk such a snapshot holds the old blocks of os that
 * a transaction in txg replaces.  The check runs in a transaction
 * that holds its txg open, so the answer is complete for a txg only
 * once that txg has quiesced.  A caller that needs txg - 1 covered
 * first sees spa_syncing_txg() reach it, and the barrier keeps that
 * read ahead of this one.
 */
static boolean_t
dsl_clonedup_snap_pending(objset_t *os, uint64_t txg)
{
	uint64_t snap;

	membar_consumer();
	snap = dmu_objset_ds(os)->ds_trysnap_txg;
	return (snap < txg &&
	    snap > spa_last_synced_txg(dmu_objset_spa(os)));
}

/*
 * Whether the head of ds's dsl_dir has a snapshot and nothing written
 * since its latest one, as dsl_dataset_modified_since_snap() tells
 * zfs receive.  ds is the head or one of its snapshots, and the
 * caller holds the pool's config lock.  Such a head is never a
 * destination.  All its data sits in that snapshot, so a redirect
 * there would write past the snapshot, grow USED and free nothing.
 * Its blocks still serve as sources, as those of a clonedup=source
 * dataset do.  A clone with no snapshot of its own compares with
 * its origin.  Any other head with no snapshot of its own has
 * $ORIGIN@$ORIGIN as its previous one, which has no objset to
 * compare, so such a head counts as changed.
 */
static boolean_t
dsl_clonedup_unchanged(dsl_dataset_t *ds)
{
	dsl_pool_t *dp = ds->ds_dir->dd_pool;
	dsl_dataset_t *head = ds;
	boolean_t unchanged;

	ASSERT(dsl_pool_config_held(dp));
	if (ds->ds_is_snapshot && dsl_dataset_hold_obj(dp,
	    dsl_dir_phys(ds->ds_dir)->dd_head_dataset_obj, FTAG,
	    &head) != 0)
		return (B_FALSE);
	unchanged = head->ds_prev != NULL &&
	    head->ds_prev != dp->dp_origin_snap &&
	    !dsl_dataset_modified_since_snap(head, head->ds_prev);
	if (head != ds)
		dsl_dataset_rele(head, FTAG);
	return (unchanged);
}

/*
 * dsl_clonedup_unchanged() for the walk, which visits every snapshot
 * of a dsl_dir and needs the same answer for each.  The walk runs in
 * sync pass 1, after the txg's datasets and sync tasks, so the answer
 * holds until the next txg.  The walk keeps it per dsl_dir until
 * then, as dsl_clonedup_ds_info() keeps a dataset's facts.
 */
boolean_t
dsl_clonedup_walk_unchanged(dsl_dataset_t *ds)
{
	dsl_pool_t *dp = ds->ds_dir->dd_pool;
	dsl_clonedup_t *dcl = dp->dp_clonedup;
	uint64_t txg = spa_syncing_txg(dp->dp_spa);
	dsl_clonedup_unch_t probe;
	dsl_clonedup_unch_t *u;
	avl_index_t where;
	boolean_t unchanged;

	if (dcl->dcl_unch_txg != txg) {
		dsl_clonedup_unch_vacate(dcl);
		dcl->dcl_unch_txg = txg;
	}
	probe.dcn_dir = ds->ds_dir->dd_object;
	u = avl_find(&dcl->dcl_unch, &probe, &where);
	if (u != NULL)
		return (u->dcn_unchanged);
	unchanged = dsl_clonedup_unchanged(ds);
	if (avl_numnodes(&dcl->dcl_unch) < DCL_DSINFO_MAX) {
		u = kmem_alloc(sizeof (*u), KM_SLEEP);
		u->dcn_dir = probe.dcn_dir;
		u->dcn_unchanged = unchanged;
		avl_insert(&dcl->dcl_unch, u, where);
	}
	return (unchanged);
}

/*
 * A redirect of a block that a snapshot also holds leaves the old
 * block on the head's deadlist and charges the clone to the head, so
 * USED grows by the block in the head's dsl_dir and in every
 * ancestor.  The referenced bytes, the owner's usage and the pool's
 * free space do not change.  A clone that zfs_clone_range() makes
 * over such a block is charged the same way.  dmu_tx_count_clone()
 * holds only the indirect blocks, and zfs_clone_range() refuses only
 * an owner already over its quota, so dmu_tx_assign() never sees the
 * data.  Each quota on the way up therefore needs room for the
 * growth, the deflated size that dsl_dataset_block_born() charges.
 * The room is the quota less the dsl_dir's usage on disk and less
 * what the run booked in txgs that have not synced.  dcl_lock keeps
 * two workers from taking the same room, and dsl_dir_willuse_space()
 * shows the growth to other writers until it syncs.
 */
static boolean_t
dsl_clonedup_quota_take(dsl_clonedup_t *dcl, objset_t *os,
    const blkptr_t *bp, dmu_tx_t *tx)
{
	dsl_dir_t *dd = dmu_objset_ds(os)->ds_dir;
	uint64_t synced = spa_last_synced_txg(dcl->dcl_dp->dp_spa);
	uint64_t txg = dmu_tx_get_txg(tx);
	uint64_t room = UINT64_MAX;
	uint64_t held = 0;
	uint64_t grow;
	int t = txg & TXG_MASK;

	for (dsl_dir_t *d = dd; d != NULL; d = d->dd_parent) {
		uint64_t quota = dsl_dir_phys(d)->dd_quota;
		uint64_t used;

		if (quota == 0)
			continue;
		mutex_enter(&d->dd_lock);
		used = dsl_dir_phys(d)->dd_used_bytes;
		mutex_exit(&d->dd_lock);
		room = MIN(room, quota > used ? quota - used : 0);
	}
	if (room == UINT64_MAX)
		return (B_TRUE);

	grow = bp_get_dsize(dcl->dcl_dp->dp_spa, bp);
	mutex_enter(&dcl->dcl_lock);
	if (dcl->dcl_grow_txg[t] != txg) {
		dcl->dcl_grow_txg[t] = txg;
		dcl->dcl_grow[t] = 0;
	}
	for (int i = 0; i < TXG_SIZE; i++) {
		if (dcl->dcl_grow_txg[i] > synced)
			held += dcl->dcl_grow[i];
	}
	if (held + grow > room) {
		mutex_exit(&dcl->dcl_lock);
		return (B_FALSE);
	}
	dcl->dcl_grow[t] += grow;
	mutex_exit(&dcl->dcl_lock);
	dsl_dir_willuse_space(dd, grow, tx);
	return (B_TRUE);
}

/*
 * dmu_tx_assign() found a quota, or the pool, out of room.  Its test
 * leaves out the transaction's own reservation, so a smaller batch
 * would fail the same way, as would zfs_clone_range().  Skip the
 * clones, and mark them dropped the way a failed verify does, so the
 * release still unlocks them.
 */
static void
dsl_clonedup_batch_refuse(dsl_clonedup_worker_t *w,
    dsl_clonedup_stats_t *st)
{
	dsl_clonedup_batch_t *b = &w->dcw_batch;

	for (uint_t i = 0; i < b->dcb_count; i++) {
		dsl_clonedup_pend_t *pd = &b->dcb_pend[i];

		if (!pd->dcp_drop) {
			pd->dcp_drop = B_TRUE;
			st->s_policy++;
		}
	}
}

/* Bytes a dry run has booked against dd, see dsl_clonedup_dry_t. */
static uint64_t
dsl_clonedup_dry_booked(dsl_clonedup_t *dcl, dsl_dir_t *dd)
{
	dsl_clonedup_dry_t probe;
	dsl_clonedup_dry_t *d;

	ASSERT(MUTEX_HELD(&dcl->dcl_lock));
	dsl_clonedup_dry_key(&probe, DCD_DIR, dd->dd_object, 0, 0);
	d = avl_find(&dcl->dcl_dry, &probe, NULL);
	return (d != NULL ? d->dcd_grow : 0);
}

/*
 * Whether dmu_tx_assign() would find no room for a batch of clones
 * into os.  A dry run assigns no tx, so this makes the test
 * dsl_dir_tempreserve_space() makes on a quiet pool.  A refquota, or
 * a quota on the way up, is already reached, or the pool's unreserved
 * space is used up, with the growth the run booked counted as used.
 * A real run then skips the batch's clones, see
 * dsl_clonedup_batch_refuse().
 */
static boolean_t
dsl_clonedup_dry_full(dsl_clonedup_t *dcl, objset_t *os)
{
	dsl_dataset_t *ds = dmu_objset_ds(os);
	uint64_t used;
	uint64_t rsrv;

	if (dsl_dataset_check_quota(ds, B_TRUE, 1, 0, &used,
	    &rsrv) == EDQUOT)
		return (B_TRUE);
	for (dsl_dir_t *d = ds->ds_dir; d != NULL;
	    d = d->dd_parent) {
		uint64_t quota = dsl_dir_phys(d)->dd_quota;

		if (d != ds->ds_dir) {
			mutex_enter(&d->dd_lock);
			used = dsl_dir_phys(d)->dd_used_bytes;
			mutex_exit(&d->dd_lock);
		}
		mutex_enter(&dcl->dcl_lock);
		used += dsl_clonedup_dry_booked(dcl, d);
		mutex_exit(&dcl->dcl_lock);
		if (d->dd_parent == NULL) {
			uint64_t avail = dsl_pool_unreserved_space(
			    dcl->dcl_dp, ZFS_SPACE_CHECK_NORMAL);

			if (quota == 0 || avail < quota)
				quota = avail;
		}
		if (quota != 0 && used >= quota)
			return (B_TRUE);
		/* a reservation keeps the parent out of it */
		if ((d == ds->ds_dir && rsrv != 0) ||
		    dsl_dir_phys(d)->dd_reserved > used)
			break;
	}
	return (B_FALSE);
}

/*
 * dsl_clonedup_quota_take() for a dry run, which has no tx.  The
 * growth is booked against every dsl_dir on the way up, so the room
 * later redirects and dsl_clonedup_dry_full() see has it taken out.
 * A dsl_dir booked for the first time gets its node with dcl_lock
 * dropped, and the room is measured again after that.
 */
static boolean_t
dsl_clonedup_dry_take(dsl_clonedup_t *dcl, dsl_clonedup_worker_t *w,
    objset_t *os, const blkptr_t *bp)
{
	dsl_dir_t *dd = dmu_objset_ds(os)->ds_dir;
	uint64_t grow = bp_get_dsize(dcl->dcl_dp->dp_spa, bp);
	dsl_clonedup_dry_t *spare = NULL;
	dsl_clonedup_dry_t probe;
	dsl_clonedup_dry_t *d;
	avl_index_t where;
	dsl_dir_t *p;
	boolean_t ok;

	mutex_enter(&dcl->dcl_lock);
	for (;;) {
		uint64_t room = UINT64_MAX;

		ok = w->dcw_gen == dcl->dcl_gen;
		for (p = dd; ok && p != NULL; p = p->dd_parent) {
			uint64_t quota = dsl_dir_phys(p)->dd_quota;
			uint64_t used;

			if (quota == 0)
				continue;
			mutex_enter(&p->dd_lock);
			used = dsl_dir_phys(p)->dd_used_bytes;
			mutex_exit(&p->dd_lock);
			used += dsl_clonedup_dry_booked(dcl, p);
			room = MIN(room,
			    quota > used ? quota - used : 0);
		}
		ok = ok && grow <= room;
		for (p = dd; ok && p != NULL; p = p->dd_parent) {
			dsl_clonedup_dry_key(&probe, DCD_DIR,
			    p->dd_object, 0, 0);
			if (avl_find(&dcl->dcl_dry, &probe,
			    &where) == NULL)
				break;
		}
		if (!ok || p == NULL)
			break;
		if (spare != NULL) {
			memcpy(spare->dcd_key, probe.dcd_key,
			    sizeof (probe.dcd_key));
			avl_insert(&dcl->dcl_dry, spare, where);
			spare = NULL;
			continue;
		}
		mutex_exit(&dcl->dcl_lock);
		spare = kmem_zalloc(sizeof (*spare), KM_SLEEP);
		mutex_enter(&dcl->dcl_lock);
	}
	for (p = dd; ok && p != NULL; p = p->dd_parent) {
		dsl_clonedup_dry_key(&probe, DCD_DIR, p->dd_object,
		    0, 0);
		/* a rename can have moved dd since the loop above */
		d = avl_find(&dcl->dcl_dry, &probe, NULL);
		if (d != NULL)
			d->dcd_grow += grow;
	}
	mutex_exit(&dcl->dcl_lock);
	if (spare != NULL)
		kmem_free(spare, sizeof (*spare));
	return (ok);
}

/*
 * Check and finish, in tx, every pair the verify kept.  A dry run
 * passes no tx.  It makes every check a real run makes here, counts
 * what a real run would finish, and writes nothing.
 */
static void
dsl_clonedup_batch_finish(dsl_clonedup_t *dcl,
    dsl_clonedup_worker_t *w, uint_t n, dmu_tx_t *tx,
    dsl_clonedup_stats_t *st)
{
	dsl_clonedup_batch_t *b = &w->dcw_batch;
	/*
	 * The apply hands destinations that share a source to the
	 * batch together, so read and check that source once for the
	 * run of them instead of once per destination.  They are all
	 * in this transaction, so nothing can change between them.
	 */
	blkptr_t sval;
	zfs_clonedup_result_t sres = ZCR_ERROR;
	uint64_t sobj = 0;
	uint64_t sblkid = 0;
	boolean_t shave = B_FALSE;
	boolean_t room;
	uint64_t prev_snap;
	boolean_t unchanged;
	int err = 0;

	/*
	 * Whether a snapshot holds a destination's old block, decided
	 * as dsl_dataset_block_kill() will decide it, from the
	 * dataset's last snapshot as it is now.  The walk's answer
	 * misses a snapshot taken since, and keeps one destroyed
	 * since.  A run has waited for every snapshot that has passed
	 * its check before its txg, see dsl_clonedup_batch_flush(),
	 * and a dry run takes the answer as it stands.
	 */
	prev_snap = dsl_dataset_phys(
	    dmu_objset_ds(b->dcb_os))->ds_prev_snap_txg;
	/*
	 * The walk keeps a head out of the destinations when nothing
	 * was written to it since its latest snapshot, see
	 * dsl_clonedup_unchanged().  A snapshot taken since the walk,
	 * with nothing written after it, leaves a head so too, and
	 * each of its pairs counts as a policy skip.  A run decides
	 * it after the same wait as the test above, and a dry run
	 * takes the answer as it stands.
	 */
	dsl_pool_config_enter(dcl->dcl_dp, FTAG);
	unchanged = dsl_clonedup_unchanged(dmu_objset_ds(b->dcb_os));
	dsl_pool_config_exit(dcl->dcl_dp, FTAG);
	for (uint_t i = 0; i < n; i++) {
		dsl_clonedup_pend_t *pd = &b->dcb_pend[i];
		zfs_clonedup_result_t res;
		boolean_t held;

		if (pd->dcp_drop)
			continue;
		if (unchanged) {
			st->s_policy++;
			continue;
		}
		held = BP_GET_BIRTH(&pd->dcp_dbp) <= prev_snap;
		if (!shave || pd->dcp_sobject != sobj ||
		    pd->dcp_sblkid != sblkid) {
			err = zfs_clonedup_src_validate(b->dcb_sos,
			    pd->dcp_sobject, pd->dcp_sblkid,
			    &pd->dcp_sbp, &sval, &sres);
			if (err != 0)
				sres = ZCR_ERROR;
			sobj = pd->dcp_sobject;
			sblkid = pd->dcp_sblkid;
			shave = B_TRUE;
		}
		if (sres == ZCR_APPLIED && held) {
			room = tx != NULL ?
			    dsl_clonedup_quota_take(dcl, b->dcb_os,
			    &sval, tx) :
			    dsl_clonedup_dry_take(dcl, w, b->dcb_os,
			    &sval);
			if (!room) {
				st->s_policy++;
				continue;
			}
		}
		if (sres != ZCR_APPLIED) {
			res = sres;
		} else {
			err = zfs_clonedup_dst_finish(pd->dcp_dst,
			    pd->dcp_blkid, &pd->dcp_dbp, b->dcb_sos,
			    pd->dcp_sobject, pd->dcp_sblkid,
			    &pd->dcp_sbp, &sval, tx, &res);
		}
		switch (res) {
		case ZCR_APPLIED:
			st->s_applied++;
			if (held)
				st->s_saved_snapheld +=
				    pd->dcp_dasize;
			else
				st->s_saved += pd->dcp_dasize;
			if (tx != NULL)
				DCL_BUMP(dcl, DCK_CLONES, 1);
			break;
		case ZCR_SRC_STALE:
		case ZCR_DST_STALE:
			st->s_stale++;
			break;
		case ZCR_SRC_DIRTY:
		case ZCR_DST_DIRTY:
			st->s_dirty++;
			break;
		default:
			st->s_errors++;
			break;
		}
		if (res != ZCR_APPLIED) {
			dsl_clonedup_loc_t sl = { b->dcb_srcobj,
			    pd->dcp_sobject, pd->dcp_sblkid };

			dsl_clonedup_dbg(dcl, "apply", b->dcb_dsobj,
			    pd->dcp_dobject, pd->dcp_blkid, &sl, err,
			    res);
		}
	}
}

/*
 * Send every queued clone out in one transaction.  The wait for the
 * previous txg, which keeps a writer that still holds it from freeing
 * a source under us, is paid once per batch.  A source no writer can
 * reach skips it, unless the destination is an open volume, see
 * zfs_clonedup_zvol_recheck(), or a snapshot of it may still sync
 * first, see dsl_clonedup_batch_finish().  A snapshot passes its
 * check in a transaction, which can hold the previous txg open after
 * ours has opened.  So the skip also needs that txg to have begun to
 * sync.  Anything that has moved is dropped; the next run finds the
 * block again.
 */
static void
dsl_clonedup_batch_flush(dsl_clonedup_t *dcl,
    dsl_clonedup_worker_t *w)
{
	dsl_clonedup_batch_t *b = &w->dcw_batch;
	dsl_clonedup_stats_t st = { 0 };
	dsl_pool_t *dp = dcl->dcl_dp;
	uint_t n = b->dcb_count;
	uint64_t txg = 0;
	dmu_tx_t *tx;
	int err;

	if (n == 0)
		return;
	ASSERT3P(b->dcb_sos, ==, w->dcw_src_os);
	if (dsl_clonedup_stale(dcl, w))
		goto release;

	dsl_clonedup_batch_verify(dcl, w, &st);
	n = b->dcb_count;
	if (n == 0)
		goto release;
	if (b->dcb_dryrun) {
		if (dsl_clonedup_dry_full(dcl, b->dcb_os))
			dsl_clonedup_batch_refuse(w, &st);
		dsl_clonedup_batch_finish(dcl, w, n, NULL, &st);
		goto release;
	}

	for (;;) {
		uint_t live = 0;

		tx = dmu_tx_create(b->dcb_os);
		for (uint_t i = 0; i < n; i++) {
			dsl_clonedup_pend_t *pd = &b->dcb_pend[i];

			if (pd->dcp_drop)
				continue;
			zfs_clonedup_dst_tx_hold(tx, pd->dcp_dst,
			    pd->dcp_blkid, pd->dcp_blksz);
			live++;
		}
		err = dmu_tx_assign(tx, DMU_TX_WAIT);
		if (err == 0) {
			if (dsl_clonedup_apply_paced(tx, live))
				break;
			continue;
		}
		dmu_tx_abort(tx);
		if (err != EDQUOT && err != ENOSPC)
			st.s_errors += live;
		else
			dsl_clonedup_batch_refuse(w, &st);
		goto release;
	}
	txg = dmu_tx_get_txg(tx);
	if (!b->dcb_src_stable ||
	    spa_syncing_txg(dp->dp_spa) < txg - 1 ||
	    dsl_clonedup_snap_pending(b->dcb_os, txg) ||
	    zfs_clonedup_dst_kind(b->dcb_pend[0].dcp_dst) ==
	    DCK_DST_ZVOL) {
		err = txg_wait_synced_flags(dp, txg - 1,
		    TXG_WAIT_SUSPEND);
		if (err != 0) {
			dmu_tx_commit(tx);
			st.s_errors += n;
			goto release;
		}
	}
	dsl_clonedup_batch_finish(dcl, w, n, tx, &st);
	dmu_tx_commit(tx);
	DCL_BUMP(dcl, DCK_BATCHES, 1);
release:
	for (uint_t i = 0; i < n; i++) {
		zfs_clonedup_dst_unlock(b->dcb_pend[i].dcp_lock);
		zfs_clonedup_src_unlock(b->dcb_pend[i].dcp_slock);
	}
	for (uint_t i = 0; i < b->dcb_ndst; i++)
		zfs_clonedup_dst_close(b->dcb_dsts[i]);
	dsl_clonedup_src_unpin(w);
	if (b->dcb_ndst != 0) {
		memset(b->dcb_dsthash, 0,
		    sizeof (*b->dcb_dsthash) * (b->dcb_dstmask + 1));
	}
	b->dcb_ndst = 0;
	b->dcb_count = 0;
	dsl_clonedup_stats_fold(dcl, w, &st);
}

/*
 * Whether a clone of sbp over dbp keeps every copy of dbp's data.
 * The group key holds the DVA count, and the DVAs of a gang block are
 * those of its gang header, which can have one copy more than the
 * data.  A block that is not a gang block holds its data in every
 * DVA, so it has as many copies as any block of its group.  A gang
 * block may have fewer, so it serves only a block with one copy of
 * its data: one with a single DVA, or a gang block whose DVA[1] holds
 * a gang header alone, which its asize shows.
 */
static boolean_t
dsl_clonedup_copies_kept(dsl_clonedup_t *dcl, const blkptr_t *sbp,
    const blkptr_t *dbp)
{
	spa_t *spa = dcl->dcl_dp->dp_spa;
	const dva_t *dva = &dbp->blk_dva[1];
	boolean_t one;
	vdev_t *vd;

	if (!BP_IS_GANG(sbp) || BP_GET_NDVAS(dbp) == 1)
		return (B_TRUE);
	if (!BP_IS_GANG(dbp))
		return (B_FALSE);
	spa_config_enter(spa, SCL_VDEV, FTAG, RW_READER);
	vd = vdev_lookup_top(spa, DVA_GET_VDEV(dva));
	one = vd != NULL &&
	    DVA_GET_ASIZE(dva) == vdev_gang_header_asize(vd);
	spa_config_exit(spa, SCL_VDEV, FTAG);
	return (one);
}

/*
 * Take the destination range lock and queue the block.  The lock is
 * held until the batch commits, so the destination cannot move under
 * the clone and no transaction is assigned before it is taken.  A
 * batch that already holds locks never waits for another.  EAGAIN
 * asks the caller to flush the batch and try the block again.
 */
static int
dsl_clonedup_batch_add(dsl_clonedup_t *dcl, dsl_clonedup_worker_t *w,
    zfs_clonedup_dst_t *dst, uint64_t dsobj, uint64_t srcobj,
    objset_t *sos, boolean_t stable, uint64_t object, uint64_t blkid,
    uint64_t blksz, const blkptr_t *dexp, const blkptr_t *sexp,
    uint64_t sobject, uint64_t sblkid, uint64_t dasize,
    boolean_t trusted, boolean_t dryrun, zfs_clonedup_result_t *resp)
{
	dsl_clonedup_batch_t *b = &w->dcw_batch;
	dsl_clonedup_pend_t *pd;
	boolean_t ready = B_FALSE;
	void *lock = NULL;
	void *slock = NULL;
	int err;

	if (b->dcb_count == 0 && w->dcw_spin == NULL) {
		err = zfs_clonedup_src_pin(sos, dst, &w->dcw_spin);
		if (err != 0 && (err != EBUSY || !trusted))
			return (err);
	}
	/* a refused pair never takes the range lock */
	err = zfs_clonedup_dst_clone_check(dst, sos);
	if (err == 0 && !dsl_clonedup_copies_kept(dcl, sexp, dexp))
		err = SET_ERROR(EXDEV);
	if (err != 0)
		return (err);
	err = zfs_clonedup_dst_prepare(dst, blkid, dexp,
	    b->dcb_count > 0, dryrun, &ready, &lock, resp);
	if (err != 0 || !ready)
		return (err);
	if (!trusted) {
		err = zfs_clonedup_src_lock(w->dcw_spin, dst, lock,
		    sos, sobject, sblkid, sexp, b->dcb_count > 0,
		    &slock, resp);
		if (err != 0 || *resp != ZCR_QUEUED) {
			zfs_clonedup_dst_unlock(lock);
			if (err == EAGAIN && b->dcb_count == 0 &&
			    zfs_clonedup_src_lock(w->dcw_spin, dst,
			    NULL, sos, sobject, sblkid, sexp, B_FALSE,
			    &slock, resp) == 0)
				zfs_clonedup_src_unlock(slock);
			return (err);
		}
	}

	if (b->dcb_count == 0) {
		b->dcb_dsobj = dsobj;
		b->dcb_srcobj = srcobj;
		b->dcb_os = zfs_clonedup_dst_objset(dst);
		b->dcb_sos = sos;
		b->dcb_src_stable = stable;
		b->dcb_dryrun = dryrun;
	}
	pd = &b->dcb_pend[b->dcb_count++];
	pd->dcp_dst = dst;
	pd->dcp_lock = lock;
	pd->dcp_slock = slock;
	pd->dcp_blkid = blkid;
	pd->dcp_blksz = blksz;
	pd->dcp_dobject = object;
	pd->dcp_dbp = *dexp;
	pd->dcp_sobject = sobject;
	pd->dcp_sblkid = sblkid;
	pd->dcp_sbp = *sexp;
	pd->dcp_dasize = dasize;
	pd->dcp_trusted = trusted;
	pd->dcp_drop = B_FALSE;
	pd->dcp_sabd = pd->dcp_dabd = NULL;
	*resp = ZCR_QUEUED;

	if (b->dcb_count >= b->dcb_max)
		dsl_clonedup_batch_flush(dcl, w);
	return (0);
}

/*
 * The scan saw this block, but the object may have been truncated or
 * rewritten smaller since.  dmu_read_l0_bps() does not tolerate an
 * offset past the end of an object: it reaches zfs_panic_recover(),
 * which panics on a default system.  Bound every read by what the
 * object holds now, sources and destinations alike: a file rewritten
 * smaller keeps a length the destination checks pass, while its one
 * block is no longer the size the scan recorded.
 */
int
dsl_clonedup_src_in_range(objset_t *os, uint64_t object,
    uint64_t blkid, uint64_t blksz)
{
	dmu_object_info_t doi;
	int err;

	err = dmu_object_info(os, object, &doi);
	if (err != 0)
		return (err);
	if (doi.doi_data_block_size != blksz ||
	    (blkid + 1) * blksz > doi.doi_max_offset)
		return (SET_ERROR(ESTALE));
	return (0);
}

static void
dsl_clonedup_group_process(dsl_clonedup_t *dcl,
    dsl_clonedup_worker_t *w, dsl_clonedup_group_t *g,
    boolean_t dryrun, dsl_clonedup_stats_t *acc, zthr_t *zthr)
{
	dsl_clonedup_stats_t st = { 0 };
	dsl_clonedup_src_t src;
	objset_t *sos;
	blkptr_t sbp;
	blkptr_t dbp;
	uint64_t sblksz;
	dsl_clonedup_dsinfo_t sdi;
	boolean_t src_stable = B_FALSE;
	boolean_t counted = g->g_counted;
	boolean_t trusted;
	uint_t start = 0;
	int tier = 0;
	int used;
	size_t nb;
	int lost = 0;
	int err;

again:
	if (start >= g->g_count)
		goto out;
	used = dsl_clonedup_group_choose_src(w, g, start, tier, &src);
	sblksz = (uint64_t)src.dcs_blkszsec << SPA_MINBLOCKSHIFT;

	if (w->dcw_batch.dcb_count > 0 &&
	    w->dcw_batch.dcb_srcobj != src.dcs_objset)
		dsl_clonedup_batch_flush(dcl, w);
	err = dsl_clonedup_src_hold(dcl, w, src.dcs_objset, &sos);
	if (err == ERESTART) {
		dsl_clonedup_yield_release(dcl, w);
		goto again;
	}
	if (err == 0)
		err = dsl_clonedup_src_in_range(sos, src.dcs_object,
		    src.dcs_blkid, sblksz);
	if (err == 0) {
		nb = 1;
		err = dmu_read_l0_bps(sos, src.dcs_object,
		    src.dcs_blkid * sblksz, sblksz, &sbp, &nb);
		if (err == 0 && (nb != 1 ||
		    !dsl_clonedup_bp_matches(&sbp, &src.dcs_dva,
		    src.dcs_birth)))
			err = SET_ERROR(ESTALE);
	}
	if (err != 0) {
		/* try the next tier, then the next entry */
		w->dcw_group_src_valid = B_FALSE;
		if (used < 2) {
			lost = err;
			tier = used + 1;
			goto again;
		}
		dsl_clonedup_group_drop_src(g, start, &src);
		if (err == EAGAIN)
			st.s_dirty++;
		else if (err == EINTR)
			st.s_busy++;
		else if (dsl_clonedup_err_moved(err))
			st.s_stale++;
		else
			st.s_errors++;
		if (++start < g->g_count)
			goto again;
		goto out;
	}
	src_stable = dsl_clonedup_ds_info(dcl, w, src.dcs_objset,
	    &sdi) == 0 && sdi.di_snapshot;

	for (uint_t i = start; i < g->g_count &&
	    !dsl_clonedup_stale(dcl, w);
	    i++) {
		dsl_clonedup_entry_t *e = g->g_entries[i];
		uint64_t dasize = 0;
		uint64_t dblksz =
		    (uint64_t)e->dce_blkszsec << SPA_MINBLOCKSHIFT;
		uint64_t dsobj = e->dce_objset;
		/*
		 * Mapped from a snapshot to its head.  A head that no
		 * longer holds the snapshot's block has nothing to
		 * redirect, so the pair is no candidate and the apply
		 * does not count it as stale.
		 */
		boolean_t insnap = B_FALSE;
		dsl_clonedup_dsinfo_t di;
		zfs_clonedup_dst_t *dst;
		zfs_clonedup_result_t res;

redo:
		/* count what a stop leaves as skipped */
		if (dsl_clonedup_stop(dcl, zthr)) {
			if (e->dce_flags & DCE_F_SRCONLY)
				continue;
			if (!DVA_EQUAL(&e->dce_dva, &src.dcs_dva) ||
			    e->dce_birth != src.dcs_birth)
				st.s_busy++;
			else if (lost != 0)
				g->g_lost = lost;
			continue;
		}
		if (dsl_clonedup_yield_pending(dcl) ||
		    w->dcw_src_ds == NULL) {
			dsl_clonedup_batch_flush(dcl, w);
			dsl_clonedup_yield_release(dcl, w);
			err = dsl_clonedup_src_hold(dcl, w,
			    src.dcs_objset, &sos);
			if (err == ERESTART) {
				dsl_clonedup_yield_release(dcl, w);
				goto redo;
			}
			if (err != 0) {
				/* source dataset gone: re-pick */
				w->dcw_group_src_valid = B_FALSE;
				lost = err;
				start = i;
				tier = 2;
				goto again;
			}
		}
		if (e->dce_flags & DCE_F_SRCONLY)
			continue;
		if (DVA_EQUAL(&e->dce_dva, &src.dcs_dva) &&
		    e->dce_birth == src.dcs_birth) {
			if (lost != 0)
				g->g_lost = lost;
			continue;
		}
		/*
		 * The order pass keeps such a copy out of the apply
		 * tree.  So src replaced a stale source, and e counts
		 * as a copy whose source failed.
		 */
		if (dsl_clonedup_src_keeps(&src, e)) {
			g->g_lost = SET_ERROR(ESTALE);
			continue;
		}
		if (dsl_clonedup_ds_info(dcl, w, dsobj, &di) != 0) {
			st.s_stale++;
			continue;
		}
		if (e->dce_flags & DCE_F_SNAPHELD) {
			if (di.di_snapshot) {
				dsobj = di.di_head;
				insnap = B_TRUE;
				if (dsl_clonedup_ds_info(dcl, w,
				    dsobj, &di) != 0) {
					st.s_stale++;
					continue;
				}
			}
		}
		if (di.di_snapshot || di.di_mode != ZFS_CLONEDUP_ON ||
		    di.di_inconsistent) {
			st.s_policy++;
			continue;
		}

		err = dsl_clonedup_dst_hold(dcl, w, dsobj,
		    e->dce_object, &dst);
		if (err == ERESTART) {
			dsl_clonedup_yield_release(dcl, w);
			goto redo;
		}
		/* EINTR is a stop, counted as busy as at redo */
		if (err == EBUSY || err == EINTR) {
			st.s_busy++;
			continue;
		}
		if (err == EROFS || err == EPERM) {
			st.s_policy++;
			continue;
		}
		if (err != 0 && !dsl_clonedup_err_moved(err)) {
			st.s_errors++;
			continue;
		}
		if (err != 0) {
			if (!insnap)
				st.s_stale++;
			continue;
		}
		if (!zfs_clonedup_dst_exclusive(dst)) {
			mutex_enter(&dcl->dcl_lock);
			w->dcw_claim_dst = 0;
			mutex_exit(&dcl->dcl_lock);
		}
		if (w->dcw_src_ds == NULL) {
			err = dsl_clonedup_src_hold(dcl, w,
			    src.dcs_objset, &sos);
			if (err == ERESTART) {
				dsl_clonedup_yield_release(dcl, w);
				goto redo;
			}
			if (err != 0) {
				w->dcw_group_src_valid = B_FALSE;
				lost = err;
				start = i;
				tier = 2;
				goto again;
			}
		}

		nb = 1;
		err = dsl_clonedup_src_in_range(
		    zfs_clonedup_dst_objset(dst), e->dce_object,
		    e->dce_blkid, dblksz);
		if (err != 0) {
			if (!dsl_clonedup_err_moved(err))
				st.s_errors++;
			else if (!insnap)
				st.s_stale++;
			continue;
		}
		err = dmu_read_l0_bps(zfs_clonedup_dst_objset(dst),
		    e->dce_object, e->dce_blkid * dblksz, dblksz,
		    &dbp, &nb);
		if (err == EAGAIN) {
			st.s_dirty++;
			continue;
		}
		if (err != 0 && !dsl_clonedup_err_moved(err)) {
			st.s_errors++;
			continue;
		}
		if (err != 0 || nb != 1 ||
		    !dsl_clonedup_bp_matches(&dbp, &e->dce_dva,
		    e->dce_birth)) {
			if (!insnap)
				st.s_stale++;
			continue;
		}
		if (zfs_dedupe_bp_same_block(&dbp, &sbp))
			continue;
		/* the first destination with work counts the group */
		g->g_live = B_TRUE;
		if (!counted) {
			st.s_groups++;
			counted = B_TRUE;
		}
		/*
		 * The clone installs the source bp whole.  Both
		 * pointers are the blocks the walk saw, and the group
		 * key holds their DVA count, so the counts match.
		 */
		ASSERT3U(BP_GET_NDVAS(&sbp), ==, BP_GET_NDVAS(&dbp));
		/*
		 * A redirect frees what the destination block
		 * occupies on disk across all its copies, not the
		 * record size it presents.  On a compressed dataset
		 * the two differ by the compression ratio.  A block
		 * that several references share counts once, at the
		 * first of them.
		 */
		if (!(e->dce_flags & DCE_F_REPEAT))
			dasize = BP_GET_ASIZE(&dbp);
		trusted = zfs_dedupe_bp_provably_equal(&sbp, &dbp);

		if (zfs_clonedup_apply_txg_delay != 0)
			delay(MSEC_TO_TICK(
			    zfs_clonedup_apply_txg_delay));

		err = dsl_clonedup_batch_add(dcl, w, dst, dsobj,
		    src.dcs_objset, sos, src_stable, e->dce_object,
		    e->dce_blkid, dblksz,
		    &dbp, &sbp, src.dcs_object, src.dcs_blkid, dasize,
		    trusted, dryrun, &res);
		if (err == EAGAIN) {
			dsl_clonedup_batch_flush(dcl, w);
			goto redo;
		}
		if (err == EDEADLK) {
			dsl_clonedup_dst_rele(dcl, w);
			err = zfs_clonedup_src_pin(sos, NULL,
			    &w->dcw_spin);
			if (err == 0) {
				w->dcw_spin_for = dsobj;
				goto redo;
			}
		}
		if (err == EBUSY) {
			st.s_busy++;
			continue;
		}
		if (err == EPERM || err == EROFS || err == EDQUOT ||
		    err == EXDEV) {
			st.s_policy++;
			continue;
		}
		if (err != 0) {
			st.s_errors++;
			continue;
		}
		switch (res) {
		case ZCR_QUEUED:
			break;
		case ZCR_SRC_STALE:
		case ZCR_DST_STALE:
			st.s_stale++;
			break;
		case ZCR_SRC_DIRTY:
		case ZCR_DST_DIRTY:
			st.s_dirty++;
			break;
		default:
			st.s_errors++;
			break;
		}
		if (res != ZCR_QUEUED) {
			dsl_clonedup_loc_t sl = { src.dcs_objset,
			    src.dcs_object, src.dcs_blkid };

			dsl_clonedup_dbg(dcl, "apply", dsobj,
			    e->dce_object, e->dce_blkid, &sl, err,
			    res);
		}
	}

out:
	if (w->dcw_batch.dcb_count == 0) {
		dsl_clonedup_dst_rele(dcl, w);
		dsl_clonedup_src_unpin(w);
	}
	dsl_clonedup_stats_sum(acc, &st);
}

/* Does a neighbor of e in the apply tree have e's locator? */
static boolean_t
dsl_clonedup_loc_shared(avl_tree_t *t, dsl_clonedup_entry_t *e)
{
	dsl_clonedup_entry_t *n;

	for (int i = 0; i < 2; i++) {
		n = i == 0 ? AVL_PREV(t, e) : AVL_NEXT(t, e);
		if (n != NULL && n->dce_objset == e->dce_objset &&
		    n->dce_object == e->dce_object &&
		    n->dce_blkid == e->dce_blkid)
			return (B_TRUE);
	}
	return (B_FALSE);
}

/*
 * Decide one group's source and move its destinations into the apply
 * tree.  The choice costs no I/O: the older block from phase two if
 * the group has one and dsl_clonedup_src_serves() accepts it,
 * otherwise the best of the group's own copies.  A copy that
 * dsl_clonedup_src_keeps() names stays out of the apply tree.
 * It is not validated here.  The apply reads its bp, and a group
 * whose source has gone stale falls back to a live copy of its own,
 * see dsl_clonedup_apply_one().
 */
static void
dsl_clonedup_group_decide(dsl_clonedup_t *dcl,
    dsl_clonedup_worker_t *w, dsl_clonedup_group_t *g)
{
	dsl_clonedup_src_t src;

	/*
	 * A group larger than one take arrives once per take, and
	 * every take must reach the same source or each keeps a
	 * survivor of its own.  dsl_clonedup_group_choose_src()
	 * caches the last source by key and prop for that.  The cache
	 * is dropped at the end of the pass, not here.
	 */
	(void) dsl_clonedup_group_choose_src(w, g, 0, 0, &src);
	/* the best copy ranks this low only when every copy does */
	if (src.dcs_flags & DCE_F_REMOVED)
		return;

	for (uint_t i = 0; i < g->g_count; i++) {
		dsl_clonedup_entry_t *e = g->g_entries[i];

		if (e->dce_flags & DCE_F_SRCONLY)
			continue;
		if (DVA_EQUAL(&e->dce_dva, &src.dcs_dva) &&
		    e->dce_birth == src.dcs_birth)
			continue;
		if (dsl_clonedup_src_keeps(&src, e))
			continue;
		e->dce_src = kmem_alloc(sizeof (src), KM_SLEEP);
		*e->dce_src = src;
		mutex_enter(&dcl->dcl_lock);
		if (w->dcw_gen != dcl->dcl_gen) {
			mutex_exit(&dcl->dcl_lock);
			kmem_free(e->dce_src, sizeof (src));
			e->dce_src = NULL;
			break;
		}
		avl_add(&dcl->dcl_apply, e);
		if (dsl_clonedup_loc_shared(&dcl->dcl_apply, e))
			dcl->dcl_apply_shared++;
		dcl->dcl_nentries++;
		dcl->dcl_nsrc++;
		dcl->dcl_mem_used += sizeof (*e) + sizeof (src);
		dsl_clonedup_mem_note(dcl);
		mutex_exit(&dcl->dcl_lock);
		g->g_entries[i] = NULL;
	}
}

/*
 * Empty the checksum-ordered index into the apply tree.  Runs once,
 * before the first clone of the partition, and issues no I/O.
 */
static void
dsl_clonedup_order_index(dsl_clonedup_t *dcl,
    dsl_clonedup_worker_t *w, zthr_t *zthr)
{
	dsl_clonedup_group_t g;
	dsl_clonedup_entry_t **buf;
	uint_t bufmax = dsl_clonedup_group_max();

	/*
	 * One array serves every group this pass takes, instead of an
	 * 8 KiB allocation per group across the whole index.  Nothing
	 * here outlives the iteration.
	 */
	buf = kmem_alloc(bufmax * sizeof (*buf), KM_SLEEP);
	/* the pass sets the total when it is done */
	mutex_enter(&dcl->dcl_lock);
	if (w->dcw_gen == dcl->dcl_gen)
		dcl->dcl_apply_total = 0;
	mutex_exit(&dcl->dcl_lock);
	while (!dsl_clonedup_stop(dcl, zthr) &&
	    dsl_clonedup_group_take(dcl, w, &g, buf, bufmax)) {
		dsl_clonedup_group_decide(dcl, w, &g);
		dsl_clonedup_group_free(&g);
		mutex_enter(&dcl->dcl_lock);
		if (dcl->dcl_apply_busy > 0)
			dcl->dcl_apply_busy--;
		if (w->dcw_gen != dcl->dcl_gen) {
			mutex_exit(&dcl->dcl_lock);
			break;
		}
		mutex_exit(&dcl->dcl_lock);
	}
	/*
	 * A cancelled pass leaves the rest of the index where it is,
	 * and the next run of the thread resumes it.  Marking it done
	 * would strand every entry it has not reached.
	 */
	mutex_enter(&dcl->dcl_lock);
	if (w->dcw_gen == dcl->dcl_gen) {
		dcl->dcl_apply_ready = dcl->dcl_index_active &&
		    avl_numnodes(&dcl->dcl_index) == 0;
		/* only destinations are left, and none has applied */
		if (dcl->dcl_apply_ready)
			dcl->dcl_apply_total = dcl->dcl_nentries;
		if (dcl->dcl_apply_ready &&
		    dcl->dcl_apply_shared != 0) {
			zfs_dbgmsg("clonedup: %s: %llu apply "
			    "entries share a locator with another",
			    spa_name(dcl->dcl_dp->dp_spa),
			    (u_longlong_t)dcl->dcl_apply_shared);
		}
	}
	mutex_exit(&dcl->dcl_lock);
	w->dcw_group_src_valid = B_FALSE;
	kmem_free(buf, bufmax * sizeof (*buf));
}

static boolean_t
dsl_clonedup_src_is(const dsl_clonedup_src_t *s, const dva_t *dva,
    uint64_t birth)
{
	return (s->dcs_birth == birth && DVA_EQUAL(&s->dcs_dva, dva));
}

/*
 * When the apply found e's source stale for an earlier copy of e's
 * group, hand w the live copy that took its place, as the group
 * source w tries first.  *tried gets that copy too, or a birth of 0.
 */
static void
dsl_clonedup_gsrc_get(dsl_clonedup_t *dcl, dsl_clonedup_worker_t *w,
    const dsl_clonedup_entry_t *e, dsl_clonedup_src_t *tried)
{
	const dsl_clonedup_src_t *s = e->dce_src;
	dsl_clonedup_gsrc_t probe;
	dsl_clonedup_gsrc_t *gs;
	dsl_clonedup_lost_t *lx;

	memset(tried, 0, sizeof (*tried));
	w->dcw_group_src_valid = B_FALSE;
	probe.dcg_key = e->dce_key;
	probe.dcg_prop = e->dce_prop;
	mutex_enter(&dcl->dcl_lock);
	if (w->dcw_gen == dcl->dcl_gen &&
	    (gs = avl_find(&dcl->dcl_gsrc, &probe, NULL)) != NULL &&
	    (lx = gs->dcg_lost) != NULL &&
	    dsl_clonedup_src_is(s, &lx->dcx_dva, lx->dcx_birth)) {
		*tried = lx->dcx_src;
		w->dcw_group_src = lx->dcx_src;
		w->dcw_group_key = e->dce_key;
		w->dcw_group_prop = e->dce_prop;
		w->dcw_group_src_valid = B_TRUE;
	}
	mutex_exit(&dcl->dcl_lock);
}

/*
 * Record what one apply of e showed about its group, and say whether
 * to apply e again.  The first destination with work counts the
 * group.  A source other than the decided one means the decided one
 * was stale, and the copy the apply took instead serves the rest of
 * the group.  That copy is the one dsl_clonedup_gsrc_get() handed
 * out, or e itself when no other copy worked.  If another worker has
 * since recorded a copy e did not try, e goes again, once, so the
 * group still ends up on one block.  An e on a removing or indirect
 * vdev serves no other copy, so it is not recorded.  A record stays
 * until the index goes, and the gen changes with it.
 */
static boolean_t
dsl_clonedup_gsrc_put(dsl_clonedup_t *dcl, dsl_clonedup_worker_t *w,
    const dsl_clonedup_entry_t *e, boolean_t live,
    const dsl_clonedup_src_t *tried, boolean_t last)
{
	const dsl_clonedup_src_t *s = e->dce_src;
	const dsl_clonedup_src_t *c = &w->dcw_group_src;
	dsl_clonedup_gsrc_t probe;
	dsl_clonedup_gsrc_t *gs;
	dsl_clonedup_gsrc_t *ngs = NULL;
	dsl_clonedup_lost_t *lx;
	dsl_clonedup_lost_t *nlx = NULL;
	avl_index_t where;
	boolean_t lost;
	boolean_t self;
	boolean_t newer;
	boolean_t again = B_FALSE;

	lost = w->dcw_group_src_valid &&
	    !dsl_clonedup_src_is(c, &s->dcs_dva, s->dcs_birth);
	if (!lost && !live)
		return (B_FALSE);
	self = lost &&
	    dsl_clonedup_src_is(c, &e->dce_dva, e->dce_birth);
	if (lost)
		nlx = kmem_zalloc(sizeof (*nlx), KM_SLEEP);

	probe.dcg_key = e->dce_key;
	probe.dcg_prop = e->dce_prop;
	mutex_enter(&dcl->dcl_lock);
	if (w->dcw_gen == dcl->dcl_gen &&
	    avl_find(&dcl->dcl_gsrc, &probe, NULL) == NULL) {
		mutex_exit(&dcl->dcl_lock);
		ngs = kmem_zalloc(sizeof (*ngs), KM_SLEEP);
		ngs->dcg_key = e->dce_key;
		ngs->dcg_prop = e->dce_prop;
		mutex_enter(&dcl->dcl_lock);
	}
	if (w->dcw_gen == dcl->dcl_gen) {
		gs = avl_find(&dcl->dcl_gsrc, &probe, &where);
		if (gs == NULL) {
			ASSERT3P(ngs, !=, NULL);
			avl_insert(&dcl->dcl_gsrc, ngs, where);
			dcl->dcl_mem_used += sizeof (*ngs);
			gs = ngs;
			ngs = NULL;
		}
		if (live && !gs->dcg_counted) {
			gs->dcg_counted = B_TRUE;
			dcl->dcl_phys.dclp_groups++;
		}
		lx = gs->dcg_lost;
		newer = lx != NULL && dsl_clonedup_src_is(s,
		    &lx->dcx_dva, lx->dcx_birth) &&
		    !dsl_clonedup_src_is(&lx->dcx_src,
		    &e->dce_dva, e->dce_birth) &&
		    !dsl_clonedup_src_is(&lx->dcx_src,
		    &tried->dcs_dva, tried->dcs_birth);
		if (self && newer && !last) {
			again = B_TRUE;
		} else if (lost && !(c->dcs_flags & DCE_F_REMOVED)) {
			if (lx == NULL) {
				lx = gs->dcg_lost = nlx;
				nlx = NULL;
				dcl->dcl_mem_used += sizeof (*lx);
			}
			lx->dcx_dva = s->dcs_dva;
			lx->dcx_birth = s->dcs_birth;
			lx->dcx_src = *c;
		}
		dsl_clonedup_mem_note(dcl);
	}
	mutex_exit(&dcl->dcl_lock);
	if (ngs != NULL)
		kmem_free(ngs, sizeof (*ngs));
	if (nlx != NULL)
		kmem_free(nlx, sizeof (*nlx));
	return (again);
}

/*
 * Apply one decided candidate.  It goes through the unordered path
 * as a group of one whose source is already chosen, so the yield
 * handshake, the batching and every per-destination check are the
 * same code in both orders.  What the candidate needs from the rest
 * of its group comes from dsl_clonedup_gsrc_get(), and what it
 * learns goes back through dsl_clonedup_gsrc_put().  A candidate
 * whose source failed, and that became its own source, has no pair
 * left.  It counts as a stale or dirty skip once, after its last
 * pass.
 */
static void
dsl_clonedup_apply_one(dsl_clonedup_t *dcl, dsl_clonedup_worker_t *w,
    dsl_clonedup_entry_t *e, boolean_t dryrun,
    dsl_clonedup_stats_t *acc, zthr_t *zthr)
{
	dsl_clonedup_entry_t *ep = e;
	dsl_clonedup_group_t g;
	dsl_clonedup_src_t tried;

	for (int pass = 0; ; pass++) {
		memset(&g, 0, sizeof (g));
		g.g_entries = &ep;
		g.g_count = 1;
		g.g_max = 1;
		g.g_key = e->dce_key;
		g.g_prop = e->dce_prop;
		g.g_src = e->dce_src;
		g.g_counted = B_TRUE;

		dsl_clonedup_gsrc_get(dcl, w, e, &tried);
		dsl_clonedup_group_process(dcl, w, &g, dryrun, acc,
		    zthr);
		if (!dsl_clonedup_gsrc_put(dcl, w, e, g.g_live,
		    &tried, pass > 0))
			break;
	}
	w->dcw_group_src_valid = B_FALSE;
	if (g.g_lost == EAGAIN)
		acc->s_dirty++;
	else if (g.g_lost != 0)
		acc->s_stale++;

	kmem_free(e->dce_src, sizeof (*e->dce_src));
	kmem_cache_free(dsl_clonedup_entry_cache, e);
}

/*
 * Does another worker hold this dataset?  An unmounted destination is
 * taken with dmu_objset_own(), which is exclusive, so two workers on
 * one dataset would collide.  The apply tree sorts on the destination
 * dataset first, so skipping a claimed one lands the worker on the
 * next dataset rather than interleaving.
 */
static boolean_t
dsl_clonedup_ds_taken(dsl_clonedup_t *dcl, dsl_clonedup_worker_t *me,
    uint64_t dsobj)
{
	ASSERT(MUTEX_HELD(&dcl->dcl_lock));
	for (uint_t i = 0; i < dcl->dcl_nworkers; i++) {
		dsl_clonedup_worker_t *o = dcl->dcl_workers[i];

		if (o == NULL || o == me)
			continue;
		if (o->dcw_claim_dst == dsobj)
			return (B_TRUE);
	}
	return (B_FALSE);
}

/*
 * The first entry of the dataset after dsobj.  The tree sorts on the
 * destination dataset, so one descent skips every entry of a dataset
 * another worker holds.  Stepping them one at a time is quadratic in
 * the candidate count and runs under dcl_lock.
 */
static dsl_clonedup_entry_t *
dsl_clonedup_apply_next_ds(dsl_clonedup_t *dcl, uint64_t dsobj)
{
	dsl_clonedup_entry_t probe;
	dsl_clonedup_src_t psrc;
	avl_index_t where;
	dsl_clonedup_entry_t *e;

	ASSERT(MUTEX_HELD(&dcl->dcl_lock));
	if (dsobj == UINT64_MAX)
		return (NULL);
	memset(&probe, 0, sizeof (probe));
	memset(&psrc, 0, sizeof (psrc));
	probe.dce_src = &psrc;
	probe.dce_objset = dsobj + 1;
	e = avl_find(&dcl->dcl_apply, &probe, &where);
	if (e == NULL)
		e = avl_nearest(&dcl->dcl_apply, where, AVL_AFTER);
	return (e);
}

static dsl_clonedup_entry_t *
dsl_clonedup_apply_pop(dsl_clonedup_t *dcl, dsl_clonedup_worker_t *w,
    boolean_t *taken)
{
	dsl_clonedup_entry_t *e;

	ASSERT(MUTEX_HELD(&dcl->dcl_lock));
	*taken = B_FALSE;
	e = avl_first(&dcl->dcl_apply);
	while (e != NULL) {
		if (!dsl_clonedup_ds_taken(dcl, w, e->dce_objset)) {
			avl_remove(&dcl->dcl_apply, e);
			dcl->dcl_nentries--;
			dcl->dcl_nsrc--;
			dcl->dcl_mem_used -= sizeof (*e) +
			    sizeof (*e->dce_src);
			w->dcw_claim_dst = e->dce_objset;
			return (e);
		}
		*taken = B_TRUE;
		e = dsl_clonedup_apply_next_ds(dcl, e->dce_objset);
	}
	w->dcw_claim_dst = 0;
	return (NULL);
}

/*
 * Fold in what the worker counted and drop its busy mark under one
 * hold of dcl_lock, so a partition cannot end with counts left out.
 */
static void
dsl_clonedup_apply_idle(dsl_clonedup_t *dcl, dsl_clonedup_worker_t *w,
    boolean_t *busyp, dsl_clonedup_stats_t *st)
{
	if (!*busyp)
		return;
	mutex_enter(&dcl->dcl_lock);
	dsl_clonedup_stats_add(dcl, w, st);
	if (dcl->dcl_apply_busy > 0)
		dcl->dcl_apply_busy--;
	mutex_exit(&dcl->dcl_lock);
	memset(st, 0, sizeof (*st));
	*busyp = B_FALSE;
}

static void
dsl_clonedup_apply_ordered(dsl_clonedup_t *dcl,
    dsl_clonedup_worker_t *w, zthr_t *zthr)
{
	dsl_pool_t *dp = dcl->dcl_dp;
	spa_t *spa = dp->dp_spa;
	dsl_clonedup_stats_t st = { 0 };
	dsl_clonedup_entry_t *e;
	boolean_t busy = B_FALSE;
	boolean_t dryrun;
	boolean_t last;
	boolean_t taken;

	while (!dsl_clonedup_stop(dcl, zthr)) {
		if (dsl_scan_is_paused_scrub(dp->dp_scan) ||
		    !zfs_clonedup_apply_enabled ||
		    !zfs_clonedup_bclone_enabled())
			break;
		dsl_clonedup_yield_release(dcl, w);
		mutex_enter(&dcl->dcl_lock);
		e = NULL;
		taken = B_FALSE;
		if (dcl->dcl_index_active &&
		    w->dcw_gen == dcl->dcl_gen)
			e = dsl_clonedup_apply_pop(dcl, w, &taken);
		if (e == NULL) {
			mutex_exit(&dcl->dcl_lock);
			if (!taken)
				break;
			/*
			 * Only datasets another worker owns are left.
			 * Give up what this one holds so it is not
			 * the one they wait on, and look again.
			 */
			dsl_clonedup_batch_flush(dcl, w);
			dsl_clonedup_apply_idle(dcl, w, &busy, &st);
			dsl_clonedup_dst_rele(dcl, w);
			delay(1);
			continue;
		}
		if (!busy)
			dcl->dcl_apply_busy++;
		busy = B_TRUE;
		/*
		 * Both stay true for the gen.  The flags are the
		 * run's, and only the order pass adds to the tree.
		 */
		dryrun = (dcl->dcl_phys.dclp_flags &
		    DSF_CLONEDUP_DRYRUN) != 0;
		last = avl_numnodes(&dcl->dcl_apply) == 0;
		mutex_exit(&dcl->dcl_lock);

		dsl_clonedup_apply_one(dcl, w, e, dryrun, &st, zthr);

		if (last || dsl_clonedup_stale(dcl, w) ||
		    vdev_queue_pool_busy(spa))
			dsl_clonedup_batch_flush(dcl, w);
		if (w->dcw_batch.dcb_count == 0)
			dsl_clonedup_apply_idle(dcl, w, &busy, &st);
	}
	if (zfs_clonedup_apply_flush_delay != 0 &&
	    w->dcw_batch.dcb_count > 0)
		delay(MSEC_TO_TICK(zfs_clonedup_apply_flush_delay));
	dsl_clonedup_batch_flush(dcl, w);
	dsl_clonedup_apply_idle(dcl, w, &busy, &st);
	mutex_enter(&dcl->dcl_lock);
	w->dcw_claim_dst = 0;
	mutex_exit(&dcl->dcl_lock);
}

boolean_t
dsl_clonedup_apply_check(void *arg, zthr_t *zthr)
{
	(void) zthr;
	spa_t *spa = arg;
	dsl_pool_t *dp = spa_get_dsl(spa);
	dsl_clonedup_t *dcl;
	boolean_t work;

	if (dp == NULL || (dcl = dp->dp_clonedup) == NULL)
		return (B_FALSE);
	if (!zfs_clonedup_apply_enabled ||
	    !zfs_clonedup_bclone_enabled() ||
	    !spa_writeable(spa) ||
	    spa_shutting_down(spa) ||
	    spa_load_state(spa) != SPA_LOAD_NONE)
		return (B_FALSE);
	if (dsl_scan_is_paused_scrub(dp->dp_scan))
		return (B_FALSE);

	mutex_enter(&dcl->dcl_lock);
	work = dcl->dcl_phys.dclp_state == DSS_SCANNING &&
	    dcl->dcl_phys.dclp_phase == POOL_CLONEDUP_APPLY &&
	    dcl->dcl_index_active && dcl->dcl_nentries > 0;
	mutex_exit(&dcl->dcl_lock);
	return (work);
}

typedef struct dsl_clonedup_wtask {
	dsl_clonedup_t		*wt_dcl;
	dsl_clonedup_worker_t	*wt_w;
	zthr_t			*wt_zthr;
} dsl_clonedup_wtask_t;

static uint_t
dsl_clonedup_worker_count(void)
{
	uint_t n = zfs_clonedup_apply_threads;

	if (n == 0)
		n = MAX(1, (max_ncpus + 3) / 4);
	return (MIN(MAX(1, n), DCL_MAX_WORKERS));
}

static void
dsl_clonedup_worker_run(void *arg)
{
	dsl_clonedup_wtask_t *wt = arg;
	dsl_clonedup_t *dcl = wt->wt_dcl;
	dsl_clonedup_worker_t *w = wt->wt_w;

	dsl_clonedup_batch_init(dcl, w);
	dsl_clonedup_dscache_init(w);
	dsl_clonedup_apply_ordered(dcl, w, wt->wt_zthr);
	dsl_clonedup_batch_flush(dcl, w);
	dsl_clonedup_dscache_fini(w);
	dsl_clonedup_batch_fini(w);
	dsl_clonedup_dst_rele(dcl, w);
	dsl_clonedup_src_rele(dcl, w);
	mutex_enter(&dcl->dcl_lock);
	dcl->dcl_apply_running--;
	cv_broadcast(&dcl->dcl_apply_cv);
	mutex_exit(&dcl->dcl_lock);
}

/*
 * Run the apply on several workers.  The caller's worker is one of
 * them and runs here, so a single worker costs no taskq at all.
 */
static void
dsl_clonedup_apply_parallel(dsl_clonedup_t *dcl,
    dsl_clonedup_worker_t *w, zthr_t *zthr)
{
	uint_t n = dsl_clonedup_worker_count();
	uint_t i;
	dsl_clonedup_wtask_t *wt = NULL;
	taskq_t *tq = NULL;

	for (i = 1; i < n; i++) {
		dcl->dcl_workers[i] = kmem_zalloc(sizeof (*w),
		    KM_SLEEP);
		dcl->dcl_workers[i]->dcw_gen = w->dcw_gen;
	}
	/* no yield reads a slot the count does not cover yet */
	mutex_enter(&dcl->dcl_yield_lock);
	dcl->dcl_workers[0] = w;
	dcl->dcl_nworkers = n;
	mutex_exit(&dcl->dcl_yield_lock);

	if (n > 1) {
		tq = taskq_create("clonedup_apply", n - 1,
		    minclsyspri, n - 1, n - 1, TASKQ_PREPOPULATE);
		wt = kmem_zalloc((n - 1) * sizeof (*wt), KM_SLEEP);
		mutex_enter(&dcl->dcl_lock);
		dcl->dcl_apply_running = n - 1;
		mutex_exit(&dcl->dcl_lock);
		for (i = 1; i < n; i++) {
			wt[i - 1].wt_dcl = dcl;
			wt[i - 1].wt_w = dcl->dcl_workers[i];
			wt[i - 1].wt_zthr = zthr;
			VERIFY(taskq_dispatch(tq,
			    dsl_clonedup_worker_run, &wt[i - 1],
			    TQ_SLEEP) != TASKQID_INVALID);
		}
	}

	dsl_clonedup_apply_ordered(dcl, w, zthr);

	if (tq != NULL) {
		/*
		 * A worker still running may be waiting for the
		 * dataset this thread holds, and nothing would
		 * release it until after the wait.
		 */
		dsl_clonedup_batch_flush(dcl, w);
		dsl_clonedup_dst_rele(dcl, w);
		dsl_clonedup_src_rele(dcl, w);
		for (;;) {
			(void) dsl_clonedup_stop(dcl, zthr);
			mutex_enter(&dcl->dcl_lock);
			if (dcl->dcl_apply_running == 0) {
				mutex_exit(&dcl->dcl_lock);
				break;
			}
			(void) cv_timedwait(&dcl->dcl_apply_cv,
			    &dcl->dcl_lock,
			    ddi_get_lbolt() + MSEC_TO_TICK(100));
			mutex_exit(&dcl->dcl_lock);
		}
		taskq_wait(tq);
		taskq_destroy(tq);
		kmem_free(wt, (n - 1) * sizeof (*wt));
	}
	/*
	 * A yield walks this array under dcl_yield_lock.  Shrink the
	 * count under it, so no walk still holds a pointer to a
	 * worker when the loop below frees it.
	 */
	mutex_enter(&dcl->dcl_yield_lock);
	dcl->dcl_nworkers = 1;
	mutex_exit(&dcl->dcl_yield_lock);
	for (i = 1; i < n; i++) {
		dsl_clonedup_worker_t *o = dcl->dcl_workers[i];

		dcl->dcl_workers[i] = NULL;
		kmem_free(o, sizeof (*w));
	}
}

static void
dsl_clonedup_apply_groups(dsl_clonedup_t *dcl,
    dsl_clonedup_worker_t *w, zthr_t *zthr)
{
	/* see dsl_clonedup_yield_idle() */
	atomic_inc_64(&dsl_clonedup_appliers);
	membar_sync();
	dsl_clonedup_batch_init(dcl, w);
	dsl_clonedup_dscache_init(w);
	if (!dcl->dcl_apply_ready)
		dsl_clonedup_order_index(dcl, w, zthr);
	dsl_clonedup_apply_parallel(dcl, w, zthr);
	dsl_clonedup_batch_flush(dcl, w);
	dsl_clonedup_dscache_fini(w);
	dsl_clonedup_batch_fini(w);
	dsl_clonedup_dst_rele(dcl, w);
	dsl_clonedup_src_rele(dcl, w);
	atomic_dec_64(&dsl_clonedup_appliers);
}

void
dsl_clonedup_apply_thread(void *arg, zthr_t *zthr)
{
	spa_t *spa = arg;
	dsl_pool_t *dp = spa_get_dsl(spa);
	dsl_clonedup_t *dcl = dp->dp_clonedup;

	mutex_enter(&dcl->dcl_lock);
	if (!dcl->dcl_index_active ||
	    dcl->dcl_phys.dclp_phase != POOL_CLONEDUP_APPLY) {
		mutex_exit(&dcl->dcl_lock);
		return;
	}
	dcl->dcl_w.dcw_gen = dcl->dcl_gen;
	dcl->dcl_w.dcw_group_src_valid = B_FALSE;
	dcl->dcl_zthr = zthr;
	atomic_store_32(&dcl->dcl_apply_stop, B_FALSE);
	mutex_exit(&dcl->dcl_lock);
	dsl_clonedup_apply_groups(dcl, &dcl->dcl_w, zthr);
	mutex_enter(&dcl->dcl_lock);
	dcl->dcl_zthr = NULL;
	mutex_exit(&dcl->dcl_lock);
}

void
dsl_clonedup_apply_wakeup(spa_t *spa)
{
	if (spa->spa_clonedup_apply_zthr != NULL)
		zthr_wakeup(spa->spa_clonedup_apply_zthr);
}

ZFS_MODULE_PARAM(zfs_clonedup, zfs_clonedup_, scan_mem_max, U64,
	ZMOD_RW,
	"Bytes the clonedup index may use (0 = RAM / lim_fact)");

ZFS_MODULE_PARAM(zfs_clonedup, zfs_clonedup_, apply_enabled, INT,
	ZMOD_RW, "Run the clonedup apply thread");

ZFS_MODULE_PARAM(zfs_clonedup, zfs_clonedup_, apply_threads, UINT,
	ZMOD_RW, "Apply workers, 0 for one per four cores");

ZFS_MODULE_PARAM(zfs_clonedup, zfs_clonedup_, apply_blocks_per_txg,
	UINT, ZMOD_RW,
	"Most blocks the apply workers put in one txg");

ZFS_MODULE_PARAM(zfs_clonedup, zfs_clonedup_, apply_txg_delay, UINT,
	ZMOD_RW, "Debug: ms to sleep before locking each block");

ZFS_MODULE_PARAM(zfs_clonedup, zfs_clonedup_, apply_flush_delay, UINT,
	ZMOD_RW, "Debug: ms to sleep before a worker's final flush");

ZFS_MODULE_PARAM(zfs_clonedup, zfs_clonedup_, yield_timeout_ms, UINT,
	ZMOD_RW, "ms an admin operation waits for the apply thread");
