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
 * Copyright (c) 2017, 2018 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/txg.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_traverse.h>
#include <sys/dmu_redact.h>
#include <sys/bqueue.h>
#include <sys/objlist.h>
#include <sys/dmu_tx.h>
#ifdef _KERNEL
#include <sys/zfs_vfsops.h>
#include <sys/zap.h>
#include <sys/zfs_znode.h>
#endif

/*
 * This controls the number of entries in the buffer the redaction_list_update
 * synctask uses to buffer writes to the redaction list.
 */
static const int redact_sync_bufsize = 1024;

/*
 * Controls how often to update the redaction list when creating a redaction
 * list.
 */
static const uint64_t redaction_list_update_interval_ns =
    1000 * 1000 * 1000ULL; /* 1s */

/*
 * This tunable controls the length of the queues that zfs redact worker threads
 * use to communicate.  If the dmu_redact_snap thread is blocking on these
 * queues, this variable may need to be increased.  If there is a significant
 * slowdown at the start of a redact operation as these threads consume all the
 * available IO resources, or the queues are consuming too much memory, this
 * variable may need to be decreased.
 */
static const int zfs_redact_queue_length = 1024 * 1024;

/*
 * These tunables control the fill fraction of the queues by zfs redact. The
 * fill fraction controls the frequency with which threads have to be
 * cv_signaled. If a lot of cpu time is being spent on cv_signal, then these
 * should be tuned down.  If the queues empty before the signalled thread can
 * catch up, then these should be tuned up.
 */
static const uint64_t zfs_redact_queue_ff = 20;

struct redact_record {
	bqueue_node_t		ln;
	boolean_t		eos_marker; /* Marks the end of the stream */
	uint64_t		start_object;
	uint64_t		start_blkid;
	uint64_t		end_object;
	uint64_t		end_blkid;
	uint8_t			indblkshift;
	uint32_t		datablksz;
};

struct redact_thread_arg {
	bqueue_t	q;
	objset_t	*os;		/* Objset to traverse */
	dsl_dataset_t	*ds;		/* Dataset to traverse */
	struct redact_record *current_record;
	int		error_code;
	boolean_t	cancel;
	zbookmark_phys_t resume;
	objlist_t	*deleted_objs;
	uint64_t	*num_blocks_visited;
	uint64_t	ignore_object;	/* ignore further callbacks on this */
	uint64_t	txg; /* txg to traverse since */
};

/*
 * The redaction node is a wrapper around the redaction record that is used
 * by the redaction merging thread to sort the records and determine overlaps.
 *
 * It contains two nodes; one sorts the records by their start_zb, and the other
 * sorts the records by their end_zb.
 */
struct redact_node {
	avl_node_t			avl_node_start;
	avl_node_t			avl_node_end;
	struct redact_record		*record;
	struct redact_thread_arg	*rt_arg;
	uint32_t			thread_num;
};

struct merge_data {
	list_t				md_redact_block_pending;
	redact_block_phys_t		md_coalesce_block;
	uint64_t			md_last_time;
	redact_block_phys_t		md_furthest[TXG_SIZE];
	/* Lists of struct redact_block_list_node. */
	list_t				md_blocks[TXG_SIZE];
	boolean_t			md_synctask_txg[TXG_SIZE];
	uint64_t			md_latest_synctask_txg;
	redaction_list_t		*md_redaction_list;
};

/*
 * A wrapper around struct redact_block so it can be stored in a list_t.
 */
struct redact_block_list_node {
	redact_block_phys_t	block;
	list_node_t		node;
};

/*
 * We've found a new redaction candidate.  In order to improve performance, we
 * coalesce these blocks when they're adjacent to each other.  This function
 * handles that.  If the new candidate block range is immediately after the
 * range we're building, coalesce it into the range we're building.  Otherwise,
 * put the record we're building on the queue, and update the build pointer to
 * point to the new record.
 */
static void
record_merge_enqueue(bqueue_t *q, struct redact_record **build,
    struct redact_record *new)
{
	if (new->eos_marker) {
		if (*build != NULL)
			bqueue_enqueue(q, *build, sizeof (*build));
		bqueue_enqueue_flush(q, new, sizeof (*new));
		return;
	}
	if (*build == NULL) {
		*build = new;
		return;
	}
	struct redact_record *curbuild = *build;
	if ((curbuild->end_object == new->start_object &&
	    curbuild->end_blkid + 1 == new->start_blkid &&
	    curbuild->end_blkid != UINT64_MAX) ||
	    (curbuild->end_object + 1 == new->start_object &&
	    curbuild->end_blkid == UINT64_MAX && new->start_blkid == 0)) {
		curbuild->end_object = new->end_object;
		curbuild->end_blkid = new->end_blkid;
		kmem_free(new, sizeof (*new));
	} else {
		bqueue_enqueue(q, curbuild, sizeof (*curbuild));
		*build = new;
	}
}
#ifdef _KERNEL
struct objnode {
	avl_node_t node;
	uint64_t obj;
};

static int
objnode_compare(const void *o1, const void *o2)
{
	const struct objnode *obj1 = o1;
	const struct objnode *obj2 = o2;
	if (obj1->obj < obj2->obj)
		return (-1);
	if (obj1->obj > obj2->obj)
		return (1);
	return (0);
}


static objlist_t *
zfs_get_deleteq(objset_t *os)
{
	objlist_t *deleteq_objlist = objlist_create();
	uint64_t deleteq_obj;
	zap_cursor_t zc;
	zap_attribute_t za;
	dmu_object_info_t doi;

	ASSERT3U(os->os_phys->os_type, ==, DMU_OST_ZFS);
	VERIFY0(dmu_object_info(os, MASTER_NODE_OBJ, &doi));
	ASSERT3U(doi.doi_type, ==, DMU_OT_MASTER_NODE);

	VERIFY0(zap_lookup(os, MASTER_NODE_OBJ,
	    ZFS_UNLINKED_SET, sizeof (uint64_t), 1, &deleteq_obj));

	/*
	 * In order to insert objects into the objlist, they must be in sorted
	 * order. We don't know what order we'll get them out of the ZAP in, so
	 * we insert them into and remove them from an avl_tree_t to sort them.
	 */
	avl_tree_t at;
	avl_create(&at, objnode_compare, sizeof (struct objnode),
	    offsetof(struct objnode, node));

	for (zap_cursor_init(&zc, os, deleteq_obj);
	    zap_cursor_retrieve(&zc, &za) == 0; zap_cursor_advance(&zc)) {
		struct objnode *obj = kmem_zalloc(sizeof (*obj), KM_SLEEP);
		obj->obj = za.za_first_integer;
		avl_add(&at, obj);
	}
	zap_cursor_fini(&zc);

	struct objnode *next, *found = avl_first(&at);
	while (found != NULL) {
		next = AVL_NEXT(&at, found);
		objlist_insert(deleteq_objlist, found->obj);
		found = next;
	}

	void *cookie = NULL;
	while ((found = avl_destroy_nodes(&at, &cookie)) != NULL)
		kmem_free(found, sizeof (*found));
	avl_destroy(&at);
	return (deleteq_objlist);
}
#endif

/*
 * This is the callback function to traverse_dataset for the redaction threads
 * for dmu_redact_snap.  This thread is responsible for creating redaction
 * records for all the data that is modified by the snapshots we're redacting
 * with respect to.  Redaction records represent ranges of data that have been
 * modified by one of the redaction snapshots, and are stored in the
 * redact_record struct. We need to create redaction records for three
 * cases:
 *
 * First, if there's a normal write, we need to create a redaction record for
 * that block.
 *
 * Second, if there's a hole, we need to create a redaction record that covers
 * the whole range of the hole.  If the hole is in the meta-dnode, it must cover
 * every block in all of the objects in the hole.
 *
 * Third, if there is a deleted object, we need to create a redaction record for
 * all of the blocks in that object.
 */
static int
redact_cb(spa_t *spa, zilog_t *zilog, const blkptr_t *bp,
    const zbookmark_phys_t *zb, const struct dnode_phys *dnp, void *arg)
{
	(void) spa, (void) zilog;
	struct redact_thread_arg *rta = arg;
	struct redact_record *record;

	ASSERT(zb->zb_object == DMU_META_DNODE_OBJECT ||
	    zb->zb_object >= rta->resume.zb_object);

	if (rta->cancel)
		return (SET_ERROR(EINTR));

	if (rta->ignore_object == zb->zb_object)
		return (0);

	/*
	 * If we're visiting a dnode, we need to handle the case where the
	 * object has been deleted.
	 */
	if (zb->zb_level == ZB_DNODE_LEVEL) {
		ASSERT3U(zb->zb_level, ==, ZB_DNODE_LEVEL);

		if (zb->zb_object == 0)
			return (0);

		/*
		 * If the object has been deleted, redact all of the blocks in
		 * it.
		 */
		if (dnp->dn_type == DMU_OT_NONE ||
		    objlist_exists(rta->deleted_objs, zb->zb_object)) {
			rta->ignore_object = zb->zb_object;
			record = kmem_zalloc(sizeof (struct redact_record),
			    KM_SLEEP);

			record->eos_marker = B_FALSE;
			record->start_object = record->end_object =
			    zb->zb_object;
			record->start_blkid = 0;
			record->end_blkid = UINT64_MAX;
			record_merge_enqueue(&rta->q,
			    &rta->current_record, record);
		}
		return (0);
	} else if (zb->zb_level < 0) {
		return (0);
	} else if (zb->zb_level > 0 && !BP_IS_HOLE(bp)) {
		/*
		 * If this is an indirect block, but not a hole, it doesn't
		 * provide any useful information for redaction, so ignore it.
		 */
		return (0);
	}

	/*
	 * At this point, there are two options left for the type of block we're
	 * looking at.  Either this is a hole (which could be in the dnode or
	 * the meta-dnode), or it's a level 0 block of some sort.  If it's a
	 * hole, we create a redaction record that covers the whole range.  If
	 * the hole is in a dnode, we need to redact all the blocks in that
	 * hole.  If the hole is in the meta-dnode, we instead need to redact
	 * all blocks in every object covered by that hole.  If it's a level 0
	 * block, we only need to redact that single block.
	 */
	record = kmem_zalloc(sizeof (struct redact_record), KM_SLEEP);
	record->eos_marker = B_FALSE;

	record->start_object = record->end_object = zb->zb_object;
	if (BP_IS_HOLE(bp)) {
		record->start_blkid = zb->zb_blkid *
		    bp_span_in_blocks(dnp->dn_indblkshift, zb->zb_level);

		record->end_blkid = ((zb->zb_blkid + 1) *
		    bp_span_in_blocks(dnp->dn_indblkshift, zb->zb_level)) - 1;

		if (zb->zb_object == DMU_META_DNODE_OBJECT) {
			record->start_object = record->start_blkid *
			    ((SPA_MINBLOCKSIZE * dnp->dn_datablkszsec) /
			    sizeof (dnode_phys_t));
			record->start_blkid = 0;
			record->end_object = ((record->end_blkid +
			    1) * ((SPA_MINBLOCKSIZE * dnp->dn_datablkszsec) /
			    sizeof (dnode_phys_t))) - 1;
			record->end_blkid = UINT64_MAX;
		}
	} else if (zb->zb_level != 0 ||
	    zb->zb_object == DMU_META_DNODE_OBJECT) {
		kmem_free(record, sizeof (*record));
		return (0);
	} else {
		record->start_blkid = record->end_blkid = zb->zb_blkid;
	}
	record->indblkshift = dnp->dn_indblkshift;
	record->datablksz = dnp->dn_datablkszsec << SPA_MINBLOCKSHIFT;
	record_merge_enqueue(&rta->q, &rta->current_record, record);

	return (0);
}

static __attribute__((noreturn)) void
redact_traverse_thread(void *arg)
{
	struct redact_thread_arg *rt_arg = arg;
	int err;
	struct redact_record *data;
#ifdef _KERNEL
	if (rt_arg->os->os_phys->os_type == DMU_OST_ZFS)
		rt_arg->deleted_objs = zfs_get_deleteq(rt_arg->os);
	else
		rt_arg->deleted_objs = objlist_create();
#else
	rt_arg->deleted_objs = objlist_create();
#endif

	err = traverse_dataset_resume(rt_arg->ds, rt_arg->txg,
	    &rt_arg->resume, TRAVERSE_PRE | TRAVERSE_PREFETCH_METADATA,
	    redact_cb, rt_arg);

	if (err != EINTR)
		rt_arg->error_code = err;
	objlist_destroy(rt_arg->deleted_objs);
	data = kmem_zalloc(sizeof (*data), KM_SLEEP);
	data->eos_marker = B_TRUE;
	record_merge_enqueue(&rt_arg->q, &rt_arg->current_record, data);
	thread_exit();
}

static inline void
create_zbookmark_from_obj_off(zbookmark_phys_t *zb, uint64_t object,
    uint64_t blkid)
{
	zb->zb_object = object;
	zb->zb_level = 0;
	zb->zb_blkid = blkid;
}

/*
 * This is a utility function that can do the comparison for the start or ends
 * of the ranges in a redact_record.
 */
static int
redact_range_compare(uint64_t obj1, uint64_t off1, uint32_t dbss1,
    uint64_t obj2, uint64_t off2, uint32_t dbss2)
{
	zbookmark_phys_t z1, z2;
	create_zbookmark_from_obj_off(&z1, obj1, off1);
	create_zbookmark_from_obj_off(&z2, obj2, off2);

	return (zbookmark_compare(dbss1 >> SPA_MINBLOCKSHIFT, 0,
	    dbss2 >> SPA_MINBLOCKSHIFT, 0, &z1, &z2));
}

/*
 * Compare two redaction records by their range's start location.  Also makes
 * eos records always compare last.  We use the thread number in the redact_node
 * to ensure that records do not compare equal (which is not allowed in our avl
 * trees).
 */
static int
redact_node_compare_start(const void *arg1, const void *arg2)
{
	const struct redact_node *rn1 = arg1;
	const struct redact_node *rn2 = arg2;
	const struct redact_record *rr1 = rn1->record;
	const struct redact_record *rr2 = rn2->record;
	if (rr1->eos_marker)
		return (1);
	if (rr2->eos_marker)
		return (-1);

	int cmp = redact_range_compare(rr1->start_object, rr1->start_blkid,
	    rr1->datablksz, rr2->start_object, rr2->start_blkid,
	    rr2->datablksz);
	if (cmp == 0)
		cmp = (rn1->thread_num < rn2->thread_num ? -1 : 1);
	return (cmp);
}

/*
 * Compare two redaction records by their range's end location.  Also makes
 * eos records always compare last.  We use the thread number in the redact_node
 * to ensure that records do not compare equal (which is not allowed in our avl
 * trees).
 */
static int
redact_node_compare_end(const void *arg1, const void *arg2)
{
	const struct redact_node *rn1 = arg1;
	const struct redact_node *rn2 = arg2;
	const struct redact_record *srr1 = rn1->record;
	const struct redact_record *srr2 = rn2->record;
	if (srr1->eos_marker)
		return (1);
	if (srr2->eos_marker)
		return (-1);

	int cmp = redact_range_compare(srr1->end_object, srr1->end_blkid,
	    srr1->datablksz, srr2->end_object, srr2->end_blkid,
	    srr2->datablksz);
	if (cmp == 0)
		cmp = (rn1->thread_num < rn2->thread_num ? -1 : 1);
	return (cmp);
}

/*
 * Utility function that compares two redaction records to determine if any part
 * of the "from" record is before any part of the "to" record. Also causes End
 * of Stream redaction records to compare after all others, so that the
 * redaction merging logic can stay simple.
 */
static boolean_t
redact_record_before(const struct redact_record *from,
    const struct redact_record *to)
{
	if (from->eos_marker == B_TRUE)
		return (B_FALSE);
	else if (to->eos_marker == B_TRUE)
		return (B_TRUE);
	return (redact_range_compare(from->start_object, from->start_blkid,
	    from->datablksz, to->end_object, to->end_blkid,
	    to->datablksz) <= 0);
}

/*
 * Pop a new redaction record off the queue, check that the records are in the
 * right order, and free the old data.
 */
static struct redact_record *
get_next_redact_record(bqueue_t *bq, struct redact_record *prev)
{
	struct redact_record *next = bqueue_dequeue(bq);
	ASSERT(redact_record_before(prev, next));
	kmem_free(prev, sizeof (*prev));
	return (next);
}

/*
 * Remove the given redaction node from both trees, pull a new redaction record
 * off the queue, free the old redaction record, update the redaction node, and
 * reinsert the node into the trees.
 */
static int
update_avl_trees(avl_tree_t *start_tree, avl_tree_t *end_tree,
    struct redact_node *redact_node)
{
	avl_remove(start_tree, redact_node);
	avl_remove(end_tree, redact_node);
	redact_node->record = get_next_redact_record(&redact_node->rt_arg->q,
	    redact_node->record);
	avl_add(end_tree, redact_node);
	avl_add(start_tree, redact_node);
	return (redact_node->rt_arg->error_code);
}

/*
 * Synctask for updating redaction lists.  We first take this txg's list of
 * redacted blocks and append those to the redaction list.  We then update the
 * redaction list's bonus buffer.  We store the furthest blocks we visited and
 * the list of snapshots that we're redacting with respect to.  We need these so
 * that redacted sends and receives can be correctly resumed.
 */
static void
redaction_list_update_sync(void *arg, dmu_tx_t *tx)
{
	struct merge_data *md = arg;
	uint64_t txg = dmu_tx_get_txg(tx);
	list_t *list = &md->md_blocks[txg & TXG_MASK];
	redact_block_phys_t *furthest_visited =
	    &md->md_furthest[txg & TXG_MASK];
	objset_t *mos = tx->tx_pool->dp_meta_objset;
	redaction_list_t *rl = md->md_redaction_list;
	int bufsize = redact_sync_bufsize;
	redact_block_phys_t *buf = kmem_alloc(bufsize * sizeof (*buf),
	    KM_SLEEP);
	int index = 0;

	dmu_buf_will_dirty(rl->rl_dbuf, tx);

	for (struct redact_block_list_node *rbln = list_remove_head(list);
	    rbln != NULL; rbln = list_remove_head(list)) {
		ASSERT3U(rbln->block.rbp_object, <=,
		    furthest_visited->rbp_object);
		ASSERT(rbln->block.rbp_object < furthest_visited->rbp_object ||
		    rbln->block.rbp_blkid <= furthest_visited->rbp_blkid);
		buf[index] = rbln->block;
		index++;
		if (index == bufsize) {
			dmu_write(mos, rl->rl_object,
			    rl->rl_phys->rlp_num_entries * sizeof (*buf),
			    bufsize * sizeof (*buf), buf, tx);
			rl->rl_phys->rlp_num_entries += bufsize;
			index = 0;
		}
		kmem_free(rbln, sizeof (*rbln));
	}
	if (index > 0) {
		dmu_write(mos, rl->rl_object, rl->rl_phys->rlp_num_entries *
		    sizeof (*buf), index * sizeof (*buf), buf, tx);
		rl->rl_phys->rlp_num_entries += index;
	}
	kmem_free(buf, bufsize * sizeof (*buf));

	md->md_synctask_txg[txg & TXG_MASK] = B_FALSE;
	rl->rl_phys->rlp_last_object = furthest_visited->rbp_object;
	rl->rl_phys->rlp_last_blkid = furthest_visited->rbp_blkid;
}

static void
commit_rl_updates(objset_t *os, struct merge_data *md, uint64_t object,
    uint64_t blkid)
{
	dmu_tx_t *tx = dmu_tx_create_dd(spa_get_dsl(os->os_spa)->dp_mos_dir);
	dmu_tx_hold_space(tx, sizeof (struct redact_block_list_node));
	VERIFY0(dmu_tx_assign(tx, TXG_WAIT));
	uint64_t txg = dmu_tx_get_txg(tx);
	if (!md->md_synctask_txg[txg & TXG_MASK]) {
		dsl_sync_task_nowait(dmu_tx_pool(tx),
		    redaction_list_update_sync, md, tx);
		md->md_synctask_txg[txg & TXG_MASK] = B_TRUE;
		md->md_latest_synctask_txg = txg;
	}
	md->md_furthest[txg & TXG_MASK].rbp_object = object;
	md->md_furthest[txg & TXG_MASK].rbp_blkid = blkid;
	list_move_tail(&md->md_blocks[txg & TXG_MASK],
	    &md->md_redact_block_pending);
	dmu_tx_commit(tx);
	md->md_last_time = gethrtime();
}

/*
 * We want to store the list of blocks that we're redacting in the bookmark's
 * redaction list.  However, this list is stored in the MOS, which means it can
 * only be written to in syncing context.  To get around this, we create a
 * synctask that will write to the mos for us.  We tell it what to write by
 * a linked list for each current transaction group; every time we decide to
 * redact a block, we append it to the transaction group that is currently in
 * open context.  We also update some progress information that the synctask
 * will store to enable resumable redacted sends.
 */
static void
update_redaction_list(struct merge_data *md, objset_t *os,
    uint64_t object, uint64_t blkid, uint64_t endblkid, uint32_t blksz)
{
	boolean_t enqueue = B_FALSE;
	redact_block_phys_t cur = {0};
	uint64_t count = endblkid - blkid + 1;
	while (count > REDACT_BLOCK_MAX_COUNT) {
		update_redaction_list(md, os, object, blkid,
		    blkid + REDACT_BLOCK_MAX_COUNT - 1, blksz);
		blkid += REDACT_BLOCK_MAX_COUNT;
		count -= REDACT_BLOCK_MAX_COUNT;
	}
	redact_block_phys_t *coalesce = &md->md_coalesce_block;
	boolean_t new;
	if (coalesce->rbp_size_count == 0) {
		new = B_TRUE;
		enqueue = B_FALSE;
	} else  {
		uint64_t old_count = redact_block_get_count(coalesce);
		if (coalesce->rbp_object == object &&
		    coalesce->rbp_blkid + old_count == blkid &&
		    old_count + count <= REDACT_BLOCK_MAX_COUNT) {
			ASSERT3U(redact_block_get_size(coalesce), ==, blksz);
			redact_block_set_count(coalesce, old_count + count);
			new = B_FALSE;
			enqueue = B_FALSE;
		} else {
			new = B_TRUE;
			enqueue = B_TRUE;
		}
	}

	if (new) {
		cur = *coalesce;
		coalesce->rbp_blkid = blkid;
		coalesce->rbp_object = object;

		redact_block_set_count(coalesce, count);
		redact_block_set_size(coalesce, blksz);
	}

	if (enqueue && redact_block_get_size(&cur) != 0) {
		struct redact_block_list_node *rbln =
		    kmem_alloc(sizeof (struct redact_block_list_node),
		    KM_SLEEP);
		rbln->block = cur;
		list_insert_tail(&md->md_redact_block_pending, rbln);
	}

	if (gethrtime() > md->md_last_time +
	    redaction_list_update_interval_ns) {
		commit_rl_updates(os, md, object, blkid);
	}
}

/*
 * This thread merges all the redaction records provided by the worker threads,
 * and determines which blocks are redacted by all the snapshots.  The algorithm
 * for doing so is similar to performing a merge in mergesort with n sub-lists
 * instead of 2, with some added complexity due to the fact that the entries are
 * ranges, not just single blocks.  This algorithm relies on the fact that the
 * queues are sorted, which is ensured by the fact that traverse_dataset
 * traverses the dataset in a consistent order.  We pull one entry off the front
 * of the queues of each secure dataset traversal thread.  Then we repeat the
 * following: each record represents a range of blocks modified by one of the
 * redaction snapshots, and each block in that range may need to be redacted in
 * the send stream.  Find the record with the latest start of its range, and the
 * record with the earliest end of its range. If the last start is before the
 * first end, then we know that the blocks in the range [last_start, first_end]
 * are covered by all of the ranges at the front of the queues, which means
 * every thread redacts that whole range.  For example, let's say the ranges on
 * each queue look like this:
 *
 * Block Id   1  2  3  4  5  6  7  8  9 10 11
 * Thread 1 |    [====================]
 * Thread 2 |       [========]
 * Thread 3 |             [=================]
 *
 * Thread 3 has the last start (5), and the thread 2 has the last end (6).  All
 * three threads modified the range [5,6], so that data should not be sent over
 * the wire.  After we've determined whether or not to redact anything, we take
 * the record with the first end.  We discard that record, and pull a new one
 * off the front of the queue it came from.  In the above example, we would
 * discard Thread 2's record, and pull a new one.  Let's say the next record we
 * pulled from Thread 2 covered range [10,11].  The new layout would look like
 * this:
 *
 * Block Id   1  2  3  4  5  6  7  8  9 10 11
 * Thread 1 |    [====================]
 * Thread 2 |                            [==]
 * Thread 3 |             [=================]
 *
 * When we compare the last start (10, from Thread 2) and the first end (9, from
 * Thread 1), we see that the last start is greater than the first end.
 * Therefore, we do not redact anything from these records.  We'll iterate by
 * replacing the record from Thread 1.
 *
 * We iterate by replacing the record with the lowest end because we know
 * that the record with the lowest end has helped us as much as it can.  All the
 * ranges before it that we will ever redact have been redacted.  In addition,
 * by replacing the one with the lowest end, we guarantee we catch all ranges
 * that need to be redacted.  For example, if in the case above we had replaced
 * the record from Thread 1 instead, we might have ended up with the following:
 *
 * Block Id   1  2  3  4  5  6  7  8  9 10 11 12
 * Thread 1 |                               [==]
 * Thread 2 |       [========]
 * Thread 3 |             [=================]
 *
 * If the next record from Thread 2 had been [8,10], for example, we should have
 * redacted part of that range, but because we updated Thread 1's record, we
 * missed it.
 *
 * We implement this algorithm by using two trees.  The first sorts the
 * redaction records by their start_zb, and the second sorts them by their
 * end_zb.  We use these to find the record with the last start and the record
 * with the first end.  We create a record with that start and end, and send it
 * on.  The overall runtime of this implementation is O(n log m), where n is the
 * total number of redaction records from all the different redaction snapshots,
 * and m is the number of redaction snapshots.
 *
 * If we redact with respect to zero snapshots, we create a redaction
 * record with the start object and blkid to 0, and the end object and blkid to
 * UINT64_MAX.  This will result in us redacting every block.
 */
static int
perform_thread_merge(bqueue_t *q, uint32_t num_threads,
    struct redact_thread_arg *thread_args, boolean_t *cancel)
{
	struct redact_node *redact_nodes = NULL;
	avl_tree_t start_tree, end_tree;
	struct redact_record *record;
	struct redact_record *current_record = NULL;
	int err = 0;
	struct merge_data md = { {0} };
	list_create(&md.md_redact_block_pending,
	    sizeof (struct redact_block_list_node),
	    offsetof(struct redact_block_list_node, node));

	/*
	 * If we're redacting with respect to zero snapshots, then no data is
	 * permitted to be sent.  We enqueue a record that redacts all blocks,
	 * and an eos marker.
	 */
	if (num_threads == 0) {
		record = kmem_zalloc(sizeof (struct redact_record),
		    KM_SLEEP);
		// We can't redact object 0, so don't try.
		record->start_object = 1;
		record->start_blkid = 0;
		record->end_object = record->end_blkid = UINT64_MAX;
		bqueue_enqueue(q, record, sizeof (*record));
		return (0);
	}
	if (num_threads > 0) {
		redact_nodes = kmem_zalloc(num_threads *
		    sizeof (*redact_nodes), KM_SLEEP);
	}

	avl_create(&start_tree, redact_node_compare_start,
	    sizeof (struct redact_node),
	    offsetof(struct redact_node, avl_node_start));
	avl_create(&end_tree, redact_node_compare_end,
	    sizeof (struct redact_node),
	    offsetof(struct redact_node, avl_node_end));

	for (int i = 0; i < num_threads; i++) {
		struct redact_node *node = &redact_nodes[i];
		struct redact_thread_arg *targ = &thread_args[i];
		node->record = bqueue_dequeue(&targ->q);
		node->rt_arg = targ;
		node->thread_num = i;
		avl_add(&start_tree, node);
		avl_add(&end_tree, node);
	}

	/*
	 * Once the first record in the end tree has returned EOS, every record
	 * must be an EOS record, so we should stop.
	 */
	while (err == 0 && !((struct redact_node *)avl_first(&end_tree))->
	    record->eos_marker) {
		if (*cancel) {
			err = EINTR;
			break;
		}
		struct redact_node *last_start = avl_last(&start_tree);
		struct redact_node *first_end = avl_first(&end_tree);

		/*
		 * If the last start record is before the first end record,
		 * then we have blocks that are redacted by all threads.
		 * Therefore, we should redact them.  Copy the record, and send
		 * it to the main thread.
		 */
		if (redact_record_before(last_start->record,
		    first_end->record)) {
			record = kmem_zalloc(sizeof (struct redact_record),
			    KM_SLEEP);
			*record = *first_end->record;
			record->start_object = last_start->record->start_object;
			record->start_blkid = last_start->record->start_blkid;
			record_merge_enqueue(q, &current_record,
			    record);
		}
		err = update_avl_trees(&start_tree, &end_tree, first_end);
	}

	/*
	 * We're done; if we were cancelled, we need to cancel our workers and
	 * clear out their queues.  Either way, we need to remove every thread's
	 * redact_node struct from the avl trees.
	 */
	for (int i = 0; i < num_threads; i++) {
		if (err != 0) {
			thread_args[i].cancel = B_TRUE;
			while (!redact_nodes[i].record->eos_marker) {
				(void) update_avl_trees(&start_tree, &end_tree,
				    &redact_nodes[i]);
			}
		}
		avl_remove(&start_tree, &redact_nodes[i]);
		avl_remove(&end_tree, &redact_nodes[i]);
		kmem_free(redact_nodes[i].record,
		    sizeof (struct redact_record));
		bqueue_destroy(&thread_args[i].q);
	}

	avl_destroy(&start_tree);
	avl_destroy(&end_tree);
	kmem_free(redact_nodes, num_threads * sizeof (*redact_nodes));
	if (current_record != NULL)
		bqueue_enqueue(q, current_record, sizeof (current_record));
	return (err);
}

struct redact_merge_thread_arg {
	bqueue_t q;
	spa_t *spa;
	int numsnaps;
	struct redact_thread_arg *thr_args;
	boolean_t cancel;
	int error_code;
};

static __attribute__((noreturn)) void
redact_merge_thread(void *arg)
{
	struct redact_merge_thread_arg *rmta = arg;
	rmta->error_code = perform_thread_merge(&rmta->q,
	    rmta->numsnaps, rmta->thr_args, &rmta->cancel);
	struct redact_record *rec = kmem_zalloc(sizeof (*rec), KM_SLEEP);
	rec->eos_marker = B_TRUE;
	bqueue_enqueue_flush(&rmta->q, rec, 1);
	thread_exit();
}

/*
 * Find the next object in or after the redaction range passed in, and hold
 * its dnode with the provided tag.  Also update *object to contain the new
 * object number.
 */
static int
hold_next_object(objset_t *os, struct redact_record *rec, const void *tag,
    uint64_t *object, dnode_t **dn)
{
	int err = 0;
	if (*dn != NULL)
		dnode_rele(*dn, tag);
	*dn = NULL;
	if (*object < rec->start_object) {
		*object = rec->start_object - 1;
	}
	err = dmu_object_next(os, object, B_FALSE, 0);
	if (err != 0)
		return (err);

	err = dnode_hold(os, *object, tag, dn);
	while (err == 0 && (*object < rec->start_object ||
	    DMU_OT_IS_METADATA((*dn)->dn_type))) {
		dnode_rele(*dn, tag);
		*dn = NULL;
		err = dmu_object_next(os, object, B_FALSE, 0);
		if (err != 0)
			break;
		err = dnode_hold(os, *object, tag, dn);
	}
	return (err);
}

static int
perform_redaction(objset_t *os, redaction_list_t *rl,
    struct redact_merge_thread_arg *rmta)
{
	int err = 0;
	bqueue_t *q = &rmta->q;
	struct redact_record *rec = NULL;
	struct merge_data md = { {0} };

	list_create(&md.md_redact_block_pending,
	    sizeof (struct redact_block_list_node),
	    offsetof(struct redact_block_list_node, node));
	md.md_redaction_list = rl;

	for (int i = 0; i < TXG_SIZE; i++) {
		list_create(&md.md_blocks[i],
		    sizeof (struct redact_block_list_node),
		    offsetof(struct redact_block_list_node, node));
	}
	dnode_t *dn = NULL;
	uint64_t prev_obj = 0;
	for (rec = bqueue_dequeue(q); !rec->eos_marker && err == 0;
	    rec = get_next_redact_record(q, rec)) {
		ASSERT3U(rec->start_object, !=, 0);
		uint64_t object;
		if (prev_obj != rec->start_object) {
			object = rec->start_object - 1;
			err = hold_next_object(os, rec, FTAG, &object, &dn);
		} else {
			object = prev_obj;
		}
		while (err == 0 && object <= rec->end_object) {
			if (issig(JUSTLOOKING) && issig(FORREAL)) {
				err = EINTR;
				break;
			}
			/*
			 * Part of the current object is contained somewhere in
			 * the range covered by rec.
			 */
			uint64_t startblkid;
			uint64_t endblkid;
			uint64_t maxblkid = dn->dn_phys->dn_maxblkid;

			if (rec->start_object < object)
				startblkid = 0;
			else if (rec->start_blkid > maxblkid)
				break;
			else
				startblkid = rec->start_blkid;

			if (rec->end_object > object || rec->end_blkid >
			    maxblkid) {
				endblkid = maxblkid;
			} else {
				endblkid = rec->end_blkid;
			}
			update_redaction_list(&md, os, object, startblkid,
			    endblkid, dn->dn_datablksz);

			if (object == rec->end_object)
				break;
			err = hold_next_object(os, rec, FTAG, &object, &dn);
		}
		if (err == ESRCH)
			err = 0;
		if (dn != NULL)
			prev_obj = object;
	}
	if (err == 0 && dn != NULL)
		dnode_rele(dn, FTAG);

	if (err == ESRCH)
		err = 0;
	rmta->cancel = B_TRUE;
	while (!rec->eos_marker)
		rec = get_next_redact_record(q, rec);
	kmem_free(rec, sizeof (*rec));

	/*
	 * There may be a block that's being coalesced, sync that out before we
	 * return.
	 */
	if (err == 0 && md.md_coalesce_block.rbp_size_count != 0) {
		struct redact_block_list_node *rbln =
		    kmem_alloc(sizeof (struct redact_block_list_node),
		    KM_SLEEP);
		rbln->block = md.md_coalesce_block;
		list_insert_tail(&md.md_redact_block_pending, rbln);
	}
	commit_rl_updates(os, &md, UINT64_MAX, UINT64_MAX);

	/*
	 * Wait for all the redaction info to sync out before we return, so that
	 * anyone who attempts to resume this redaction will have all the data
	 * they need.
	 */
	dsl_pool_t *dp = spa_get_dsl(os->os_spa);
	if (md.md_latest_synctask_txg != 0)
		txg_wait_synced(dp, md.md_latest_synctask_txg);
	for (int i = 0; i < TXG_SIZE; i++)
		list_destroy(&md.md_blocks[i]);
	return (err);
}

static boolean_t
redact_snaps_contains(uint64_t *snaps, uint64_t num_snaps, uint64_t guid)
{
	for (int i = 0; i < num_snaps; i++) {
		if (snaps[i] == guid)
			return (B_TRUE);
	}
	return (B_FALSE);
}

int
dmu_redact_snap(const char *snapname, nvlist_t *redactnvl,
    const char *redactbook)
{
	int err = 0;
	dsl_pool_t *dp = NULL;
	dsl_dataset_t *ds = NULL;
	int numsnaps = 0;
	objset_t *os;
	struct redact_thread_arg *args = NULL;
	redaction_list_t *new_rl = NULL;
	char *newredactbook;

	if ((err = dsl_pool_hold(snapname, FTAG, &dp)) != 0)
		return (err);

	newredactbook = kmem_zalloc(sizeof (char) * ZFS_MAX_DATASET_NAME_LEN,
	    KM_SLEEP);

	if ((err = dsl_dataset_hold_flags(dp, snapname, DS_HOLD_FLAG_DECRYPT,
	    FTAG, &ds)) != 0) {
		goto out;
	}
	dsl_dataset_long_hold(ds, FTAG);
	if (!ds->ds_is_snapshot || dmu_objset_from_ds(ds, &os) != 0) {
		err = EINVAL;
		goto out;
	}
	if (dsl_dataset_feature_is_active(ds, SPA_FEATURE_REDACTED_DATASETS)) {
		err = EALREADY;
		goto out;
	}

	numsnaps = fnvlist_num_pairs(redactnvl);
	if (numsnaps > 0)
		args = kmem_zalloc(numsnaps * sizeof (*args), KM_SLEEP);

	nvpair_t *pair = NULL;
	for (int i = 0; i < numsnaps; i++) {
		pair = nvlist_next_nvpair(redactnvl, pair);
		const char *name = nvpair_name(pair);
		struct redact_thread_arg *rta = &args[i];
		err = dsl_dataset_hold_flags(dp, name, DS_HOLD_FLAG_DECRYPT,
		    FTAG, &rta->ds);
		if (err != 0)
			break;
		/*
		 * We want to do the long hold before we can get any other
		 * errors, because the cleanup code will release the long
		 * hold if rta->ds is filled in.
		 */
		dsl_dataset_long_hold(rta->ds, FTAG);

		err = dmu_objset_from_ds(rta->ds, &rta->os);
		if (err != 0)
			break;
		if (!dsl_dataset_is_before(rta->ds, ds, 0)) {
			err = EINVAL;
			break;
		}
		if (dsl_dataset_feature_is_active(rta->ds,
		    SPA_FEATURE_REDACTED_DATASETS)) {
			err = EALREADY;
			break;

		}
	}
	if (err != 0)
		goto out;
	VERIFY3P(nvlist_next_nvpair(redactnvl, pair), ==, NULL);

	boolean_t resuming = B_FALSE;
	zfs_bookmark_phys_t bookmark;

	(void) strlcpy(newredactbook, snapname, ZFS_MAX_DATASET_NAME_LEN);
	char *c = strchr(newredactbook, '@');
	ASSERT3P(c, !=, NULL);
	int n = snprintf(c, ZFS_MAX_DATASET_NAME_LEN - (c - newredactbook),
	    "#%s", redactbook);
	if (n >= ZFS_MAX_DATASET_NAME_LEN - (c - newredactbook)) {
		dsl_pool_rele(dp, FTAG);
		kmem_free(newredactbook,
		    sizeof (char) * ZFS_MAX_DATASET_NAME_LEN);
		if (args != NULL)
			kmem_free(args, numsnaps * sizeof (*args));
		return (SET_ERROR(ENAMETOOLONG));
	}
	err = dsl_bookmark_lookup(dp, newredactbook, NULL, &bookmark);
	if (err == 0) {
		resuming = B_TRUE;
		if (bookmark.zbm_redaction_obj == 0) {
			err = EEXIST;
			goto out;
		}
		err = dsl_redaction_list_hold_obj(dp,
		    bookmark.zbm_redaction_obj, FTAG, &new_rl);
		if (err != 0) {
			err = EIO;
			goto out;
		}
		dsl_redaction_list_long_hold(dp, new_rl, FTAG);
		if (new_rl->rl_phys->rlp_num_snaps != numsnaps) {
			err = ESRCH;
			goto out;
		}
		for (int i = 0; i < numsnaps; i++) {
			struct redact_thread_arg *rta = &args[i];
			if (!redact_snaps_contains(new_rl->rl_phys->rlp_snaps,
			    new_rl->rl_phys->rlp_num_snaps,
			    dsl_dataset_phys(rta->ds)->ds_guid)) {
				err = ESRCH;
				goto out;
			}
		}
		if (new_rl->rl_phys->rlp_last_blkid == UINT64_MAX &&
		    new_rl->rl_phys->rlp_last_object == UINT64_MAX) {
			err = EEXIST;
			goto out;
		}
		dsl_pool_rele(dp, FTAG);
		dp = NULL;
	} else {
		uint64_t *guids = NULL;
		if (numsnaps > 0) {
			guids = kmem_zalloc(numsnaps * sizeof (uint64_t),
			    KM_SLEEP);
		}
		for (int i = 0; i < numsnaps; i++) {
			struct redact_thread_arg *rta = &args[i];
			guids[i] = dsl_dataset_phys(rta->ds)->ds_guid;
		}

		dsl_pool_rele(dp, FTAG);
		dp = NULL;
		err = dsl_bookmark_create_redacted(newredactbook, snapname,
		    numsnaps, guids, FTAG, &new_rl);
		kmem_free(guids, numsnaps * sizeof (uint64_t));
		if (err != 0) {
			goto out;
		}
	}

	for (int i = 0; i < numsnaps; i++) {
		struct redact_thread_arg *rta = &args[i];
		(void) bqueue_init(&rta->q, zfs_redact_queue_ff,
		    zfs_redact_queue_length,
		    offsetof(struct redact_record, ln));
		if (resuming) {
			rta->resume.zb_blkid =
			    new_rl->rl_phys->rlp_last_blkid;
			rta->resume.zb_object =
			    new_rl->rl_phys->rlp_last_object;
		}
		rta->txg = dsl_dataset_phys(ds)->ds_creation_txg;
		(void) thread_create(NULL, 0, redact_traverse_thread, rta,
		    0, curproc, TS_RUN, minclsyspri);
	}

	struct redact_merge_thread_arg *rmta;
	rmta = kmem_zalloc(sizeof (struct redact_merge_thread_arg), KM_SLEEP);

	(void) bqueue_init(&rmta->q, zfs_redact_queue_ff,
	    zfs_redact_queue_length, offsetof(struct redact_record, ln));
	rmta->numsnaps = numsnaps;
	rmta->spa = os->os_spa;
	rmta->thr_args = args;
	(void) thread_create(NULL, 0, redact_merge_thread, rmta, 0, curproc,
	    TS_RUN, minclsyspri);
	err = perform_redaction(os, new_rl, rmta);
	bqueue_destroy(&rmta->q);
	kmem_free(rmta, sizeof (struct redact_merge_thread_arg));

out:
	kmem_free(newredactbook, sizeof (char) * ZFS_MAX_DATASET_NAME_LEN);

	if (new_rl != NULL) {
		dsl_redaction_list_long_rele(new_rl, FTAG);
		dsl_redaction_list_rele(new_rl, FTAG);
	}
	for (int i = 0; i < numsnaps; i++) {
		struct redact_thread_arg *rta = &args[i];
		/*
		 * rta->ds may be NULL if we got an error while filling
		 * it in.
		 */
		if (rta->ds != NULL) {
			dsl_dataset_long_rele(rta->ds, FTAG);
			dsl_dataset_rele_flags(rta->ds,
			    DS_HOLD_FLAG_DECRYPT, FTAG);
		}
	}

	if (args != NULL)
		kmem_free(args, numsnaps * sizeof (*args));
	if (dp != NULL)
		dsl_pool_rele(dp, FTAG);
	if (ds != NULL) {
		dsl_dataset_long_rele(ds, FTAG);
		dsl_dataset_rele_flags(ds, DS_HOLD_FLAG_DECRYPT, FTAG);
	}
	return (SET_ERROR(err));

}
