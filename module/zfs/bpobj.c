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
 * Copyright (c) 2011, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2017 Datto Inc.
 */

#include <sys/bpobj.h>
#include <sys/zfs_context.h>
#include <sys/zfs_refcount.h>
#include <sys/dsl_pool.h>
#include <sys/zfeature.h>
#include <sys/zap.h>

/*
 * Return an empty bpobj, preferably the empty dummy one (dp_empty_bpobj).
 */
uint64_t
bpobj_alloc_empty(objset_t *os, int blocksize, dmu_tx_t *tx)
{
	spa_t *spa = dmu_objset_spa(os);
	dsl_pool_t *dp = dmu_objset_pool(os);

	if (spa_feature_is_enabled(spa, SPA_FEATURE_EMPTY_BPOBJ)) {
		if (!spa_feature_is_active(spa, SPA_FEATURE_EMPTY_BPOBJ)) {
			ASSERT0(dp->dp_empty_bpobj);
			dp->dp_empty_bpobj =
			    bpobj_alloc(os, SPA_OLD_MAXBLOCKSIZE, tx);
			VERIFY(zap_add(os,
			    DMU_POOL_DIRECTORY_OBJECT,
			    DMU_POOL_EMPTY_BPOBJ, sizeof (uint64_t), 1,
			    &dp->dp_empty_bpobj, tx) == 0);
		}
		spa_feature_incr(spa, SPA_FEATURE_EMPTY_BPOBJ, tx);
		ASSERT(dp->dp_empty_bpobj != 0);
		return (dp->dp_empty_bpobj);
	} else {
		return (bpobj_alloc(os, blocksize, tx));
	}
}

void
bpobj_decr_empty(objset_t *os, dmu_tx_t *tx)
{
	dsl_pool_t *dp = dmu_objset_pool(os);

	spa_feature_decr(dmu_objset_spa(os), SPA_FEATURE_EMPTY_BPOBJ, tx);
	if (!spa_feature_is_active(dmu_objset_spa(os),
	    SPA_FEATURE_EMPTY_BPOBJ)) {
		VERIFY3U(0, ==, zap_remove(dp->dp_meta_objset,
		    DMU_POOL_DIRECTORY_OBJECT,
		    DMU_POOL_EMPTY_BPOBJ, tx));
		VERIFY3U(0, ==, dmu_object_free(os, dp->dp_empty_bpobj, tx));
		dp->dp_empty_bpobj = 0;
	}
}

uint64_t
bpobj_alloc(objset_t *os, int blocksize, dmu_tx_t *tx)
{
	int size;

	if (spa_version(dmu_objset_spa(os)) < SPA_VERSION_BPOBJ_ACCOUNT)
		size = BPOBJ_SIZE_V0;
	else if (spa_version(dmu_objset_spa(os)) < SPA_VERSION_DEADLISTS)
		size = BPOBJ_SIZE_V1;
	else if (!spa_feature_is_active(dmu_objset_spa(os),
	    SPA_FEATURE_LIVELIST))
		size = BPOBJ_SIZE_V2;
	else
		size = sizeof (bpobj_phys_t);

	return (dmu_object_alloc(os, DMU_OT_BPOBJ, blocksize,
	    DMU_OT_BPOBJ_HDR, size, tx));
}

void
bpobj_free(objset_t *os, uint64_t obj, dmu_tx_t *tx)
{
	int64_t i;
	bpobj_t bpo;
	dmu_object_info_t doi;
	int epb;
	dmu_buf_t *dbuf = NULL;

	ASSERT(obj != dmu_objset_pool(os)->dp_empty_bpobj);
	VERIFY3U(0, ==, bpobj_open(&bpo, os, obj));

	mutex_enter(&bpo.bpo_lock);

	if (!bpo.bpo_havesubobj || bpo.bpo_phys->bpo_subobjs == 0)
		goto out;

	VERIFY3U(0, ==, dmu_object_info(os, bpo.bpo_phys->bpo_subobjs, &doi));
	epb = doi.doi_data_block_size / sizeof (uint64_t);

	for (i = bpo.bpo_phys->bpo_num_subobjs - 1; i >= 0; i--) {
		uint64_t *objarray;
		uint64_t offset, blkoff;

		offset = i * sizeof (uint64_t);
		blkoff = P2PHASE(i, epb);

		if (dbuf == NULL || dbuf->db_offset > offset) {
			if (dbuf)
				dmu_buf_rele(dbuf, FTAG);
			VERIFY3U(0, ==, dmu_buf_hold(os,
			    bpo.bpo_phys->bpo_subobjs, offset, FTAG, &dbuf, 0));
		}

		ASSERT3U(offset, >=, dbuf->db_offset);
		ASSERT3U(offset, <, dbuf->db_offset + dbuf->db_size);

		objarray = dbuf->db_data;
		bpobj_free(os, objarray[blkoff], tx);
	}
	if (dbuf) {
		dmu_buf_rele(dbuf, FTAG);
		dbuf = NULL;
	}
	VERIFY3U(0, ==, dmu_object_free(os, bpo.bpo_phys->bpo_subobjs, tx));

out:
	mutex_exit(&bpo.bpo_lock);
	bpobj_close(&bpo);

	VERIFY3U(0, ==, dmu_object_free(os, obj, tx));
}

int
bpobj_open(bpobj_t *bpo, objset_t *os, uint64_t object)
{
	dmu_object_info_t doi;
	int err;

	err = dmu_object_info(os, object, &doi);
	if (err)
		return (err);

	memset(bpo, 0, sizeof (*bpo));
	mutex_init(&bpo->bpo_lock, NULL, MUTEX_DEFAULT, NULL);

	ASSERT(bpo->bpo_dbuf == NULL);
	ASSERT(bpo->bpo_phys == NULL);
	ASSERT(object != 0);
	ASSERT3U(doi.doi_type, ==, DMU_OT_BPOBJ);
	ASSERT3U(doi.doi_bonus_type, ==, DMU_OT_BPOBJ_HDR);

	err = dmu_bonus_hold(os, object, bpo, &bpo->bpo_dbuf);
	if (err)
		return (err);

	bpo->bpo_os = os;
	bpo->bpo_object = object;
	bpo->bpo_epb = doi.doi_data_block_size >> SPA_BLKPTRSHIFT;
	bpo->bpo_havecomp = (doi.doi_bonus_size > BPOBJ_SIZE_V0);
	bpo->bpo_havesubobj = (doi.doi_bonus_size > BPOBJ_SIZE_V1);
	bpo->bpo_havefreed = (doi.doi_bonus_size > BPOBJ_SIZE_V2);
	bpo->bpo_phys = bpo->bpo_dbuf->db_data;
	return (0);
}

boolean_t
bpobj_is_open(const bpobj_t *bpo)
{
	return (bpo->bpo_object != 0);
}

void
bpobj_close(bpobj_t *bpo)
{
	/* Lame workaround for closing a bpobj that was never opened. */
	if (bpo->bpo_object == 0)
		return;

	dmu_buf_rele(bpo->bpo_dbuf, bpo);
	if (bpo->bpo_cached_dbuf != NULL)
		dmu_buf_rele(bpo->bpo_cached_dbuf, bpo);
	bpo->bpo_dbuf = NULL;
	bpo->bpo_phys = NULL;
	bpo->bpo_cached_dbuf = NULL;
	bpo->bpo_object = 0;

	mutex_destroy(&bpo->bpo_lock);
}

static boolean_t
bpobj_is_empty_impl(bpobj_t *bpo)
{
	ASSERT(MUTEX_HELD(&bpo->bpo_lock));
	return (bpo->bpo_phys->bpo_num_blkptrs == 0 &&
	    (!bpo->bpo_havesubobj || bpo->bpo_phys->bpo_num_subobjs == 0));
}

boolean_t
bpobj_is_empty(bpobj_t *bpo)
{
	mutex_enter(&bpo->bpo_lock);
	boolean_t is_empty = bpobj_is_empty_impl(bpo);
	mutex_exit(&bpo->bpo_lock);
	return (is_empty);
}

/*
 * A recursive iteration of the bpobjs would be nice here but we run the risk
 * of overflowing function stack space.  Instead, find each subobj and add it
 * to the head of our list so it can be scanned for subjobjs.  Like a
 * recursive implementation, the "deepest" subobjs will be freed first.
 * When a subobj is found to have no additional subojs, free it.
 */
typedef struct bpobj_info {
	bpobj_t *bpi_bpo;
	/*
	 * This object is a subobj of bpi_parent,
	 * at bpi_index in its subobj array.
	 */
	struct bpobj_info *bpi_parent;
	uint64_t bpi_index;
	/* How many of our subobj's are left to process. */
	uint64_t bpi_unprocessed_subobjs;
	/* True after having visited this bpo's directly referenced BPs. */
	boolean_t bpi_visited;
	list_node_t bpi_node;
} bpobj_info_t;

static bpobj_info_t *
bpi_alloc(bpobj_t *bpo, bpobj_info_t *parent, uint64_t index)
{
	bpobj_info_t *bpi = kmem_zalloc(sizeof (bpobj_info_t), KM_SLEEP);
	bpi->bpi_bpo = bpo;
	bpi->bpi_parent = parent;
	bpi->bpi_index = index;
	if (bpo->bpo_havesubobj && bpo->bpo_phys->bpo_subobjs != 0) {
		bpi->bpi_unprocessed_subobjs = bpo->bpo_phys->bpo_num_subobjs;
	}
	return (bpi);
}

/*
 * Update bpobj and all of its parents with new space accounting.
 */
static void
propagate_space_reduction(bpobj_info_t *bpi, int64_t freed,
    int64_t comp_freed, int64_t uncomp_freed, dmu_tx_t *tx)
{

	for (; bpi != NULL; bpi = bpi->bpi_parent) {
		bpobj_t *p = bpi->bpi_bpo;
		ASSERT(dmu_buf_is_dirty(p->bpo_dbuf, tx));
		p->bpo_phys->bpo_bytes -= freed;
		ASSERT3S(p->bpo_phys->bpo_bytes, >=, 0);
		if (p->bpo_havecomp) {
			p->bpo_phys->bpo_comp -= comp_freed;
			p->bpo_phys->bpo_uncomp -= uncomp_freed;
		}
	}
}

static int
bpobj_iterate_blkptrs(bpobj_info_t *bpi, bpobj_itor_t func, void *arg,
    int64_t start, dmu_tx_t *tx, boolean_t free)
{
	int err = 0;
	int64_t freed = 0, comp_freed = 0, uncomp_freed = 0;
	dmu_buf_t *dbuf = NULL;
	bpobj_t *bpo = bpi->bpi_bpo;

	for (int64_t i = bpo->bpo_phys->bpo_num_blkptrs - 1; i >= start; i--) {
		uint64_t offset = i * sizeof (blkptr_t);
		uint64_t blkoff = P2PHASE(i, bpo->bpo_epb);

		if (dbuf == NULL || dbuf->db_offset > offset) {
			if (dbuf)
				dmu_buf_rele(dbuf, FTAG);
			err = dmu_buf_hold(bpo->bpo_os, bpo->bpo_object,
			    offset, FTAG, &dbuf, 0);
			if (err)
				break;
		}

		ASSERT3U(offset, >=, dbuf->db_offset);
		ASSERT3U(offset, <, dbuf->db_offset + dbuf->db_size);

		blkptr_t *bparray = dbuf->db_data;
		blkptr_t *bp = &bparray[blkoff];

		boolean_t bp_freed = BP_GET_FREE(bp);
		err = func(arg, bp, bp_freed, tx);
		if (err)
			break;

		if (free) {
			int sign = bp_freed ? -1 : +1;
			spa_t *spa = dmu_objset_spa(bpo->bpo_os);
			freed += sign * bp_get_dsize_sync(spa, bp);
			comp_freed += sign * BP_GET_PSIZE(bp);
			uncomp_freed += sign * BP_GET_UCSIZE(bp);
			ASSERT(dmu_buf_is_dirty(bpo->bpo_dbuf, tx));
			bpo->bpo_phys->bpo_num_blkptrs--;
			ASSERT3S(bpo->bpo_phys->bpo_num_blkptrs, >=, 0);
			if (bp_freed) {
				ASSERT(bpo->bpo_havefreed);
				bpo->bpo_phys->bpo_num_freed--;
				ASSERT3S(bpo->bpo_phys->bpo_num_freed, >=, 0);
			}
		}
	}
	if (free) {
		propagate_space_reduction(bpi, freed, comp_freed,
		    uncomp_freed, tx);
		VERIFY0(dmu_free_range(bpo->bpo_os,
		    bpo->bpo_object,
		    bpo->bpo_phys->bpo_num_blkptrs * sizeof (blkptr_t),
		    DMU_OBJECT_END, tx));
	}
	if (dbuf) {
		dmu_buf_rele(dbuf, FTAG);
		dbuf = NULL;
	}
	return (err);
}

/*
 * Given an initial bpo, start by freeing the BPs that are directly referenced
 * by that bpo. If the bpo has subobjs, read in its last subobj and push the
 * subobj to our stack. By popping items off our stack, eventually we will
 * encounter a bpo that has no subobjs.  We can free its bpobj_info_t, and if
 * requested also free the now-empty bpo from disk and decrement
 * its parent's subobj count. We continue popping each subobj from our stack,
 * visiting its last subobj until they too have no more subobjs, and so on.
 */
static int
bpobj_iterate_impl(bpobj_t *initial_bpo, bpobj_itor_t func, void *arg,
    dmu_tx_t *tx, boolean_t free, uint64_t *bpobj_size)
{
	list_t stack;
	bpobj_info_t *bpi;
	int err = 0;

	/*
	 * Create a "stack" for us to work with without worrying about
	 * stack overflows. Initialize it with the initial_bpo.
	 */
	list_create(&stack, sizeof (bpobj_info_t),
	    offsetof(bpobj_info_t, bpi_node));
	mutex_enter(&initial_bpo->bpo_lock);

	if (bpobj_size != NULL)
		*bpobj_size = initial_bpo->bpo_phys->bpo_num_blkptrs;

	list_insert_head(&stack, bpi_alloc(initial_bpo, NULL, 0));

	while ((bpi = list_head(&stack)) != NULL) {
		bpobj_t *bpo = bpi->bpi_bpo;

		ASSERT3P(bpo, !=, NULL);
		ASSERT(MUTEX_HELD(&bpo->bpo_lock));
		ASSERT(bpobj_is_open(bpo));

		if (free)
			dmu_buf_will_dirty(bpo->bpo_dbuf, tx);

		if (bpi->bpi_visited == B_FALSE) {
			err = bpobj_iterate_blkptrs(bpi, func, arg, 0, tx,
			    free);
			bpi->bpi_visited = B_TRUE;
			if (err != 0)
				break;
		}
		/*
		 * We've finished with this bpo's directly-referenced BP's and
		 * it has no more unprocessed subobjs. We can free its
		 * bpobj_info_t (unless it is the topmost, initial_bpo).
		 * If we are freeing from disk, we can also do that.
		 */
		if (bpi->bpi_unprocessed_subobjs == 0) {
			/*
			 * If there are no entries, there should
			 * be no bytes.
			 */
			if (bpobj_is_empty_impl(bpo)) {
				ASSERT0(bpo->bpo_phys->bpo_bytes);
				ASSERT0(bpo->bpo_phys->bpo_comp);
				ASSERT0(bpo->bpo_phys->bpo_uncomp);
			}

			/* The initial_bpo has no parent and is not closed. */
			if (bpi->bpi_parent != NULL) {
				if (free) {
					bpobj_t *p = bpi->bpi_parent->bpi_bpo;

					ASSERT0(bpo->bpo_phys->bpo_num_blkptrs);
					ASSERT3U(p->bpo_phys->bpo_num_subobjs,
					    >, 0);
					ASSERT3U(bpi->bpi_index, ==,
					    p->bpo_phys->bpo_num_subobjs - 1);
					ASSERT(dmu_buf_is_dirty(bpo->bpo_dbuf,
					    tx));

					p->bpo_phys->bpo_num_subobjs--;

					VERIFY0(dmu_free_range(p->bpo_os,
					    p->bpo_phys->bpo_subobjs,
					    bpi->bpi_index * sizeof (uint64_t),
					    sizeof (uint64_t), tx));

					/* eliminate the empty subobj list */
					if (bpo->bpo_havesubobj &&
					    bpo->bpo_phys->bpo_subobjs != 0) {
						ASSERT0(bpo->bpo_phys->
						    bpo_num_subobjs);
						err = dmu_object_free(
						    bpo->bpo_os,
						    bpo->bpo_phys->bpo_subobjs,
						    tx);
						if (err)
							break;
						bpo->bpo_phys->bpo_subobjs = 0;
					}
					err = dmu_object_free(p->bpo_os,
					    bpo->bpo_object, tx);
					if (err)
						break;
				}

				mutex_exit(&bpo->bpo_lock);
				bpobj_close(bpo);
				kmem_free(bpo, sizeof (bpobj_t));
			} else {
				mutex_exit(&bpo->bpo_lock);
			}

			/*
			 * Finished processing this bpo. Unlock, and free
			 * our "stack" info.
			 */
			list_remove_head(&stack);
			kmem_free(bpi, sizeof (bpobj_info_t));
		} else {
			/*
			 * We have unprocessed subobjs. Process the next one.
			 */
			ASSERT(bpo->bpo_havecomp);
			ASSERT3P(bpobj_size, ==, NULL);

			/* Add the last subobj to stack. */
			int64_t i = bpi->bpi_unprocessed_subobjs - 1;
			uint64_t offset = i * sizeof (uint64_t);

			uint64_t obj_from_sublist;
			err = dmu_read(bpo->bpo_os, bpo->bpo_phys->bpo_subobjs,
			    offset, sizeof (uint64_t), &obj_from_sublist,
			    DMU_READ_PREFETCH);
			if (err)
				break;
			bpobj_t *sublist = kmem_alloc(sizeof (bpobj_t),
			    KM_SLEEP);

			err = bpobj_open(sublist, bpo->bpo_os,
			    obj_from_sublist);
			if (err)
				break;

			list_insert_head(&stack, bpi_alloc(sublist, bpi, i));
			mutex_enter(&sublist->bpo_lock);
			bpi->bpi_unprocessed_subobjs--;
		}
	}
	/*
	 * Cleanup anything left on the "stack" after we left the loop.
	 * Every bpo on the stack is locked so we must remember to undo
	 * that now (in LIFO order).
	 */
	while ((bpi = list_remove_head(&stack)) != NULL) {
		bpobj_t *bpo = bpi->bpi_bpo;
		ASSERT(err != 0);
		ASSERT3P(bpo, !=, NULL);

		mutex_exit(&bpo->bpo_lock);

		/* do not free the initial_bpo */
		if (bpi->bpi_parent != NULL) {
			bpobj_close(bpi->bpi_bpo);
			kmem_free(bpi->bpi_bpo, sizeof (bpobj_t));
		}
		kmem_free(bpi, sizeof (bpobj_info_t));
	}

	list_destroy(&stack);

	return (err);
}

/*
 * Iterate and remove the entries.  If func returns nonzero, iteration
 * will stop and that entry will not be removed.
 */
int
bpobj_iterate(bpobj_t *bpo, bpobj_itor_t func, void *arg, dmu_tx_t *tx)
{
	return (bpobj_iterate_impl(bpo, func, arg, tx, B_TRUE, NULL));
}

/*
 * Iterate the entries.  If func returns nonzero, iteration will stop.
 *
 * If there are no subobjs:
 *
 * *bpobj_size can be used to return the number of block pointers in the
 * bpobj.  Note that this may be different from the number of block pointers
 * that are iterated over, if iteration is terminated early (e.g. by the func
 * returning nonzero).
 *
 * If there are concurrent (or subsequent) modifications to the bpobj then the
 * returned *bpobj_size can be passed as "start" to
 * livelist_bpobj_iterate_from_nofree() to iterate the newly added entries.
 */
int
bpobj_iterate_nofree(bpobj_t *bpo, bpobj_itor_t func, void *arg,
    uint64_t *bpobj_size)
{
	return (bpobj_iterate_impl(bpo, func, arg, NULL, B_FALSE, bpobj_size));
}

/*
 * Iterate over the blkptrs in the bpobj beginning at index start. If func
 * returns nonzero, iteration will stop. This is a livelist specific function
 * since it assumes that there are no subobjs present.
 */
int
livelist_bpobj_iterate_from_nofree(bpobj_t *bpo, bpobj_itor_t func, void *arg,
    int64_t start)
{
	if (bpo->bpo_havesubobj)
		VERIFY0(bpo->bpo_phys->bpo_subobjs);
	bpobj_info_t *bpi = bpi_alloc(bpo, NULL, 0);
	int err = bpobj_iterate_blkptrs(bpi, func, arg, start, NULL, B_FALSE);
	kmem_free(bpi, sizeof (bpobj_info_t));
	return (err);
}

/*
 * Logically add subobj's contents to the parent bpobj.
 *
 * In the most general case, this is accomplished in constant time by adding
 * a reference to subobj.  This case is used when enqueuing a large subobj:
 * +--------------+                        +--------------+
 * | bpobj        |----------------------->| subobj list  |
 * +----+----+----+----+----+              +-----+-----+--+--+
 * | bp | bp | bp | bp | bp |              | obj | obj | obj |
 * +----+----+----+----+----+              +-----+-----+-----+
 *
 * +--------------+                        +--------------+
 * | sub-bpobj    |----------------------> | subsubobj    |
 * +----+----+----+----+---------+----+    +-----+-----+--+--------+-----+
 * | bp | bp | bp | bp |   ...   | bp |    | obj | obj |    ...    | obj |
 * +----+----+----+----+---------+----+    +-----+-----+-----------+-----+
 *
 * Result: sub-bpobj added to parent's subobj list.
 * +--------------+                        +--------------+
 * | bpobj        |----------------------->| subobj list  |
 * +----+----+----+----+----+              +-----+-----+--+--+-----+
 * | bp | bp | bp | bp | bp |              | obj | obj | obj | OBJ |
 * +----+----+----+----+----+              +-----+-----+-----+--|--+
 *                                                              |
 *       /-----------------------------------------------------/
 *       v
 * +--------------+                        +--------------+
 * | sub-bpobj    |----------------------> | subsubobj    |
 * +----+----+----+----+---------+----+    +-----+-----+--+--------+-----+
 * | bp | bp | bp | bp |   ...   | bp |    | obj | obj |    ...    | obj |
 * +----+----+----+----+---------+----+    +-----+-----+-----------+-----+
 *
 *
 * In a common case, the subobj is small: its bp's and its list of subobj's
 * are each stored in a single block.  In this case we copy the subobj's
 * contents to the parent:
 * +--------------+                        +--------------+
 * | bpobj        |----------------------->| subobj list  |
 * +----+----+----+----+----+              +-----+-----+--+--+
 * | bp | bp | bp | bp | bp |              | obj | obj | obj |
 * +----+----+----+----+----+              +-----+-----+-----+
 *                          ^                                ^
 * +--------------+         |              +--------------+  |
 * | sub-bpobj    |---------^------------> | subsubobj    |  ^
 * +----+----+----+         |              +-----+-----+--+  |
 * | BP | BP |-->-->-->-->-/               | OBJ | OBJ |-->-/
 * +----+----+                             +-----+-----+
 *
 * Result: subobj destroyed, contents copied to parent:
 * +--------------+                        +--------------+
 * | bpobj        |----------------------->| subobj list  |
 * +----+----+----+----+----+----+----+    +-----+-----+--+--+-----+-----+
 * | bp | bp | bp | bp | bp | BP | BP |    | obj | obj | obj | OBJ | OBJ |
 * +----+----+----+----+----+----+----+    +-----+-----+-----+-----+-----+
 *
 *
 * If the subobj has many BP's but few subobj's, we can copy the sub-subobj's
 * but retain the sub-bpobj:
 * +--------------+                        +--------------+
 * | bpobj        |----------------------->| subobj list  |
 * +----+----+----+----+----+              +-----+-----+--+--+
 * | bp | bp | bp | bp | bp |              | obj | obj | obj |
 * +----+----+----+----+----+              +-----+-----+-----+
 *                                                           ^
 * +--------------+                        +--------------+  |
 * | sub-bpobj    |----------------------> | subsubobj    |  ^
 * +----+----+----+----+---------+----+    +-----+-----+--+  |
 * | bp | bp | bp | bp |   ...   | bp |    | OBJ | OBJ |-->-/
 * +----+----+----+----+---------+----+    +-----+-----+
 *
 * Result: sub-sub-bpobjs and subobj added to parent's subobj list.
 * +--------------+                     +--------------+
 * | bpobj        |-------------------->| subobj list  |
 * +----+----+----+----+----+           +-----+-----+--+--+-----+-----+------+
 * | bp | bp | bp | bp | bp |           | obj | obj | obj | OBJ | OBJ | OBJ* |
 * +----+----+----+----+----+           +-----+-----+-----+-----+-----+--|---+
 *                                                                       |
 *       /--------------------------------------------------------------/
 *       v
 * +--------------+
 * | sub-bpobj    |
 * +----+----+----+----+---------+----+
 * | bp | bp | bp | bp |   ...   | bp |
 * +----+----+----+----+---------+----+
 */
void
bpobj_enqueue_subobj(bpobj_t *bpo, uint64_t subobj, dmu_tx_t *tx)
{
	bpobj_t subbpo;
	uint64_t used, comp, uncomp, subsubobjs;
	boolean_t copy_subsub = B_TRUE;
	boolean_t copy_bps = B_TRUE;

	ASSERT(bpobj_is_open(bpo));
	ASSERT(subobj != 0);
	ASSERT(bpo->bpo_havesubobj);
	ASSERT(bpo->bpo_havecomp);
	ASSERT(bpo->bpo_object != dmu_objset_pool(bpo->bpo_os)->dp_empty_bpobj);

	if (subobj == dmu_objset_pool(bpo->bpo_os)->dp_empty_bpobj) {
		bpobj_decr_empty(bpo->bpo_os, tx);
		return;
	}

	VERIFY3U(0, ==, bpobj_open(&subbpo, bpo->bpo_os, subobj));
	VERIFY3U(0, ==, bpobj_space(&subbpo, &used, &comp, &uncomp));

	if (bpobj_is_empty(&subbpo)) {
		/* No point in having an empty subobj. */
		bpobj_close(&subbpo);
		bpobj_free(bpo->bpo_os, subobj, tx);
		return;
	}

	mutex_enter(&bpo->bpo_lock);
	dmu_buf_will_dirty(bpo->bpo_dbuf, tx);

	dmu_object_info_t doi;

	if (bpo->bpo_phys->bpo_subobjs != 0) {
		ASSERT0(dmu_object_info(bpo->bpo_os, bpo->bpo_phys->bpo_subobjs,
		    &doi));
		ASSERT3U(doi.doi_type, ==, DMU_OT_BPOBJ_SUBOBJ);
	}

	/*
	 * If subobj has only one block of subobjs, then move subobj's
	 * subobjs to bpo's subobj list directly.  This reduces recursion in
	 * bpobj_iterate due to nested subobjs.
	 */
	subsubobjs = subbpo.bpo_phys->bpo_subobjs;
	if (subsubobjs != 0) {
		VERIFY0(dmu_object_info(bpo->bpo_os, subsubobjs, &doi));
		if (doi.doi_max_offset > doi.doi_data_block_size) {
			copy_subsub = B_FALSE;
		}
	}

	/*
	 * If, in addition to having only one block of subobj's, subobj has
	 * only one block of bp's, then move subobj's bp's to bpo's bp list
	 * directly. This reduces recursion in bpobj_iterate due to nested
	 * subobjs.
	 */
	VERIFY3U(0, ==, dmu_object_info(bpo->bpo_os, subobj, &doi));
	if (doi.doi_max_offset > doi.doi_data_block_size || !copy_subsub) {
		copy_bps = B_FALSE;
	}

	if (copy_subsub && subsubobjs != 0) {
		dmu_buf_t *subdb;
		uint64_t numsubsub = subbpo.bpo_phys->bpo_num_subobjs;

		VERIFY0(dmu_buf_hold(bpo->bpo_os, subsubobjs,
		    0, FTAG, &subdb, 0));
		/*
		 * Make sure that we are not asking dmu_write()
		 * to write more data than we have in our buffer.
		 */
		VERIFY3U(subdb->db_size, >=,
		    numsubsub * sizeof (subobj));
		if (bpo->bpo_phys->bpo_subobjs == 0) {
			bpo->bpo_phys->bpo_subobjs =
			    dmu_object_alloc(bpo->bpo_os,
			    DMU_OT_BPOBJ_SUBOBJ, SPA_OLD_MAXBLOCKSIZE,
			    DMU_OT_NONE, 0, tx);
		}
		dmu_write(bpo->bpo_os, bpo->bpo_phys->bpo_subobjs,
		    bpo->bpo_phys->bpo_num_subobjs * sizeof (subobj),
		    numsubsub * sizeof (subobj), subdb->db_data, tx);
		dmu_buf_rele(subdb, FTAG);
		bpo->bpo_phys->bpo_num_subobjs += numsubsub;

		dmu_buf_will_dirty(subbpo.bpo_dbuf, tx);
		subbpo.bpo_phys->bpo_subobjs = 0;
		VERIFY0(dmu_object_free(bpo->bpo_os, subsubobjs, tx));
	}

	if (copy_bps) {
		dmu_buf_t *bps;
		uint64_t numbps = subbpo.bpo_phys->bpo_num_blkptrs;

		ASSERT(copy_subsub);
		VERIFY0(dmu_buf_hold(bpo->bpo_os, subobj,
		    0, FTAG, &bps, 0));

		/*
		 * Make sure that we are not asking dmu_write()
		 * to write more data than we have in our buffer.
		 */
		VERIFY3U(bps->db_size, >=, numbps * sizeof (blkptr_t));
		dmu_write(bpo->bpo_os, bpo->bpo_object,
		    bpo->bpo_phys->bpo_num_blkptrs * sizeof (blkptr_t),
		    numbps * sizeof (blkptr_t),
		    bps->db_data, tx);
		dmu_buf_rele(bps, FTAG);
		bpo->bpo_phys->bpo_num_blkptrs += numbps;

		bpobj_close(&subbpo);
		VERIFY0(dmu_object_free(bpo->bpo_os, subobj, tx));
	} else {
		bpobj_close(&subbpo);
		if (bpo->bpo_phys->bpo_subobjs == 0) {
			bpo->bpo_phys->bpo_subobjs =
			    dmu_object_alloc(bpo->bpo_os,
			    DMU_OT_BPOBJ_SUBOBJ, SPA_OLD_MAXBLOCKSIZE,
			    DMU_OT_NONE, 0, tx);
		}

		dmu_write(bpo->bpo_os, bpo->bpo_phys->bpo_subobjs,
		    bpo->bpo_phys->bpo_num_subobjs * sizeof (subobj),
		    sizeof (subobj), &subobj, tx);
		bpo->bpo_phys->bpo_num_subobjs++;
	}

	bpo->bpo_phys->bpo_bytes += used;
	bpo->bpo_phys->bpo_comp += comp;
	bpo->bpo_phys->bpo_uncomp += uncomp;
	mutex_exit(&bpo->bpo_lock);

}

void
bpobj_enqueue(bpobj_t *bpo, const blkptr_t *bp, boolean_t bp_freed,
    dmu_tx_t *tx)
{
	blkptr_t stored_bp = *bp;
	uint64_t offset;
	int blkoff;
	blkptr_t *bparray;

	ASSERT(bpobj_is_open(bpo));
	ASSERT(!BP_IS_HOLE(bp));
	ASSERT(bpo->bpo_object != dmu_objset_pool(bpo->bpo_os)->dp_empty_bpobj);

	if (BP_IS_EMBEDDED(bp)) {
		/*
		 * The bpobj will compress better without the payload.
		 *
		 * Note that we store EMBEDDED bp's because they have an
		 * uncompressed size, which must be accounted for.  An
		 * alternative would be to add their size to bpo_uncomp
		 * without storing the bp, but that would create additional
		 * complications: bpo_uncomp would be inconsistent with the
		 * set of BP's stored, and bpobj_iterate() wouldn't visit
		 * all the space accounted for in the bpobj.
		 */
		memset(&stored_bp, 0, sizeof (stored_bp));
		stored_bp.blk_prop = bp->blk_prop;
		stored_bp.blk_birth = bp->blk_birth;
	} else if (!BP_GET_DEDUP(bp)) {
		/* The bpobj will compress better without the checksum */
		memset(&stored_bp.blk_cksum, 0, sizeof (stored_bp.blk_cksum));
	}

	stored_bp.blk_fill = 0;
	BP_SET_FREE(&stored_bp, bp_freed);

	mutex_enter(&bpo->bpo_lock);

	offset = bpo->bpo_phys->bpo_num_blkptrs * sizeof (stored_bp);
	blkoff = P2PHASE(bpo->bpo_phys->bpo_num_blkptrs, bpo->bpo_epb);

	if (bpo->bpo_cached_dbuf == NULL ||
	    offset < bpo->bpo_cached_dbuf->db_offset ||
	    offset >= bpo->bpo_cached_dbuf->db_offset +
	    bpo->bpo_cached_dbuf->db_size) {
		if (bpo->bpo_cached_dbuf)
			dmu_buf_rele(bpo->bpo_cached_dbuf, bpo);
		VERIFY3U(0, ==, dmu_buf_hold(bpo->bpo_os, bpo->bpo_object,
		    offset, bpo, &bpo->bpo_cached_dbuf, 0));
	}

	dmu_buf_will_dirty(bpo->bpo_cached_dbuf, tx);
	bparray = bpo->bpo_cached_dbuf->db_data;
	bparray[blkoff] = stored_bp;

	dmu_buf_will_dirty(bpo->bpo_dbuf, tx);
	bpo->bpo_phys->bpo_num_blkptrs++;
	int sign = bp_freed ? -1 : +1;
	bpo->bpo_phys->bpo_bytes += sign *
	    bp_get_dsize_sync(dmu_objset_spa(bpo->bpo_os), bp);
	if (bpo->bpo_havecomp) {
		bpo->bpo_phys->bpo_comp += sign * BP_GET_PSIZE(bp);
		bpo->bpo_phys->bpo_uncomp += sign * BP_GET_UCSIZE(bp);
	}
	if (bp_freed) {
		ASSERT(bpo->bpo_havefreed);
		bpo->bpo_phys->bpo_num_freed++;
	}
	mutex_exit(&bpo->bpo_lock);
}

struct space_range_arg {
	spa_t *spa;
	uint64_t mintxg;
	uint64_t maxtxg;
	uint64_t used;
	uint64_t comp;
	uint64_t uncomp;
};

static int
space_range_cb(void *arg, const blkptr_t *bp, boolean_t bp_freed, dmu_tx_t *tx)
{
	(void) bp_freed, (void) tx;
	struct space_range_arg *sra = arg;

	if (bp->blk_birth > sra->mintxg && bp->blk_birth <= sra->maxtxg) {
		if (dsl_pool_sync_context(spa_get_dsl(sra->spa)))
			sra->used += bp_get_dsize_sync(sra->spa, bp);
		else
			sra->used += bp_get_dsize(sra->spa, bp);
		sra->comp += BP_GET_PSIZE(bp);
		sra->uncomp += BP_GET_UCSIZE(bp);
	}
	return (0);
}

int
bpobj_space(bpobj_t *bpo, uint64_t *usedp, uint64_t *compp, uint64_t *uncompp)
{
	ASSERT(bpobj_is_open(bpo));
	mutex_enter(&bpo->bpo_lock);

	*usedp = bpo->bpo_phys->bpo_bytes;
	if (bpo->bpo_havecomp) {
		*compp = bpo->bpo_phys->bpo_comp;
		*uncompp = bpo->bpo_phys->bpo_uncomp;
		mutex_exit(&bpo->bpo_lock);
		return (0);
	} else {
		mutex_exit(&bpo->bpo_lock);
		return (bpobj_space_range(bpo, 0, UINT64_MAX,
		    usedp, compp, uncompp));
	}
}

/*
 * Return the amount of space in the bpobj which is:
 * mintxg < blk_birth <= maxtxg
 */
int
bpobj_space_range(bpobj_t *bpo, uint64_t mintxg, uint64_t maxtxg,
    uint64_t *usedp, uint64_t *compp, uint64_t *uncompp)
{
	struct space_range_arg sra = { 0 };
	int err;

	ASSERT(bpobj_is_open(bpo));

	/*
	 * As an optimization, if they want the whole txg range, just
	 * get bpo_bytes rather than iterating over the bps.
	 */
	if (mintxg < TXG_INITIAL && maxtxg == UINT64_MAX && bpo->bpo_havecomp)
		return (bpobj_space(bpo, usedp, compp, uncompp));

	sra.spa = dmu_objset_spa(bpo->bpo_os);
	sra.mintxg = mintxg;
	sra.maxtxg = maxtxg;

	err = bpobj_iterate_nofree(bpo, space_range_cb, &sra, NULL);
	*usedp = sra.used;
	*compp = sra.comp;
	*uncompp = sra.uncomp;
	return (err);
}

/*
 * A bpobj_itor_t to append blkptrs to a bplist. Note that while blkptrs in a
 * bpobj are designated as free or allocated that information is not preserved
 * in bplists.
 */
int
bplist_append_cb(void *arg, const blkptr_t *bp, boolean_t bp_freed,
    dmu_tx_t *tx)
{
	(void) bp_freed, (void) tx;
	bplist_t *bpl = arg;
	bplist_append(bpl, bp);
	return (0);
}
