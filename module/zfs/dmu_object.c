/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2013, 2015 by Delphix. All rights reserved.
 * Copyright 2014 HybridCluster. All rights reserved.
 */

#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_tx.h>
#include <sys/dbuf.h>
#include <sys/dnode.h>
#include <sys/zap.h>
#include <sys/zfeature.h>
#include <sys/dsl_dataset.h>

dnode_t *dnode_create(objset_t *os, dnode_phys_t *dnp, dmu_buf_impl_t *db,
    uint64_t object, dnode_handle_t *dnh);
int dnode_alloc_impl(objset_t *os, uint64_t *object, int slots, void *tag,
	dmu_buf_impl_t **rdb);

uint64_t
dmu_object_alloc(objset_t *os, dmu_object_type_t ot, int blocksize,
    dmu_object_type_t bonustype, int bonuslen, dmu_tx_t *tx)
{
	return dmu_object_alloc_dnsize(os, ot, blocksize, bonustype, bonuslen,
	    0, tx);
}

void dnode_allocate_structures(objset_t *os, dmu_buf_impl_t *db,
	uint64_t object, int slots, dnode_t **dnp, void *tag)
{
	dnode_children_t *children_dnodes;
	dnode_phys_t *dn_block_begin;
	dnode_handle_t *dnh;
	dnode_t *dn;
	int epb, idx;

	epb = db->db.db_size >> DNODE_SHIFT;
	idx = object & (epb - 1);
	dn_block_begin = (dnode_phys_t *)db->db.db_data;
	children_dnodes = dmu_buf_get_user(&db->db);

	dnh = &children_dnodes->dnc_children[idx];
	zrl_add(&dnh->dnh_zrlock);
	dn = dnh->dnh_dnode;
	if (dn == NULL)
		dn = dnode_create(os, dn_block_begin + idx, db, object, dnh);

	mutex_enter(&dn->dn_mtx);
	ASSERT(dn->dn_type == DMU_OT_NONE);
	if (refcount_add(&dn->dn_holds, tag) == 1)
		dbuf_add_ref(db, dnh);
	mutex_exit(&dn->dn_mtx);

	/* Now we can rely on the hold to prevent the dnode from moving. */
	zrl_remove(&dnh->dnh_zrlock);

	DNODE_VERIFY(dn);
	ASSERT3P(dn->dn_dbuf, ==, db);
	ASSERT3U(dn->dn_object, ==, object);
	*dnp = dn;
}

static dmu_tx_hold_t *
dmu_tx_hold_dnode_impl(dmu_tx_t *tx, objset_t *os, dnode_t *dn,
    enum dmu_tx_hold_type type, uint64_t arg1, uint64_t arg2)
{
	dmu_tx_hold_t *txh;

	mutex_enter(&dn->dn_mtx);
	/*
	 * dn->dn_assigned_txg == tx->tx_txg doesn't pose a
	 * problem, but there's no way for it to happen (for
	 * now, at least).
	 */
	ASSERT(dn->dn_assigned_txg == 0);
	dn->dn_assigned_txg = tx->tx_txg;
	(void) refcount_add(&dn->dn_tx_holds, tx);
	mutex_exit(&dn->dn_mtx);

	txh = kmem_zalloc(sizeof (dmu_tx_hold_t), KM_SLEEP);
	txh->txh_tx = tx;
	txh->txh_dnode = dn;
	dnode_add_ref(dn, tx);
#ifdef DEBUG_DMU_TX
	txh->txh_type = type;
	txh->txh_arg1 = arg1;
	txh->txh_arg2 = arg2;
#endif
	list_insert_tail(&tx->tx_holds, txh);

	return (txh);
}

uint64_t
dmu_object_alloc_dnsize(objset_t *os, dmu_object_type_t ot, int blocksize,
    dmu_object_type_t bonustype, int bonuslen, int dnodesize, dmu_tx_t *tx)
{
	uint64_t object;
	uint64_t L1_dnode_count = DNODES_PER_BLOCK <<
	    (DMU_META_DNODE(os)->dn_indblkshift - SPA_BLKPTRSHIFT);
	dnode_t *dn = NULL;
	dmu_buf_impl_t *db;
	int dn_slots = dnodesize >> DNODE_SHIFT;
	boolean_t restarted = B_FALSE;

	if (dn_slots == 0) {
		dn_slots = DNODE_MIN_SLOTS;
	} else {
		ASSERT3S(dn_slots, >=, DNODE_MIN_SLOTS);
		ASSERT3S(dn_slots, <=, DNODE_MAX_SLOTS);
	}

	mutex_enter(&os->os_obj_lock);
	for (;;) {
		object = os->os_obj_next;
		/*
		 * Each time we polish off a L1 bp worth of dnodes (2^12
		 * objects), move to another L1 bp that's still
		 * reasonably sparse (at most 1/4 full). Look from the
		 * beginning at most once per txg. If we still can't
		 * allocate from that L1 block, search for an empty L0
		 * block, which will quickly skip to the end of the
		 * metadnode if the no nearby L0 blocks are empty. This
		 * fallback avoids a pathology where full dnode blocks
		 * containing large dnodes appear sparse because they
		 * have a low blk_fill, leading to many failed
		 * allocation attempts. In the long term a better
		 * mechanism to search for sparse metadnode regions,
		 * such as spacemaps, could be implemented.
		 *
		 * os_scan_dnodes is set during txg sync if enough objects
		 * have been freed since the previous rescan to justify
		 * backfilling again.
		 *
		 * Note that dmu_traverse depends on the behavior that we use
		 * multiple blocks of the dnode object before going back to
		 * reuse objects.  Any change to this algorithm should preserve
		 * that property or find another solution to the issues
		 * described in traverse_visitbp.
		 */
		if (P2PHASE(object, L1_dnode_count) == 0) {
			uint64_t offset;
			uint64_t blkfill;
			int minlvl;
			int error;
			if (os->os_rescan_dnodes) {
				offset = 0;
				os->os_rescan_dnodes = B_FALSE;
			} else {
				offset = object << DNODE_SHIFT;
			}
			blkfill = restarted ? 1 : DNODES_PER_BLOCK >> 2;
			minlvl = restarted ? 1 : 2;
			restarted = B_TRUE;
			error = dnode_next_offset(DMU_META_DNODE(os),
			    DNODE_FIND_HOLE, &offset, minlvl, blkfill, 0);
			if (error == 0)
				object = offset >> DNODE_SHIFT;
		}
		os->os_obj_next = object + dn_slots;

		/*
		 * XXX We should check for an i/o error here and return
		 * up to our caller.  Actually we should pre-read it in
		 * dmu_tx_assign(), but there is currently no mechanism
		 * to do so.
		 */
		(void) dnode_alloc_impl(os, &object, dn_slots, FTAG, &db);
		if (db)
			break;

		if (dmu_object_next(os, &object, B_TRUE, 0) == 0)
			os->os_obj_next = object;
		else
			/*
			 * Skip to next known valid starting point for a dnode.
			 */
			os->os_obj_next = P2ROUNDUP(object + 1,
			    DNODES_PER_BLOCK);
	}

	os->os_obj_next = object + dn_slots;
	mutex_exit(&os->os_obj_lock);

	dnode_allocate_structures(os, db, object, dn_slots, &dn, FTAG);
	dnode_allocate(dn, ot, blocksize, 0, bonustype, bonuslen, dn_slots, tx);

	dmu_tx_add_new_object(tx, os, dn);
	dnode_rele(dn, FTAG);
	dbuf_rele(db, FTAG);

	return (object);
}

int
dmu_object_claim(objset_t *os, uint64_t object, dmu_object_type_t ot,
    int blocksize, dmu_object_type_t bonustype, int bonuslen, dmu_tx_t *tx)
{
	return (dmu_object_claim_dnsize(os, object, ot, blocksize, bonustype,
	    bonuslen, 0, tx));
}

int
dmu_object_claim_dnsize(objset_t *os, uint64_t object, dmu_object_type_t ot,
    int blocksize, dmu_object_type_t bonustype, int bonuslen,
    int dnodesize, dmu_tx_t *tx)
{
	dnode_t *dn;
	int dn_slots = dnodesize >> DNODE_SHIFT;
	int err;

	if (dn_slots == 0)
		dn_slots = DNODE_MIN_SLOTS;
	ASSERT3S(dn_slots, >=, DNODE_MIN_SLOTS);
	ASSERT3S(dn_slots, <=, DNODE_MAX_SLOTS);

	if (object == DMU_META_DNODE_OBJECT && !dmu_tx_private_ok(tx))
		return (SET_ERROR(EBADF));

	err = dnode_hold_impl(os, object, DNODE_MUST_BE_FREE, dn_slots,
	    FTAG, &dn);
	if (err)
		return (err);

	dnode_allocate(dn, ot, blocksize, 0, bonustype, bonuslen, dn_slots, tx);
	dmu_tx_add_new_object(tx, os, dn);

	dnode_rele(dn, FTAG);

	return (0);
}

int
dmu_object_reclaim(objset_t *os, uint64_t object, dmu_object_type_t ot,
    int blocksize, dmu_object_type_t bonustype, int bonuslen, dmu_tx_t *tx)
{
	return (dmu_object_reclaim_dnsize(os, object, ot, blocksize, bonustype,
	    bonuslen, 0, tx));
}

int
dmu_object_reclaim_dnsize(objset_t *os, uint64_t object, dmu_object_type_t ot,
    int blocksize, dmu_object_type_t bonustype, int bonuslen, int dnodesize,
    dmu_tx_t *tx)
{
	dnode_t *dn;
	int dn_slots = dnodesize >> DNODE_SHIFT;
	int err;

	if (object == DMU_META_DNODE_OBJECT)
		return (SET_ERROR(EBADF));

	err = dnode_hold_impl(os, object, DNODE_MUST_BE_ALLOCATED, 0,
	    FTAG, &dn);
	if (err)
		return (err);

	dnode_reallocate(dn, ot, blocksize, bonustype, bonuslen, dn_slots, tx);

	dnode_rele(dn, FTAG);
	return (err);
}


int
dmu_object_free(objset_t *os, uint64_t object, dmu_tx_t *tx)
{
	dnode_t *dn;
	int err;

	ASSERT(object != DMU_META_DNODE_OBJECT || dmu_tx_private_ok(tx));

	err = dnode_hold_impl(os, object, DNODE_MUST_BE_ALLOCATED, 0,
	    FTAG, &dn);
	if (err)
		return (err);

	ASSERT(dn->dn_type != DMU_OT_NONE);
	dnode_free_range(dn, 0, DMU_OBJECT_END, tx);
	dnode_free(dn, tx);
	dnode_rele(dn, FTAG);

	return (0);
}

/*
 * Return (in *objectp) the next object which is allocated (or a hole)
 * after *object, taking into account only objects that may have been modified
 * after the specified txg.
 */
int
dmu_object_next(objset_t *os, uint64_t *objectp, boolean_t hole, uint64_t txg)
{
	uint64_t offset;
	uint64_t start_obj;
	struct dsl_dataset *ds = os->os_dsl_dataset;
	int error;

	if (*objectp == 0) {
		start_obj = 1;
	} else if (ds && ds->ds_feature_inuse[SPA_FEATURE_LARGE_DNODE]) {
		/*
		 * For large_dnode datasets, scan from the beginning of the
		 * dnode block to find the starting offset. This is needed
		 * because objectp could be part of a large dnode so we can't
		 * assume it's a hole even if dmu_object_info() returns ENOENT.
		 */
		int epb = DNODE_BLOCK_SIZE >> DNODE_SHIFT;
		int skip;
		uint64_t i;

		for (i = *objectp & ~(epb - 1); i <= *objectp; i += skip) {
			dmu_object_info_t doi;

			error = dmu_object_info(os, i, &doi);
			if (error)
				skip = 1;
			else
				skip = doi.doi_dnodesize >> DNODE_SHIFT;
		}

		start_obj = i;
	} else {
		start_obj = *objectp + 1;
	}

	offset = start_obj << DNODE_SHIFT;

	error = dnode_next_offset(DMU_META_DNODE(os),
	    (hole ? DNODE_FIND_HOLE : 0), &offset, 0, DNODES_PER_BLOCK, txg);

	*objectp = offset >> DNODE_SHIFT;

	return (error);
}

/*
 * Turn this object from old_type into DMU_OTN_ZAP_METADATA, and bump the
 * refcount on SPA_FEATURE_EXTENSIBLE_DATASET.
 *
 * Only for use from syncing context, on MOS objects.
 */
void
dmu_object_zapify(objset_t *mos, uint64_t object, dmu_object_type_t old_type,
    dmu_tx_t *tx)
{
	dnode_t *dn;

	ASSERT(dmu_tx_is_syncing(tx));

	VERIFY0(dnode_hold(mos, object, FTAG, &dn));
	if (dn->dn_type == DMU_OTN_ZAP_METADATA) {
		dnode_rele(dn, FTAG);
		return;
	}
	ASSERT3U(dn->dn_type, ==, old_type);
	ASSERT0(dn->dn_maxblkid);
	dn->dn_next_type[tx->tx_txg & TXG_MASK] = dn->dn_type =
	    DMU_OTN_ZAP_METADATA;
	dnode_setdirty(dn, tx);
	dnode_rele(dn, FTAG);

	mzap_create_impl(mos, object, 0, 0, tx);

	spa_feature_incr(dmu_objset_spa(mos),
	    SPA_FEATURE_EXTENSIBLE_DATASET, tx);
}

void
dmu_object_free_zapified(objset_t *mos, uint64_t object, dmu_tx_t *tx)
{
	dnode_t *dn;
	dmu_object_type_t t;

	ASSERT(dmu_tx_is_syncing(tx));

	VERIFY0(dnode_hold(mos, object, FTAG, &dn));
	t = dn->dn_type;
	dnode_rele(dn, FTAG);

	if (t == DMU_OTN_ZAP_METADATA) {
		spa_feature_decr(dmu_objset_spa(mos),
		    SPA_FEATURE_EXTENSIBLE_DATASET, tx);
	}
	VERIFY0(dmu_object_free(mos, object, tx));
}

#if defined(_KERNEL) && defined(HAVE_SPL)
EXPORT_SYMBOL(dmu_object_alloc);
EXPORT_SYMBOL(dmu_object_alloc_dnsize);
EXPORT_SYMBOL(dmu_object_claim);
EXPORT_SYMBOL(dmu_object_claim_dnsize);
EXPORT_SYMBOL(dmu_object_reclaim);
EXPORT_SYMBOL(dmu_object_reclaim_dnsize);
EXPORT_SYMBOL(dmu_object_free);
EXPORT_SYMBOL(dmu_object_next);
EXPORT_SYMBOL(dmu_object_zapify);
EXPORT_SYMBOL(dmu_object_free_zapified);
#endif
