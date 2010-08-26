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
 */

#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_tx.h>
#include <sys/dnode.h>

uint64_t
dmu_object_alloc(objset_t *os, dmu_object_type_t ot, int blocksize,
    dmu_object_type_t bonustype, int bonuslen, dmu_tx_t *tx)
{
	uint64_t object;
	uint64_t L2_dnode_count = DNODES_PER_BLOCK <<
	    (DMU_META_DNODE(os)->dn_indblkshift - SPA_BLKPTRSHIFT);
	dnode_t *dn = NULL;
	int restarted = B_FALSE;

	mutex_enter(&os->os_obj_lock);
	for (;;) {
		object = os->os_obj_next;
		/*
		 * Each time we polish off an L2 bp worth of dnodes
		 * (2^13 objects), move to another L2 bp that's still
		 * reasonably sparse (at most 1/4 full).  Look from the
		 * beginning once, but after that keep looking from here.
		 * If we can't find one, just keep going from here.
		 */
		if (P2PHASE(object, L2_dnode_count) == 0) {
			uint64_t offset = restarted ? object << DNODE_SHIFT : 0;
			int error = dnode_next_offset(DMU_META_DNODE(os),
			    DNODE_FIND_HOLE,
			    &offset, 2, DNODES_PER_BLOCK >> 2, 0);
			restarted = B_TRUE;
			if (error == 0)
				object = offset >> DNODE_SHIFT;
		}
		os->os_obj_next = ++object;

		/*
		 * XXX We should check for an i/o error here and return
		 * up to our caller.  Actually we should pre-read it in
		 * dmu_tx_assign(), but there is currently no mechanism
		 * to do so.
		 */
		(void) dnode_hold_impl(os, object, DNODE_MUST_BE_FREE,
		    FTAG, &dn);
		if (dn)
			break;

		if (dmu_object_next(os, &object, B_TRUE, 0) == 0)
			os->os_obj_next = object - 1;
	}

	dnode_allocate(dn, ot, blocksize, 0, bonustype, bonuslen, tx);
	dnode_rele(dn, FTAG);

	mutex_exit(&os->os_obj_lock);

	dmu_tx_add_new_object(tx, os, object);
	return (object);
}

int
dmu_object_claim(objset_t *os, uint64_t object, dmu_object_type_t ot,
    int blocksize, dmu_object_type_t bonustype, int bonuslen, dmu_tx_t *tx)
{
	dnode_t *dn;
	int err;

	if (object == DMU_META_DNODE_OBJECT && !dmu_tx_private_ok(tx))
		return (EBADF);

	err = dnode_hold_impl(os, object, DNODE_MUST_BE_FREE, FTAG, &dn);
	if (err)
		return (err);
	dnode_allocate(dn, ot, blocksize, 0, bonustype, bonuslen, tx);
	dnode_rele(dn, FTAG);

	dmu_tx_add_new_object(tx, os, object);
	return (0);
}

int
dmu_object_reclaim(objset_t *os, uint64_t object, dmu_object_type_t ot,
    int blocksize, dmu_object_type_t bonustype, int bonuslen)
{
	dnode_t *dn;
	dmu_tx_t *tx;
	int nblkptr;
	int err;

	if (object == DMU_META_DNODE_OBJECT)
		return (EBADF);

	err = dnode_hold_impl(os, object, DNODE_MUST_BE_ALLOCATED,
	    FTAG, &dn);
	if (err)
		return (err);

	if (dn->dn_type == ot && dn->dn_datablksz == blocksize &&
	    dn->dn_bonustype == bonustype && dn->dn_bonuslen == bonuslen) {
		/* nothing is changing, this is a noop */
		dnode_rele(dn, FTAG);
		return (0);
	}

	if (bonustype == DMU_OT_SA) {
		nblkptr = 1;
	} else {
		nblkptr = 1 + ((DN_MAX_BONUSLEN - bonuslen) >> SPA_BLKPTRSHIFT);
	}

	/*
	 * If we are losing blkptrs or changing the block size this must
	 * be a new file instance.   We must clear out the previous file
	 * contents before we can change this type of metadata in the dnode.
	 */
	if (dn->dn_nblkptr > nblkptr || dn->dn_datablksz != blocksize) {
		err = dmu_free_long_range(os, object, 0, DMU_OBJECT_END);
		if (err)
			goto out;
	}

	tx = dmu_tx_create(os);
	dmu_tx_hold_bonus(tx, object);
	err = dmu_tx_assign(tx, TXG_WAIT);
	if (err) {
		dmu_tx_abort(tx);
		goto out;
	}

	dnode_reallocate(dn, ot, blocksize, bonustype, bonuslen, tx);

	dmu_tx_commit(tx);
out:
	dnode_rele(dn, FTAG);

	return (err);
}

int
dmu_object_free(objset_t *os, uint64_t object, dmu_tx_t *tx)
{
	dnode_t *dn;
	int err;

	ASSERT(object != DMU_META_DNODE_OBJECT || dmu_tx_private_ok(tx));

	err = dnode_hold_impl(os, object, DNODE_MUST_BE_ALLOCATED,
	    FTAG, &dn);
	if (err)
		return (err);

	ASSERT(dn->dn_type != DMU_OT_NONE);
	dnode_free_range(dn, 0, DMU_OBJECT_END, tx);
	dnode_free(dn, tx);
	dnode_rele(dn, FTAG);

	return (0);
}

int
dmu_object_next(objset_t *os, uint64_t *objectp, boolean_t hole, uint64_t txg)
{
	uint64_t offset = (*objectp + 1) << DNODE_SHIFT;
	int error;

	error = dnode_next_offset(DMU_META_DNODE(os),
	    (hole ? DNODE_FIND_HOLE : 0), &offset, 0, DNODES_PER_BLOCK, txg);

	*objectp = offset >> DNODE_SHIFT;

	return (error);
}
