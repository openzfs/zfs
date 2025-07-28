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
 * Copyright (c) 2014 Integros [integros.com]
 * Copyright (c) 2018 Datto Inc.
 */

/* Portions Copyright 2010 Robert Milkowski */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/dmu.h>
#include <sys/zap.h>
#include <sys/arc.h>
#include <sys/stat.h>
#include <sys/zil.h>
#include <sys/zil_impl.h>
#include <sys/dsl_dataset.h>
#include <sys/vdev_impl.h>
#include <sys/dmu_tx.h>
#include <sys/dsl_pool.h>
#include <sys/metaslab.h>
#include <sys/trace_zfs.h>
#include <sys/abd.h>

/*
 * The ZFS Intent Log (ZIL) saves "transaction records" (itxs) of system
 * calls that change the file system. Each itx has enough information to
 * be able to replay them after a system crash, power loss, or
 * equivalent failure mode. These are stored in memory until either:
 *
 *   1. they are committed to the pool by the DMU transaction group
 *      (txg), at which point they can be discarded; or
 *   2. they are committed to the on-disk ZIL for the dataset being
 *      modified (e.g. due to an fsync, O_DSYNC, or other synchronous
 *      requirement).
 *
 * In the event of a crash or power loss, the itxs contained by each
 * dataset's on-disk ZIL will be replayed when that dataset is first
 * instantiated (e.g. if the dataset is a normal filesystem, when it is
 * first mounted).
 *
 * As hinted at above, there is one ZIL per dataset (both the in-memory
 * representation, and the on-disk representation). The on-disk format
 * consists of 3 parts:
 *
 * 	- a single, per-dataset, ZIL header; which points to a chain of
 * 	- zero or more ZIL blocks; each of which contains
 * 	- zero or more ZIL records
 *
 * A ZIL record holds the information necessary to replay a single
 * system call transaction. A ZIL block can hold many ZIL records, and
 * the blocks are chained together, similarly to a singly linked list.
 *
 * Each ZIL block contains a block pointer (blkptr_t) to the next ZIL
 * block in the chain, and the ZIL header points to the first block in
 * the chain.
 *
 * Note, there is not a fixed place in the pool to hold these ZIL
 * blocks; they are dynamically allocated and freed as needed from the
 * blocks available on the pool, though they can be preferentially
 * allocated from a dedicated "log" vdev.
 */

/*
 * This controls the amount of time that a ZIL block (lwb) will remain
 * "open" when it isn't "full", and it has a thread waiting for it to be
 * committed to stable storage. Please refer to the zil_commit_waiter()
 * function (and the comments within it) for more details.
 */
int zfs_commit_timeout_pct = 5;

/*
 * See zil.h for more information about these fields.
 */
zil_stats_t zil_stats = {
	{ "zil_commit_count",			KSTAT_DATA_UINT64 },
	{ "zil_commit_writer_count",		KSTAT_DATA_UINT64 },
	{ "zil_itx_count",			KSTAT_DATA_UINT64 },
	{ "zil_itx_indirect_count",		KSTAT_DATA_UINT64 },
	{ "zil_itx_indirect_bytes",		KSTAT_DATA_UINT64 },
	{ "zil_itx_copied_count",		KSTAT_DATA_UINT64 },
	{ "zil_itx_copied_bytes",		KSTAT_DATA_UINT64 },
	{ "zil_itx_needcopy_count",		KSTAT_DATA_UINT64 },
	{ "zil_itx_needcopy_bytes",		KSTAT_DATA_UINT64 },
	{ "zil_itx_metaslab_normal_count",	KSTAT_DATA_UINT64 },
	{ "zil_itx_metaslab_normal_bytes",	KSTAT_DATA_UINT64 },
	{ "zil_itx_metaslab_slog_count",	KSTAT_DATA_UINT64 },
	{ "zil_itx_metaslab_slog_bytes",	KSTAT_DATA_UINT64 },
};

static kstat_t *zil_ksp;

/*
 * Disable intent logging replay.  This global ZIL switch affects all pools.
 */
int zil_replay_disable = 0;

/*
 * Disable the DKIOCFLUSHWRITECACHE commands that are normally sent to
 * the disk(s) by the ZIL after an LWB write has completed. Setting this
 * will cause ZIL corruption on power loss if a volatile out-of-order
 * write cache is enabled.
 */
int zil_nocacheflush = 0;

/*
 * Limit SLOG write size per commit executed with synchronous priority.
 * Any writes above that will be executed with lower (asynchronous) priority
 * to limit potential SLOG device abuse by single active ZIL writer.
 */
unsigned long zil_slog_bulk = 768 * 1024;

static kmem_cache_t *zil_lwb_cache;
static kmem_cache_t *zil_zcw_cache;

#define	LWB_EMPTY(lwb) ((BP_GET_LSIZE(&lwb->lwb_blk) - \
    sizeof (zil_chain_t)) == (lwb->lwb_sz - lwb->lwb_nused))

static int
zil_bp_compare(const void *x1, const void *x2)
{
	const dva_t *dva1 = &((zil_bp_node_t *)x1)->zn_dva;
	const dva_t *dva2 = &((zil_bp_node_t *)x2)->zn_dva;

	int cmp = TREE_CMP(DVA_GET_VDEV(dva1), DVA_GET_VDEV(dva2));
	if (likely(cmp))
		return (cmp);

	return (TREE_CMP(DVA_GET_OFFSET(dva1), DVA_GET_OFFSET(dva2)));
}

static void
zil_bp_tree_init(zilog_t *zilog)
{
	avl_create(&zilog->zl_bp_tree, zil_bp_compare,
	    sizeof (zil_bp_node_t), offsetof(zil_bp_node_t, zn_node));
}

static void
zil_bp_tree_fini(zilog_t *zilog)
{
	avl_tree_t *t = &zilog->zl_bp_tree;
	zil_bp_node_t *zn;
	void *cookie = NULL;

	while ((zn = avl_destroy_nodes(t, &cookie)) != NULL)
		kmem_free(zn, sizeof (zil_bp_node_t));

	avl_destroy(t);
}

int
zil_bp_tree_add(zilog_t *zilog, const blkptr_t *bp)
{
	avl_tree_t *t = &zilog->zl_bp_tree;
	const dva_t *dva;
	zil_bp_node_t *zn;
	avl_index_t where;

	if (BP_IS_EMBEDDED(bp))
		return (0);

	dva = BP_IDENTITY(bp);

	if (avl_find(t, dva, &where) != NULL)
		return (SET_ERROR(EEXIST));

	zn = kmem_alloc(sizeof (zil_bp_node_t), KM_SLEEP);
	zn->zn_dva = *dva;
	avl_insert(t, zn, where);

	return (0);
}

static zil_header_t *
zil_header_in_syncing_context(zilog_t *zilog)
{
	return ((zil_header_t *)zilog->zl_header);
}

static void
zil_init_log_chain(zilog_t *zilog, blkptr_t *bp)
{
	zio_cksum_t *zc = &bp->blk_cksum;

	(void) random_get_pseudo_bytes((void *)&zc->zc_word[ZIL_ZC_GUID_0],
	    sizeof (zc->zc_word[ZIL_ZC_GUID_0]));
	(void) random_get_pseudo_bytes((void *)&zc->zc_word[ZIL_ZC_GUID_1],
	    sizeof (zc->zc_word[ZIL_ZC_GUID_1]));
	zc->zc_word[ZIL_ZC_OBJSET] = dmu_objset_id(zilog->zl_os);
	zc->zc_word[ZIL_ZC_SEQ] = 1ULL;
}

/*
 * Read a log block and make sure it's valid.
 */
static int
zil_read_log_block(zilog_t *zilog, boolean_t decrypt, const blkptr_t *bp,
    blkptr_t *nbp, void *dst, char **end)
{
	zio_flag_t zio_flags = ZIO_FLAG_CANFAIL;
	arc_flags_t aflags = ARC_FLAG_WAIT;
	arc_buf_t *abuf = NULL;
	zbookmark_phys_t zb;
	int error;

	if (zilog->zl_header->zh_claim_txg == 0)
		zio_flags |= ZIO_FLAG_SPECULATIVE | ZIO_FLAG_SCRUB;

	if (!(zilog->zl_header->zh_flags & ZIL_CLAIM_LR_SEQ_VALID))
		zio_flags |= ZIO_FLAG_SPECULATIVE;

	if (!decrypt)
		zio_flags |= ZIO_FLAG_RAW;

	SET_BOOKMARK(&zb, bp->blk_cksum.zc_word[ZIL_ZC_OBJSET],
	    ZB_ZIL_OBJECT, ZB_ZIL_LEVEL, bp->blk_cksum.zc_word[ZIL_ZC_SEQ]);

	error = arc_read(NULL, zilog->zl_spa, bp, arc_getbuf_func,
	    &abuf, ZIO_PRIORITY_SYNC_READ, zio_flags, &aflags, &zb);

	if (error == 0) {
		zio_cksum_t cksum = bp->blk_cksum;

		/*
		 * Validate the checksummed log block.
		 *
		 * Sequence numbers should be... sequential.  The checksum
		 * verifier for the next block should be bp's checksum plus 1.
		 *
		 * Also check the log chain linkage and size used.
		 */
		cksum.zc_word[ZIL_ZC_SEQ]++;

		if (BP_GET_CHECKSUM(bp) == ZIO_CHECKSUM_ZILOG2) {
			zil_chain_t *zilc = abuf->b_data;
			char *lr = (char *)(zilc + 1);
			uint64_t len = zilc->zc_nused - sizeof (zil_chain_t);

			if (bcmp(&cksum, &zilc->zc_next_blk.blk_cksum,
			    sizeof (cksum)) || BP_IS_HOLE(&zilc->zc_next_blk)) {
				error = SET_ERROR(ECKSUM);
			} else {
				ASSERT3U(len, <=, SPA_OLD_MAXBLOCKSIZE);
				bcopy(lr, dst, len);
				*end = (char *)dst + len;
				*nbp = zilc->zc_next_blk;
			}
		} else {
			char *lr = abuf->b_data;
			uint64_t size = BP_GET_LSIZE(bp);
			zil_chain_t *zilc = (zil_chain_t *)(lr + size) - 1;

			if (bcmp(&cksum, &zilc->zc_next_blk.blk_cksum,
			    sizeof (cksum)) || BP_IS_HOLE(&zilc->zc_next_blk) ||
			    (zilc->zc_nused > (size - sizeof (*zilc)))) {
				error = SET_ERROR(ECKSUM);
			} else {
				ASSERT3U(zilc->zc_nused, <=,
				    SPA_OLD_MAXBLOCKSIZE);
				bcopy(lr, dst, zilc->zc_nused);
				*end = (char *)dst + zilc->zc_nused;
				*nbp = zilc->zc_next_blk;
			}
		}

		arc_buf_destroy(abuf, &abuf);
	}

	return (error);
}

/*
 * Read a TX_WRITE log data block.
 */
static int
zil_read_log_data(zilog_t *zilog, const lr_write_t *lr, void *wbuf)
{
	zio_flag_t zio_flags = ZIO_FLAG_CANFAIL;
	const blkptr_t *bp = &lr->lr_blkptr;
	arc_flags_t aflags = ARC_FLAG_WAIT;
	arc_buf_t *abuf = NULL;
	zbookmark_phys_t zb;
	int error;

	if (BP_IS_HOLE(bp)) {
		if (wbuf != NULL)
			bzero(wbuf, MAX(BP_GET_LSIZE(bp), lr->lr_length));
		return (0);
	}

	if (zilog->zl_header->zh_claim_txg == 0)
		zio_flags |= ZIO_FLAG_SPECULATIVE | ZIO_FLAG_SCRUB;

	/*
	 * If we are not using the resulting data, we are just checking that
	 * it hasn't been corrupted so we don't need to waste CPU time
	 * decompressing and decrypting it.
	 */
	if (wbuf == NULL)
		zio_flags |= ZIO_FLAG_RAW;

	SET_BOOKMARK(&zb, dmu_objset_id(zilog->zl_os), lr->lr_foid,
	    ZB_ZIL_LEVEL, lr->lr_offset / BP_GET_LSIZE(bp));

	error = arc_read(NULL, zilog->zl_spa, bp, arc_getbuf_func, &abuf,
	    ZIO_PRIORITY_SYNC_READ, zio_flags, &aflags, &zb);

	if (error == 0) {
		if (wbuf != NULL)
			bcopy(abuf->b_data, wbuf, arc_buf_size(abuf));
		arc_buf_destroy(abuf, &abuf);
	}

	return (error);
}

/*
 * Parse the intent log, and call parse_func for each valid record within.
 */
int
zil_parse(zilog_t *zilog, zil_parse_blk_func_t *parse_blk_func,
    zil_parse_lr_func_t *parse_lr_func, void *arg, uint64_t txg,
    boolean_t decrypt)
{
	const zil_header_t *zh = zilog->zl_header;
	boolean_t claimed = !!zh->zh_claim_txg;
	uint64_t claim_blk_seq = claimed ? zh->zh_claim_blk_seq : UINT64_MAX;
	uint64_t claim_lr_seq = claimed ? zh->zh_claim_lr_seq : UINT64_MAX;
	uint64_t max_blk_seq = 0;
	uint64_t max_lr_seq = 0;
	uint64_t blk_count = 0;
	uint64_t lr_count = 0;
	blkptr_t blk, next_blk;
	char *lrbuf, *lrp;
	int error = 0;

	bzero(&next_blk, sizeof (blkptr_t));

	/*
	 * Old logs didn't record the maximum zh_claim_lr_seq.
	 */
	if (!(zh->zh_flags & ZIL_CLAIM_LR_SEQ_VALID))
		claim_lr_seq = UINT64_MAX;

	/*
	 * Starting at the block pointed to by zh_log we read the log chain.
	 * For each block in the chain we strongly check that block to
	 * ensure its validity.  We stop when an invalid block is found.
	 * For each block pointer in the chain we call parse_blk_func().
	 * For each record in each valid block we call parse_lr_func().
	 * If the log has been claimed, stop if we encounter a sequence
	 * number greater than the highest claimed sequence number.
	 */
	lrbuf = zio_buf_alloc(SPA_OLD_MAXBLOCKSIZE);
	zil_bp_tree_init(zilog);

	for (blk = zh->zh_log; !BP_IS_HOLE(&blk); blk = next_blk) {
		uint64_t blk_seq = blk.blk_cksum.zc_word[ZIL_ZC_SEQ];
		int reclen;
		char *end = NULL;

		if (blk_seq > claim_blk_seq)
			break;

		error = parse_blk_func(zilog, &blk, arg, txg);
		if (error != 0)
			break;
		ASSERT3U(max_blk_seq, <, blk_seq);
		max_blk_seq = blk_seq;
		blk_count++;

		if (max_lr_seq == claim_lr_seq && max_blk_seq == claim_blk_seq)
			break;

		error = zil_read_log_block(zilog, decrypt, &blk, &next_blk,
		    lrbuf, &end);
		if (error != 0)
			break;

		for (lrp = lrbuf; lrp < end; lrp += reclen) {
			lr_t *lr = (lr_t *)lrp;
			reclen = lr->lrc_reclen;
			ASSERT3U(reclen, >=, sizeof (lr_t));
			if (lr->lrc_seq > claim_lr_seq)
				goto done;

			error = parse_lr_func(zilog, lr, arg, txg);
			if (error != 0)
				goto done;
			ASSERT3U(max_lr_seq, <, lr->lrc_seq);
			max_lr_seq = lr->lrc_seq;
			lr_count++;
		}
	}
done:
	zilog->zl_parse_error = error;
	zilog->zl_parse_blk_seq = max_blk_seq;
	zilog->zl_parse_lr_seq = max_lr_seq;
	zilog->zl_parse_blk_count = blk_count;
	zilog->zl_parse_lr_count = lr_count;

	ASSERT(!claimed || !(zh->zh_flags & ZIL_CLAIM_LR_SEQ_VALID) ||
	    (max_blk_seq == claim_blk_seq && max_lr_seq == claim_lr_seq) ||
	    (decrypt && error == EIO));

	zil_bp_tree_fini(zilog);
	zio_buf_free(lrbuf, SPA_OLD_MAXBLOCKSIZE);

	return (error);
}

static int
zil_clear_log_block(zilog_t *zilog, const blkptr_t *bp, void *tx,
    uint64_t first_txg)
{
	(void) tx;
	ASSERT(!BP_IS_HOLE(bp));

	/*
	 * As we call this function from the context of a rewind to a
	 * checkpoint, each ZIL block whose txg is later than the txg
	 * that we rewind to is invalid. Thus, we return -1 so
	 * zil_parse() doesn't attempt to read it.
	 */
	if (bp->blk_birth >= first_txg)
		return (-1);

	if (zil_bp_tree_add(zilog, bp) != 0)
		return (0);

	zio_free(zilog->zl_spa, first_txg, bp);
	return (0);
}

static int
zil_noop_log_record(zilog_t *zilog, const lr_t *lrc, void *tx,
    uint64_t first_txg)
{
	(void) zilog, (void) lrc, (void) tx, (void) first_txg;
	return (0);
}

static int
zil_claim_log_block(zilog_t *zilog, const blkptr_t *bp, void *tx,
    uint64_t first_txg)
{
	/*
	 * Claim log block if not already committed and not already claimed.
	 * If tx == NULL, just verify that the block is claimable.
	 */
	if (BP_IS_HOLE(bp) || bp->blk_birth < first_txg ||
	    zil_bp_tree_add(zilog, bp) != 0)
		return (0);

	return (zio_wait(zio_claim(NULL, zilog->zl_spa,
	    tx == NULL ? 0 : first_txg, bp, spa_claim_notify, NULL,
	    ZIO_FLAG_CANFAIL | ZIO_FLAG_SPECULATIVE | ZIO_FLAG_SCRUB)));
}

static int
zil_claim_log_record(zilog_t *zilog, const lr_t *lrc, void *tx,
    uint64_t first_txg)
{
	lr_write_t *lr = (lr_write_t *)lrc;
	int error;

	if (lrc->lrc_txtype != TX_WRITE)
		return (0);

	/*
	 * If the block is not readable, don't claim it.  This can happen
	 * in normal operation when a log block is written to disk before
	 * some of the dmu_sync() blocks it points to.  In this case, the
	 * transaction cannot have been committed to anyone (we would have
	 * waited for all writes to be stable first), so it is semantically
	 * correct to declare this the end of the log.
	 */
	if (lr->lr_blkptr.blk_birth >= first_txg) {
		error = zil_read_log_data(zilog, lr, NULL);
		if (error != 0)
			return (error);
	}

	return (zil_claim_log_block(zilog, &lr->lr_blkptr, tx, first_txg));
}

static int
zil_free_log_block(zilog_t *zilog, const blkptr_t *bp, void *tx,
    uint64_t claim_txg)
{
	(void) claim_txg;

	zio_free(zilog->zl_spa, dmu_tx_get_txg(tx), bp);

	return (0);
}

static int
zil_free_log_record(zilog_t *zilog, const lr_t *lrc, void *tx,
    uint64_t claim_txg)
{
	lr_write_t *lr = (lr_write_t *)lrc;
	blkptr_t *bp = &lr->lr_blkptr;

	/*
	 * If we previously claimed it, we need to free it.
	 */
	if (claim_txg != 0 && lrc->lrc_txtype == TX_WRITE &&
	    bp->blk_birth >= claim_txg && zil_bp_tree_add(zilog, bp) == 0 &&
	    !BP_IS_HOLE(bp))
		zio_free(zilog->zl_spa, dmu_tx_get_txg(tx), bp);

	return (0);
}

static lwb_t *
zil_alloc_lwb(zilog_t *zilog, blkptr_t *bp, boolean_t slog, uint64_t txg,
    boolean_t fastwrite)
{
	lwb_t *lwb;

	lwb = kmem_cache_alloc(zil_lwb_cache, KM_SLEEP);
	lwb->lwb_zilog = zilog;
	lwb->lwb_blk = *bp;
	lwb->lwb_fastwrite = fastwrite;
	lwb->lwb_slog = slog;
	lwb->lwb_state = LWB_STATE_CLOSED;
	lwb->lwb_buf = zio_buf_alloc(BP_GET_LSIZE(bp));
	lwb->lwb_max_txg = txg;
	lwb->lwb_write_zio = NULL;
	lwb->lwb_root_zio = NULL;
	lwb->lwb_tx = NULL;
	lwb->lwb_issued_timestamp = 0;
	if (BP_GET_CHECKSUM(bp) == ZIO_CHECKSUM_ZILOG2) {
		lwb->lwb_nused = sizeof (zil_chain_t);
		lwb->lwb_sz = BP_GET_LSIZE(bp);
	} else {
		lwb->lwb_nused = 0;
		lwb->lwb_sz = BP_GET_LSIZE(bp) - sizeof (zil_chain_t);
	}

	mutex_enter(&zilog->zl_lock);
	list_insert_tail(&zilog->zl_lwb_list, lwb);
	mutex_exit(&zilog->zl_lock);

	ASSERT(avl_is_empty(&lwb->lwb_vdev_tree));
	VERIFY(list_is_empty(&lwb->lwb_waiters));
	VERIFY(list_is_empty(&lwb->lwb_itxs));

	return (lwb);
}

static boolean_t zil_failed(zilog_t *zilog);

static void
zil_free_lwb(zilog_t *zilog, lwb_t *lwb)
{
	ASSERT(MUTEX_HELD(&zilog->zl_lock));
	VERIFY(list_is_empty(&lwb->lwb_waiters));
	VERIFY(list_is_empty(&lwb->lwb_itxs));
	ASSERT(avl_is_empty(&lwb->lwb_vdev_tree));
	ASSERT3P(lwb->lwb_write_zio, ==, NULL);
	ASSERT3P(lwb->lwb_root_zio, ==, NULL);

	/*
	 * If we can't write, then we could be in zil_close() destroying
	 * unissued lwbs; in that case, don't assert that they're completed.
	 */
	if (!(zil_failed(zilog) || spa_exiting_any(zilog->zl_spa))) {
		ASSERT3U(lwb->lwb_max_txg, <=, spa_syncing_txg(zilog->zl_spa));
		ASSERT(lwb->lwb_state == LWB_STATE_CLOSED ||
		    lwb->lwb_state == LWB_STATE_FLUSH_DONE);
	}

	/*
	 * Clear the zilog's field to indicate this lwb is no longer
	 * valid, and prevent use-after-free errors.
	 */
	if (zilog->zl_last_lwb_opened == lwb)
		zilog->zl_last_lwb_opened = NULL;

	kmem_cache_free(zil_lwb_cache, lwb);
}

/*
 * Called when we create in-memory log transactions so that we know
 * to cleanup the itxs at the end of spa_sync().
 */
static void
zilog_dirty(zilog_t *zilog, uint64_t txg)
{
	dsl_pool_t *dp = zilog->zl_dmu_pool;
	dsl_dataset_t *ds = dmu_objset_ds(zilog->zl_os);

	ASSERT(spa_writeable(zilog->zl_spa));

	if (ds->ds_is_snapshot)
		panic("dirtying snapshot!");

	if (txg_list_add(&dp->dp_dirty_zilogs, zilog, txg)) {
		/* up the hold count until we can be written out */
		dmu_buf_add_ref(ds->ds_dbuf, zilog);

		zilog->zl_dirty_max_txg = MAX(txg, zilog->zl_dirty_max_txg);
	}
}

/*
 * Determine if the zil is dirty in the specified txg. Callers wanting to
 * ensure that the dirty state does not change must hold the itxg_lock for
 * the specified txg. Holding the lock will ensure that the zil cannot be
 * dirtied (zil_itx_assign) or cleaned (zil_clean) while we check its current
 * state.
 */
static boolean_t __maybe_unused
zilog_is_dirty_in_txg(zilog_t *zilog, uint64_t txg)
{
	dsl_pool_t *dp = zilog->zl_dmu_pool;

	if (txg_list_member(&dp->dp_dirty_zilogs, zilog, txg & TXG_MASK))
		return (B_TRUE);
	return (B_FALSE);
}

/*
 * Determine if the zil is dirty. The zil is considered dirty if it has
 * any pending itx records that have not been cleaned by zil_clean().
 */
static boolean_t
zilog_is_dirty(zilog_t *zilog)
{
	dsl_pool_t *dp = zilog->zl_dmu_pool;

	for (int t = 0; t < TXG_SIZE; t++) {
		if (txg_list_member(&dp->dp_dirty_zilogs, zilog, t))
			return (B_TRUE);
	}
	return (B_FALSE);
}

/*
 * Create an on-disk intent log.
 */
static lwb_t *
zil_create(zilog_t *zilog)
{
	const zil_header_t *zh = zilog->zl_header;
	lwb_t *lwb = NULL;
	uint64_t txg = 0;
	dmu_tx_t *tx = NULL;
	blkptr_t blk;
	int error = 0;
	boolean_t fastwrite = FALSE;
	boolean_t slog = FALSE;

	/*
	 * Wait for any previous destroy to complete.
	 */
	error = txg_wait_synced_flags(zilog->zl_dmu_pool, zilog->zl_destroy_txg,
	    TXG_WAIT_F_NOSUSPEND);
	if (error != 0) {
		ASSERT3S(error, ==, EAGAIN);
		return (NULL);
	}

	ASSERT(zh->zh_claim_txg == 0);
	ASSERT(zh->zh_replay_seq == 0);

	blk = zh->zh_log;

	/*
	 * Allocate an initial log block if:
	 *    - there isn't one already
	 *    - the existing block is the wrong endianness
	 */
	if (BP_IS_HOLE(&blk) || BP_SHOULD_BYTESWAP(&blk)) {
		tx = dmu_tx_create(zilog->zl_os);
		error = dmu_tx_assign(tx, DMU_TX_ASSIGN_WAIT |
		    DMU_TX_ASSIGN_NOSUSPEND);
		if (error != 0) {
			ASSERT3S(error, ==, EAGAIN);
			dmu_tx_abort(tx);
			return (NULL);
		}
		dsl_dataset_dirty(dmu_objset_ds(zilog->zl_os), tx);
		txg = dmu_tx_get_txg(tx);

		if (!BP_IS_HOLE(&blk)) {
			zio_free(zilog->zl_spa, txg, &blk);
			BP_ZERO(&blk);
		}

		error = zio_alloc_zil(zilog->zl_spa, zilog->zl_os, txg, &blk,
		    ZIL_MIN_BLKSZ, &slog);
		fastwrite = TRUE;

		if (error == 0)
			zil_init_log_chain(zilog, &blk);
	}

	/*
	 * Allocate a log write block (lwb) for the first log block.
	 */
	if (error == 0)
		lwb = zil_alloc_lwb(zilog, &blk, slog, txg, fastwrite);

	/*
	 * If we just allocated the first log block, commit our transaction
	 * and wait for zil_sync() to stuff the block pointer into zh_log.
	 * (zh is part of the MOS, so we cannot modify it in open context.)
	 */
	if (tx != NULL) {
		dmu_tx_commit(tx);
		error = txg_wait_synced_flags(zilog->zl_dmu_pool, txg,
		    TXG_WAIT_F_NOSUSPEND);
		if (error != 0) {
			/*
			 * Pool suspended at the last moment, so we have to
			 * unwind everything we've just done.
			 */
			ASSERT3S(error, ==, EAGAIN);

			metaslab_fastwrite_unmark(zilog->zl_spa, &lwb->lwb_blk);

			mutex_enter(&zilog->zl_lock);
			list_remove(&zilog->zl_lwb_list, lwb);
			mutex_exit(&zilog->zl_lock);

			zio_buf_free(lwb->lwb_buf, lwb->lwb_sz);
			zil_free_lwb(zilog, lwb);

			return (NULL);
		}
	}

	ASSERT(error != 0 || bcmp(&blk, &zh->zh_log, sizeof (blk)) == 0);
	IMPLY(error == 0, lwb != NULL);

	return (lwb);
}

/*
 * In one tx, free all log blocks and clear the log header. If keep_first
 * is set, then we're replaying a log with no content. We want to keep the
 * first block, however, so that the first synchronous transaction doesn't
 * require a txg_wait_synced() in zil_create(). We don't need to
 * txg_wait_synced() here either when keep_first is set, because both
 * zil_create() and zil_destroy() will wait for any in-progress destroys
 * to complete.
 */
void
zil_destroy(zilog_t *zilog, boolean_t keep_first)
{
	const zil_header_t *zh = zilog->zl_header;
	lwb_t *lwb;
	dmu_tx_t *tx;
	uint64_t txg;
	int error;

	/*
	 * Wait for any previous destroy to complete. Its ok to block here,
	 * as this is always arrived at via administrative operations, never
	 * appication IO. Its also not obvious how we'd recover.
	 */
	txg_wait_synced(zilog->zl_dmu_pool, zilog->zl_destroy_txg);

	zilog->zl_old_header = *zh;		/* debugging aid */

	if (BP_IS_HOLE(&zh->zh_log))
		return;

	tx = dmu_tx_create(zilog->zl_os);
	error = dmu_tx_assign(tx, DMU_TX_ASSIGN_WAIT);
	if (error != 0) {
		ASSERT(dmu_objset_exiting(zilog->zl_os));
		dmu_tx_abort(tx);
		return;
	}
	dsl_dataset_dirty(dmu_objset_ds(zilog->zl_os), tx);
	txg = dmu_tx_get_txg(tx);

	mutex_enter(&zilog->zl_lock);

	ASSERT3U(zilog->zl_destroy_txg, <, txg);
	zilog->zl_destroy_txg = txg;
	zilog->zl_keep_first = keep_first;

	if (!list_is_empty(&zilog->zl_lwb_list)) {
		ASSERT(zh->zh_claim_txg == 0);
		VERIFY(!keep_first);
		while ((lwb = list_head(&zilog->zl_lwb_list)) != NULL) {
			if (lwb->lwb_fastwrite)
				metaslab_fastwrite_unmark(zilog->zl_spa,
				    &lwb->lwb_blk);

			list_remove(&zilog->zl_lwb_list, lwb);
			if (lwb->lwb_buf != NULL)
				zio_buf_free(lwb->lwb_buf, lwb->lwb_sz);
			zio_free(zilog->zl_spa, txg, &lwb->lwb_blk);
			zil_free_lwb(zilog, lwb);
		}
	} else if (!keep_first) {
		zil_destroy_sync(zilog, tx);
	}
	mutex_exit(&zilog->zl_lock);

	dmu_tx_commit(tx);
}

void
zil_destroy_sync(zilog_t *zilog, dmu_tx_t *tx)
{
	ASSERT(list_is_empty(&zilog->zl_lwb_list));
	(void) zil_parse(zilog, zil_free_log_block,
	    zil_free_log_record, tx, zilog->zl_header->zh_claim_txg, B_FALSE);
}

int
zil_claim(dsl_pool_t *dp, dsl_dataset_t *ds, void *txarg)
{
	dmu_tx_t *tx = txarg;
	zilog_t *zilog;
	uint64_t first_txg;
	zil_header_t *zh;
	objset_t *os;
	int error;

	error = dmu_objset_own_obj(dp, ds->ds_object,
	    DMU_OST_ANY, B_FALSE, B_FALSE, FTAG, &os);
	if (error != 0) {
		/*
		 * EBUSY indicates that the objset is inconsistent, in which
		 * case it can not have a ZIL.
		 */
		if (error != EBUSY) {
			cmn_err(CE_WARN, "can't open objset for %llu, error %u",
			    (unsigned long long)ds->ds_object, error);
		}

		return (0);
	}

	zilog = dmu_objset_zil(os);
	zh = zil_header_in_syncing_context(zilog);
	ASSERT3U(tx->tx_txg, ==, spa_first_txg(zilog->zl_spa));
	first_txg = spa_min_claim_txg(zilog->zl_spa);

	/*
	 * If the spa_log_state is not set to be cleared, check whether
	 * the current uberblock is a checkpoint one and if the current
	 * header has been claimed before moving on.
	 *
	 * If the current uberblock is a checkpointed uberblock then
	 * one of the following scenarios took place:
	 *
	 * 1] We are currently rewinding to the checkpoint of the pool.
	 * 2] We crashed in the middle of a checkpoint rewind but we
	 *    did manage to write the checkpointed uberblock to the
	 *    vdev labels, so when we tried to import the pool again
	 *    the checkpointed uberblock was selected from the import
	 *    procedure.
	 *
	 * In both cases we want to zero out all the ZIL blocks, except
	 * the ones that have been claimed at the time of the checkpoint
	 * (their zh_claim_txg != 0). The reason is that these blocks
	 * may be corrupted since we may have reused their locations on
	 * disk after we took the checkpoint.
	 *
	 * We could try to set spa_log_state to SPA_LOG_CLEAR earlier
	 * when we first figure out whether the current uberblock is
	 * checkpointed or not. Unfortunately, that would discard all
	 * the logs, including the ones that are claimed, and we would
	 * leak space.
	 */
	if (spa_get_log_state(zilog->zl_spa) == SPA_LOG_CLEAR ||
	    (zilog->zl_spa->spa_uberblock.ub_checkpoint_txg != 0 &&
	    zh->zh_claim_txg == 0)) {
		if (!BP_IS_HOLE(&zh->zh_log)) {
			(void) zil_parse(zilog, zil_clear_log_block,
			    zil_noop_log_record, tx, first_txg, B_FALSE);
		}
		BP_ZERO(&zh->zh_log);
		if (os->os_encrypted)
			os->os_next_write_raw[tx->tx_txg & TXG_MASK] = B_TRUE;
		dsl_dataset_dirty(dmu_objset_ds(os), tx);
		dmu_objset_disown(os, B_FALSE, FTAG);
		return (0);
	}

	/*
	 * If we are not rewinding and opening the pool normally, then
	 * the min_claim_txg should be equal to the first txg of the pool.
	 */
	ASSERT3U(first_txg, ==, spa_first_txg(zilog->zl_spa));

	/*
	 * Claim all log blocks if we haven't already done so, and remember
	 * the highest claimed sequence number.  This ensures that if we can
	 * read only part of the log now (e.g. due to a missing device),
	 * but we can read the entire log later, we will not try to replay
	 * or destroy beyond the last block we successfully claimed.
	 */
	ASSERT3U(zh->zh_claim_txg, <=, first_txg);
	if (zh->zh_claim_txg == 0 && !BP_IS_HOLE(&zh->zh_log)) {
		(void) zil_parse(zilog, zil_claim_log_block,
		    zil_claim_log_record, tx, first_txg, B_FALSE);
		zh->zh_claim_txg = first_txg;
		zh->zh_claim_blk_seq = zilog->zl_parse_blk_seq;
		zh->zh_claim_lr_seq = zilog->zl_parse_lr_seq;
		if (zilog->zl_parse_lr_count || zilog->zl_parse_blk_count > 1)
			zh->zh_flags |= ZIL_REPLAY_NEEDED;
		zh->zh_flags |= ZIL_CLAIM_LR_SEQ_VALID;
		if (os->os_encrypted)
			os->os_next_write_raw[tx->tx_txg & TXG_MASK] = B_TRUE;
		dsl_dataset_dirty(dmu_objset_ds(os), tx);
	}

	ASSERT3U(first_txg, ==, (spa_last_synced_txg(zilog->zl_spa) + 1));
	dmu_objset_disown(os, B_FALSE, FTAG);
	return (0);
}

/*
 * Check the log by walking the log chain.
 * Checksum errors are ok as they indicate the end of the chain.
 * Any other error (no device or read failure) returns an error.
 */
int
zil_check_log_chain(dsl_pool_t *dp, dsl_dataset_t *ds, void *tx)
{
	(void) dp;
	zilog_t *zilog;
	objset_t *os;
	blkptr_t *bp;
	int error;

	ASSERT(tx == NULL);

	error = dmu_objset_from_ds(ds, &os);
	if (error != 0) {
		cmn_err(CE_WARN, "can't open objset %llu, error %d",
		    (unsigned long long)ds->ds_object, error);
		return (0);
	}

	zilog = dmu_objset_zil(os);
	bp = (blkptr_t *)&zilog->zl_header->zh_log;

	if (!BP_IS_HOLE(bp)) {
		vdev_t *vd;
		boolean_t valid = B_TRUE;

		/*
		 * Check the first block and determine if it's on a log device
		 * which may have been removed or faulted prior to loading this
		 * pool.  If so, there's no point in checking the rest of the
		 * log as its content should have already been synced to the
		 * pool.
		 */
		spa_config_enter(os->os_spa, SCL_STATE, FTAG, RW_READER);
		vd = vdev_lookup_top(os->os_spa, DVA_GET_VDEV(&bp->blk_dva[0]));
		if (vd->vdev_islog && vdev_is_dead(vd))
			valid = vdev_log_state_valid(vd);
		spa_config_exit(os->os_spa, SCL_STATE, FTAG);

		if (!valid)
			return (0);

		/*
		 * Check whether the current uberblock is checkpointed (e.g.
		 * we are rewinding) and whether the current header has been
		 * claimed or not. If it hasn't then skip verifying it. We
		 * do this because its ZIL blocks may be part of the pool's
		 * state before the rewind, which is no longer valid.
		 */
		zil_header_t *zh = zil_header_in_syncing_context(zilog);
		if (zilog->zl_spa->spa_uberblock.ub_checkpoint_txg != 0 &&
		    zh->zh_claim_txg == 0)
			return (0);
	}

	/*
	 * Because tx == NULL, zil_claim_log_block() will not actually claim
	 * any blocks, but just determine whether it is possible to do so.
	 * In addition to checking the log chain, zil_claim_log_block()
	 * will invoke zio_claim() with a done func of spa_claim_notify(),
	 * which will update spa_max_claim_txg.  See spa_load() for details.
	 */
	error = zil_parse(zilog, zil_claim_log_block, zil_claim_log_record, tx,
	    zilog->zl_header->zh_claim_txg ? -1ULL :
	    spa_min_claim_txg(os->os_spa), B_FALSE);

	return ((error == ECKSUM || error == ENOENT) ? 0 : error);
}

/*
 * True if the ZIL has failed.
 */
static boolean_t
zil_failed(zilog_t *zilog)
{
	return ((zilog->zl_unfail_txg > 0) ? B_TRUE : B_FALSE);
}

static void zil_commit_waiter_skip(zil_commit_waiter_t *zcw);
static itxs_t *zil_alloc_itxs(void);
static void zil_itxg_clean_failed(zilog_t *zilog, uint64_t synced_txg);

/*
 * Fail the ZIL. This will collect up all failed itxs, and note that the ZIL
 * cannot be unfailed until all their txgs are synced. After this, all
 * zil_commit() calls will be serviced by the regular txg sync, and no new itxs
 * will be assigned or committed.
 */
static void
zil_fail(zilog_t *zilog)
{
	ASSERT(MUTEX_HELD(&zilog->zl_issuer_lock));
	ASSERT(MUTEX_HELD(&zilog->zl_lock));
	ASSERT(!zil_failed(zilog));

	itx_t *itx;
	list_t *l;

	list_t waiters;
	list_create(&waiters, sizeof (zil_commit_waiter_t),
	    offsetof(zil_commit_waiter_t, zcw_node));

	/*
	 * A ZIL failure occurs when an LWB write or flush fails, or an LWB
	 * can't be issued. This usually occurs when the pool suspends during
	 * zil_commit().
	 *
	 * The LWBs and the ZIL proper have outstanding itxs attached to them,
	 * with callbacks on them to be called when the data is safely on disk.
	 * If the pool never returns it doesn't matter; that data is never
	 * going to disk anyway. If the pool does return, and the txg covering
	 * those itxs is committed, then we need to fire those callbacks to
	 * make sure the rest of the system knows that its data is properly on
	 * disk.
	 *
	 * Outstandng itxs exist in in the live LWB list. The tail LWB is open,
	 * any others have issued IO. Once we set failure, zil_lwb_write_done()
	 * and zil_lwb_flush_vdevs_done() will quietly ignore any responses
	 * from live IO, so these LWBs will not progress any further. There may
	 * also be itxs on the commit list, that have been moved out of
	 * zl_itxg[] but not yet assigned to an itx.
	 *
	 * We collect up all those outstanding itxs, and put them onto
	 * fail_itxg. As we take each itx, we check its txg, and record the
	 * highest one we see. In the future, when that highest txg is
	 * committed, we know that all outstanding work is completed and the
	 * ZIL can be unfailed. See zil_sync() for details.
	 *
	 * In addition, each LWB has waiters attached, which threads are
	 * currently waiting on for zil_commit() to complete. We gather those
	 * so we can fire them via zil_commit_waiter_skip() after the unfail
	 * txg has been set. The first thing they'll do when they wake is to
	 * check the fail state, so we need to be done with our work before
	 * that.
	 */

	/*
	 * Prepare the fail itxg. This is not a real itxg, just a convenient
	 * holder for the itxs that couldn't be written and associated metadata
	 * until their transaction is committed and we can fire their
	 * callbacks.
	 *
	 * Note that once we take itxg_lock, zil_clean() is blocked until we're
	 * done.
	 */
	itxg_t *fail_itxg = &zilog->zl_fail_itxg;
	mutex_enter(&fail_itxg->itxg_lock);

	/*
	 * Starting txg for failure. Any itx seen on or before this txg is
	 * already committed to the pool.
	 */
	uint64_t last_synced_txg = spa_last_synced_txg(zilog->zl_spa);

	/*
	 * The highest txg we've seen across all itxs. We'll bump this as we
	 * scan them, and the ZIL can't resume until the pool has passed this
	 * txg, thus fully committing all itxs.
	 */
	uint64_t highest_txg = last_synced_txg;

	/*
	 * At the earliest opportunity, set all the failure state. We need
	 * zil_failed() in particular to return true so that zil_commit() does
	 * not proceed to itx processing, since we're about to start that
	 * ourselves.
	 */
	zilog->zl_unfail_txg = highest_txg + 1;

	for (int i = 0; i < TXG_SIZE; i++) {
		itxg_t *itxg = &zilog->zl_itxg[i];

		mutex_enter(&itxg->itxg_lock);
		ASSERT(!itxg->itxg_failed);
		itxg->itxg_failed = B_TRUE;
		mutex_exit(&itxg->itxg_lock);
	}

	/*
	 * Anything left in the failed itx must be from a previous failure
	 * in the past. We force it to empty out (signalling waiters) here
	 * so that we can properly calculate the new resume txg without
	 * having to scan the fail itx list. Those things have waited for
	 * zil_clean() to service them for a while already anyway.
	 */
	if (fail_itxg->itxg_txg > 0) {
		zil_itxg_clean_failed(zilog, last_synced_txg); 
		ASSERT3U(fail_itxg->itxg_txg, ==, 0);
		ASSERT3P(fail_itxg->itxg_itxs, ==, NULL);
	}

	fail_itxg->itxg_itxs = zil_alloc_itxs();

	/*
	 * Unlike the live itxgs, fail_itxg can carry itxs from multiple txgs,
	 * as zil_commit will collect all pending changes for a given foid,
	 * regardless of which txg its on. To handle this, we set its itxg_txg
	 * to the lowest txg of all the itxs it holds, as a cheap reference for
	 * what's left to process.
	 */
	fail_itxg->itxg_txg = UINT64_MAX;

	/* Process failed LWBs, oldest first */
	for (lwb_t *lwb = list_head(&zilog->zl_lwb_list); lwb;
	    lwb = list_next(&zilog->zl_lwb_list, lwb)) {
		/*
		 * All transactions for unissued or completed LWBs should be
		 * aborted by the time we get here. Conversely, transactions
		 * on in-flight LWBs will still be live, to be aborted in
		 * zil_lwb_flush_vdevs_done().
		 */
		IMPLY(lwb->lwb_state == LWB_STATE_CLOSED, lwb->lwb_tx == NULL);
		IMPLY(lwb->lwb_state == LWB_STATE_OPENED, lwb->lwb_tx == NULL);
		IMPLY(lwb->lwb_state == LWB_STATE_ISSUED, lwb->lwb_tx != NULL);
		IMPLY(lwb->lwb_state == LWB_STATE_WRITE_DONE,
		    lwb->lwb_tx != NULL);
		IMPLY(lwb->lwb_state == LWB_STATE_FLUSH_DONE,
		    lwb->lwb_tx == NULL);

		l = &lwb->lwb_itxs;
		while ((itx = list_head(l)) != NULL) {
			list_remove(l, itx);
			if (itx->itx_lr.lrc_txtype == TX_COMMIT) {
				/*
				 * zil_process_commit_list() already put the
				 * zcw for commit itxs on the waiter list, so
				 * we don't need to do anything special here.
				 */
				zil_itx_destroy(itx);
			} else if (itx->itx_lr.lrc_txg <= last_synced_txg) {
				/*
				 * txg covering this itx is alread on the main
				 * pool, so we can safely complete it now.
				 */
				zil_itx_destroy(itx);
			} else {
				/*
				 * Put the itx on the fail itxg sync list,
				 * and bump the unfail txg.
				 */
				list_insert_tail(
				    &fail_itxg->itxg_itxs->i_sync_list, itx);
				fail_itxg->itxg_txg = MIN(fail_itxg->itxg_txg,
				    itx->itx_lr.lrc_txg);

				highest_txg =
				    MAX(highest_txg, itx->itx_lr.lrc_txg);
			}
		}

		/* Take any waiters */
		list_move_tail(&waiters, &lwb->lwb_waiters);

		ASSERT(list_is_empty(&lwb->lwb_itxs));
		ASSERT(list_is_empty(&lwb->lwb_waiters));
	}

	/*
	 * Failures in zil_process_commit_list() will leave stuff on the
	 * commit list; bring those over too.
	 */
	l = &zilog->zl_itx_commit_list;
	while ((itx = list_head(l)) != NULL) {
		list_remove(l, itx);
		if (itx->itx_lr.lrc_txtype == TX_COMMIT) {
			/* Take commit waiters */
			list_insert_tail(&waiters, itx->itx_private);
			itx->itx_private = NULL;
			zil_itx_destroy(itx);
		} else if (itx->itx_lr.lrc_txg <= last_synced_txg) {
			/*
			 * txg covering this itx is alread on the main pool, so
			 * we can safely complete it now.
			 */
			zil_itx_destroy(itx);
		} else {

			/*
			 * Put the itx on the fail itxg sync list, and bump the
			 * unfail txg.
			 */
			list_insert_tail(
			    &fail_itxg->itxg_itxs->i_sync_list, itx);
			fail_itxg->itxg_txg = MIN(fail_itxg->itxg_txg,
			    itx->itx_lr.lrc_txg);

			highest_txg = MAX(highest_txg, itx->itx_lr.lrc_txg);
		}
	}
	ASSERT(list_is_empty(&zilog->zl_itx_commit_list));

	/*
	 * Its possible that we took no itxs if all the work was committed to
	 * the pool before the ZIL write errored; in that case there's only
	 * commit waiters left on the lists, and we just completed those.
	 *
	 * We've already flagged the itxs for failure though, so we still have
	 * to fail the whole ZIL. We'll just set its highest to be the last
	 * synced txg, so it can immediately reopen if the circumstances suit.
	 */
	if (fail_itxg->itxg_txg == UINT64_MAX)
		fail_itxg->itxg_txg = highest_txg;

	/* Prepare the live itxgs for failure */
	for (int i = 0; i < TXG_SIZE; i++) {
		itxg_t *itxg = &zilog->zl_itxg[i];

		mutex_enter(&itxg->itxg_lock);

		if (itxg->itxg_txg == 0) {
			/* Previously cleaned itxg, nothing to do. */
			ASSERT3U(itxg->itxg_itxs, ==, NULL);
			mutex_exit(&itxg->itxg_lock);
			continue;
		}

		/* Ensure unfail covers this itxg */
		highest_txg = MAX(highest_txg, itxg->itxg_txg);

		/*
		 * Scan the sync list for commit itxs, so we can signal their
		 * waiters. This shouldn't happen, but see zil_itxg_clean() for
		 * how they can legitimately end up here.
		 */
		l = &itxg->itxg_itxs->i_sync_list;
		itx = list_head(l);
		while (itx != NULL) {
			itx_t *nitx = list_next(l, itx);
			if (itx->itx_lr.lrc_txtype == TX_COMMIT) {
				list_remove(l, itx);
				list_insert_tail(&waiters, itx->itx_private);
				itx->itx_private = NULL;
				zil_itx_destroy(itx);
			}
			itx = nitx;
		}

		mutex_exit(&itxg->itxg_lock);
	}

	/* We've seen every outstanding itx, and have our unfail point */
	zilog->zl_unfail_txg = highest_txg + 1;

	mutex_exit(&fail_itxg->itxg_lock);

	char ds_name[ZFS_MAX_DATASET_NAME_LEN];
	dsl_dataset_name(dmu_objset_ds(zilog->zl_os), ds_name);
	cmn_err(CE_WARN,
	    "ZIL failed on pool '%s' dataset '%s': "
	    "last_txg=%llu; unfail_txg=%llu",
	    spa_name(zilog->zl_spa), ds_name,
	    last_synced_txg, zilog->zl_unfail_txg);

	/* Now inform all the waiters */
	zil_commit_waiter_t *zcw;
	while ((zcw = list_head(&waiters)) != NULL) {
		mutex_enter(&zcw->zcw_lock);

		ASSERT(list_link_active(&zcw->zcw_node));
		list_remove(&waiters, zcw);

		zcw->zcw_lwb = NULL;

		/*
		 * Set a reasonable error for zil_commit_impl() to find when
		 * zil_commit_waiter() returns.
		 */
		zcw->zcw_zio_error = EIO;

		ASSERT3B(zcw->zcw_done, ==, B_FALSE);
		zcw->zcw_done = B_TRUE;

		cv_broadcast(&zcw->zcw_cv);

		mutex_exit(&zcw->zcw_lock);
	}
}

/*
 * When an itx is "skipped", this function is used to properly mark the
 * waiter as "done, and signal any thread(s) waiting on it. An itx can
 * be skipped (and not committed to an lwb) for a variety of reasons,
 * one of them being that the itx was committed via spa_sync(), prior to
 * it being committed to an lwb; this can happen if a thread calling
 * zil_commit() is racing with spa_sync().
 */
static void
zil_commit_waiter_skip(zil_commit_waiter_t *zcw)
{
	mutex_enter(&zcw->zcw_lock);
	ASSERT3B(zcw->zcw_done, ==, B_FALSE);
	zcw->zcw_done = B_TRUE;
	cv_broadcast(&zcw->zcw_cv);
	mutex_exit(&zcw->zcw_lock);
}

/*
 * This function is used when the given waiter is to be linked into an
 * lwb's "lwb_waiter" list; i.e. when the itx is committed to the lwb.
 * At this point, the waiter will no longer be referenced by the itx,
 * and instead, will be referenced by the lwb.
 */
static void
zil_commit_waiter_link_lwb(zil_commit_waiter_t *zcw, lwb_t *lwb)
{
	/*
	 * The lwb_waiters field of the lwb is protected by the zilog's
	 * zl_lock, thus it must be held when calling this function.
	 */
	ASSERT(MUTEX_HELD(&lwb->lwb_zilog->zl_lock));

	mutex_enter(&zcw->zcw_lock);
	ASSERT(!list_link_active(&zcw->zcw_node));
	ASSERT3P(zcw->zcw_lwb, ==, NULL);
	ASSERT3P(lwb, !=, NULL);
	ASSERT(lwb->lwb_state == LWB_STATE_OPENED ||
	    lwb->lwb_state == LWB_STATE_ISSUED ||
	    lwb->lwb_state == LWB_STATE_WRITE_DONE);

	list_insert_tail(&lwb->lwb_waiters, zcw);
	zcw->zcw_lwb = lwb;
	mutex_exit(&zcw->zcw_lock);
}

/*
 * This function is used when zio_alloc_zil() fails to allocate a ZIL
 * block, and the given waiter must be linked to the "nolwb waiters"
 * list inside of zil_process_commit_list().
 */
static void
zil_commit_waiter_link_nolwb(zil_commit_waiter_t *zcw, list_t *nolwb)
{
	mutex_enter(&zcw->zcw_lock);
	ASSERT(!list_link_active(&zcw->zcw_node));
	ASSERT3P(zcw->zcw_lwb, ==, NULL);
	list_insert_tail(nolwb, zcw);
	mutex_exit(&zcw->zcw_lock);
}

void
zil_lwb_add_txg(lwb_t *lwb, uint64_t txg)
{
	lwb->lwb_max_txg = MAX(lwb->lwb_max_txg, txg);
}

/*
 * This function is a called after all vdevs associated with a given lwb
 * write have completed their DKIOCFLUSHWRITECACHE command; or as soon
 * as the lwb write completes, if "zil_nocacheflush" is set. Further,
 * all "previous" lwb's will have completed before this function is
 * called; i.e. this function is called for all previous lwbs before
 * it's called for "this" lwb (enforced via zio the dependencies
 * configured in zil_lwb_set_zio_dependency()).
 *
 * The intention is for this function to be called as soon as the
 * contents of an lwb are considered "stable" on disk, and will survive
 * any sudden loss of power. At this point, any threads waiting for the
 * lwb to reach this state are signalled, and the "waiter" structures
 * are marked "done".
 */
static void
zil_lwb_flush_vdevs_done(zio_t *zio)
{
	lwb_t *lwb = zio->io_private;
	zilog_t *zilog = lwb->lwb_zilog;
	dmu_tx_t *tx = lwb->lwb_tx;
	zil_commit_waiter_t *zcw;
	itx_t *itx;

	spa_config_exit(zilog->zl_spa, SCL_STATE, lwb);

	zio_buf_free(lwb->lwb_buf, lwb->lwb_sz);

	mutex_enter(&zilog->zl_lock);

	/*
	 * Ensure the lwb buffer pointer is cleared before releasing the
	 * txg. If we have had an allocation failure and the txg is
	 * waiting to sync then we want zil_sync() to remove the lwb so
	 * that it's not picked up as the next new one in
	 * zil_process_commit_list(). zil_sync() will only remove the
	 * lwb if lwb_buf is null.
	 */
	lwb->lwb_buf = NULL;
	lwb->lwb_tx = NULL;

	ASSERT3U(lwb->lwb_issued_timestamp, >, 0);
	zilog->zl_last_lwb_latency = gethrtime() - lwb->lwb_issued_timestamp;

	lwb->lwb_root_zio = NULL;

	ASSERT3S(lwb->lwb_state, ==, LWB_STATE_WRITE_DONE);
	lwb->lwb_state = LWB_STATE_FLUSH_DONE;

	if (zil_failed(zilog)) {
		/*
		 * The ZIL failed some time ago, so itxs have already been
		 * emptied and waiters informed, so there's nothing else
		 * to do here.
		 */
		mutex_exit(&zilog->zl_lock);

		/*
		 * This is the transaction for the next block; we have to
		 * commit it here otherwise txg_quiesce will block if the pool
		 * returns.
		 */
		dmu_tx_commit(tx);

		return;
	}

	if (zio->io_error != 0) {
		/*
		 * We expect any ZIO errors from child ZIOs to have been
		 * propagated "up" to this specific LWB's root ZIO, in
		 * order for this error handling to work correctly. This
		 * includes ZIO errors from either this LWB's write or
		 * flush, as well as any errors from other dependent LWBs
		 * (e.g. a root LWB ZIO that might be a child of this LWB).
		 */

		/*
		 * The IO failed, so we need to fail the ZIL by calling
		 * zil_fail().  To call zil_fail() we need to hold both
		 * zl_issuer_lock and zl_lock. We already have zl_lock, but
		 * zl_issuer_lock must be taken before zl_lock, so we must
		 * release and retake them in the right order.
		 */
		mutex_exit(&zilog->zl_lock);
		mutex_enter(&zilog->zl_issuer_lock);
		mutex_enter(&zilog->zl_lock);

		/*
		 * ZIL might have been failed by another thread while the locks
		 * were down, so we need to check again.
		 */
		if (!zil_failed(zilog))
			/* It wasn't, so fail it. */
			zil_fail(zilog);

		mutex_exit(&zilog->zl_lock);
		mutex_exit(&zilog->zl_issuer_lock);

		dmu_tx_commit(tx);

		return;
	}

	if (zilog->zl_last_lwb_opened == lwb) {
		/*
		 * Remember the highest committed log sequence number
		 * for ztest. We only update this value when all the log
		 * writes succeeded, because ztest wants to ASSERT that
		 * it got the whole log chain.
		 */
		zilog->zl_commit_lr_seq = zilog->zl_lr_seq;
	}

	while ((itx = list_head(&lwb->lwb_itxs)) != NULL) {
		list_remove(&lwb->lwb_itxs, itx);
		zil_itx_destroy(itx);
	}

	while ((zcw = list_head(&lwb->lwb_waiters)) != NULL) {
		mutex_enter(&zcw->zcw_lock);

		ASSERT(list_link_active(&zcw->zcw_node));
		list_remove(&lwb->lwb_waiters, zcw);

		ASSERT3P(zcw->zcw_lwb, ==, lwb);
		zcw->zcw_lwb = NULL;

		ASSERT3B(zcw->zcw_done, ==, B_FALSE);
		zcw->zcw_done = B_TRUE;
		cv_broadcast(&zcw->zcw_cv);

		mutex_exit(&zcw->zcw_lock);
	}

	mutex_exit(&zilog->zl_lock);

	/*
	 * Now that we've written this log block, we have a stable pointer
	 * to the next block in the chain, so it's OK to let the txg in
	 * which we allocated the next block sync.
	 */
	dmu_tx_commit(tx);
}

/*
 * This is called when an lwb's write zio completes. The callback's
 * purpose is to issue the DKIOCFLUSHWRITECACHE commands for the vdevs
 * in the lwb's lwb_vdev_tree. The tree will contain the vdevs involved
 * in writing out this specific lwb's data, and in the case that cache
 * flushes have been deferred, vdevs involved in writing the data for
 * previous lwbs. The writes corresponding to all the vdevs in the
 * lwb_vdev_tree will have completed by the time this is called, due to
 * the zio dependencies configured in zil_lwb_set_zio_dependency(),
 * which takes deferred flushes into account. The lwb will be "done"
 * once zil_lwb_flush_vdevs_done() is called, which occurs in the zio
 * completion callback for the lwb's root zio.
 */
static void
zil_lwb_write_done(zio_t *zio)
{
	lwb_t *lwb = zio->io_private;
	spa_t *spa = zio->io_spa;
	zilog_t *zilog = lwb->lwb_zilog;
	lwb_t *nlwb;
	void *cookie;
	avl_tree_t *vt, *t;
	zio_vdev_trace_t *zvt, *nzvt, zvt_search;
	avl_index_t where;

	ASSERT3S(spa_config_held(spa, SCL_STATE, RW_READER), !=, 0);

	ASSERT(BP_GET_COMPRESS(zio->io_bp) == ZIO_COMPRESS_OFF);
	ASSERT(BP_GET_TYPE(zio->io_bp) == DMU_OT_INTENT_LOG);
	ASSERT(BP_GET_LEVEL(zio->io_bp) == 0);
	ASSERT(BP_GET_BYTEORDER(zio->io_bp) == ZFS_HOST_BYTEORDER);
	ASSERT(!BP_IS_GANG(zio->io_bp));
	ASSERT(!BP_IS_HOLE(zio->io_bp));
	ASSERT(BP_GET_FILL(zio->io_bp) == 0);

	abd_free(zio->io_abd);

	mutex_enter(&zilog->zl_lock);
	ASSERT3S(lwb->lwb_state, ==, LWB_STATE_ISSUED);
	lwb->lwb_state = LWB_STATE_WRITE_DONE;
	lwb->lwb_write_zio = NULL;
	lwb->lwb_fastwrite = FALSE;
	nlwb = list_next(&zilog->zl_lwb_list, lwb);
	mutex_exit(&zilog->zl_lock);

	/* Flushes disabled, so skip everything */
	if (zil_nocacheflush)
		return;

	/*
	 * If there was an IO error, or the ZIL failed, there's no point
	 * issuing flushes, just get out. Any error handling or cleanup will be
	 * done in zil_lwb_flush_vdevs_done().
	 */
	if (zil_failed(zilog) || zio->io_error != 0)
		return;

	/*
	 * If this lwb does not have any threads waiting for it to
	 * complete, we want to defer issuing the DKIOCFLUSHWRITECACHE
	 * command to the vdevs written to by "this" lwb, and instead
	 * rely on the "next" lwb to handle the DKIOCFLUSHWRITECACHE
	 * command for those vdevs. Thus, we merge the vdev tree of
	 * "this" lwb with the vdev tree of the "next" lwb in the list,
	 * and assume the "next" lwb will handle flushing the vdevs (or
	 * deferring the flush(s) again).
	 *
	 * This is a useful performance optimization, especially for
	 * workloads with lots of async write activity and few sync
	 * write and/or fsync activity, as it has the potential to
	 * coalesce multiple flush commands to a vdev into one.
	 */
	boolean_t defer =
	    (list_head(&lwb->lwb_waiters) == NULL && nlwb != NULL);

	/* Select the right lwb vdev tree to copy the trace tree to */
	vt = &zio->io_vdev_trace_tree;
	if (defer)
		t = &nlwb->lwb_vdev_tree;
	else
		t = &lwb->lwb_vdev_tree;

	/*
	 * And copy it. Note that we can't just take these, as they're owned
	 * by the zio.
	 */
	for (zvt = avl_first(vt); zvt != NULL; zvt = AVL_NEXT(vt, zvt)) {
		zvt_search.zvt_guid = zvt->zvt_guid;
		if (avl_find(t, &zvt_search, &where) == NULL) {
			nzvt = zio_vdev_trace_alloc();
			nzvt->zvt_guid = zvt->zvt_guid;
			avl_insert(t, nzvt, where);
		}
	}

	if (defer) {
		/*
		 * If we're deferring, copy any remaining vdev nodes to the
		 * next lwb. There might still be some here if a previous
		 * write deferred to us.
		 */
		vt = &lwb->lwb_vdev_tree;
		cookie = NULL;
		while ((zvt = avl_destroy_nodes(vt, &cookie)) != NULL) {
			if (avl_find(t, zvt, &where) == NULL)
				avl_insert(t, zvt, where);
			else
				zio_vdev_trace_free(zvt);
		}
		avl_destroy(vt);
		return;
	}

	zio_flush_traced(lwb->lwb_root_zio, t, B_TRUE);

	cookie = NULL;
	while ((zvt = avl_destroy_nodes(t, &cookie)) != NULL)
		zio_vdev_trace_free(zvt);
	avl_destroy(t);
}

static void
zil_lwb_set_zio_dependency(zilog_t *zilog, lwb_t *lwb)
{
	lwb_t *last_lwb_opened = zilog->zl_last_lwb_opened;

	ASSERT(MUTEX_HELD(&zilog->zl_issuer_lock));
	ASSERT(MUTEX_HELD(&zilog->zl_lock));

	/*
	 * The zilog's "zl_last_lwb_opened" field is used to build the
	 * lwb/zio dependency chain, which is used to preserve the
	 * ordering of lwb completions that is required by the semantics
	 * of the ZIL. Each new lwb zio becomes a parent of the
	 * "previous" lwb zio, such that the new lwb's zio cannot
	 * complete until the "previous" lwb's zio completes.
	 *
	 * This is required by the semantics of zil_commit(); the commit
	 * waiters attached to the lwbs will be woken in the lwb zio's
	 * completion callback, so this zio dependency graph ensures the
	 * waiters are woken in the correct order (the same order the
	 * lwbs were created).
	 */
	if (last_lwb_opened != NULL &&
	    last_lwb_opened->lwb_state != LWB_STATE_FLUSH_DONE) {
		ASSERT(last_lwb_opened->lwb_state == LWB_STATE_OPENED ||
		    last_lwb_opened->lwb_state == LWB_STATE_ISSUED ||
		    last_lwb_opened->lwb_state == LWB_STATE_WRITE_DONE);

		ASSERT3P(last_lwb_opened->lwb_root_zio, !=, NULL);
		zio_add_child(lwb->lwb_root_zio,
		    last_lwb_opened->lwb_root_zio);

		/*
		 * If the previous lwb's write hasn't already completed,
		 * we also want to order the completion of the lwb write
		 * zios (above, we only order the completion of the lwb
		 * root zios). This is required because of how we can
		 * defer the DKIOCFLUSHWRITECACHE commands for each lwb.
		 *
		 * When the DKIOCFLUSHWRITECACHE commands are deferred,
		 * the previous lwb will rely on this lwb to flush the
		 * vdevs written to by that previous lwb. Thus, we need
		 * to ensure this lwb doesn't issue the flush until
		 * after the previous lwb's write completes. We ensure
		 * this ordering by setting the zio parent/child
		 * relationship here.
		 *
		 * Without this relationship on the lwb's write zio,
		 * it's possible for this lwb's write to complete prior
		 * to the previous lwb's write completing; and thus, the
		 * vdevs for the previous lwb would be flushed prior to
		 * that lwb's data being written to those vdevs (the
		 * vdevs are flushed in the lwb write zio's completion
		 * handler, zil_lwb_write_done()).
		 */
		if (last_lwb_opened->lwb_state != LWB_STATE_WRITE_DONE) {
			ASSERT(last_lwb_opened->lwb_state == LWB_STATE_OPENED ||
			    last_lwb_opened->lwb_state == LWB_STATE_ISSUED);

			ASSERT3P(last_lwb_opened->lwb_write_zio, !=, NULL);
			zio_add_child(lwb->lwb_write_zio,
			    last_lwb_opened->lwb_write_zio);
		}
	}
}


/*
 * This function's purpose is to "open" an lwb such that it is ready to
 * accept new itxs being committed to it. To do this, the lwb's zio
 * structures are created, and linked to the lwb. This function is
 * idempotent; if the passed in lwb has already been opened, this
 * function is essentially a no-op.
 */
static void
zil_lwb_write_open(zilog_t *zilog, lwb_t *lwb)
{
	zbookmark_phys_t zb;
	zio_priority_t prio;

	ASSERT(MUTEX_HELD(&zilog->zl_issuer_lock));
	ASSERT3P(lwb, !=, NULL);
	EQUIV(lwb->lwb_root_zio == NULL, lwb->lwb_state == LWB_STATE_CLOSED);
	EQUIV(lwb->lwb_root_zio != NULL, lwb->lwb_state == LWB_STATE_OPENED);

	SET_BOOKMARK(&zb, lwb->lwb_blk.blk_cksum.zc_word[ZIL_ZC_OBJSET],
	    ZB_ZIL_OBJECT, ZB_ZIL_LEVEL,
	    lwb->lwb_blk.blk_cksum.zc_word[ZIL_ZC_SEQ]);

	/* Lock so zil_sync() doesn't fastwrite_unmark after zio is created */
	mutex_enter(&zilog->zl_lock);
	if (lwb->lwb_root_zio == NULL) {
		abd_t *lwb_abd = abd_get_from_buf(lwb->lwb_buf,
		    BP_GET_LSIZE(&lwb->lwb_blk));

		if (!lwb->lwb_fastwrite) {
			metaslab_fastwrite_mark(zilog->zl_spa, &lwb->lwb_blk);
			lwb->lwb_fastwrite = 1;
		}

		if (!lwb->lwb_slog || zilog->zl_cur_used <= zil_slog_bulk)
			prio = ZIO_PRIORITY_SYNC_WRITE;
		else
			prio = ZIO_PRIORITY_ASYNC_WRITE;

		lwb->lwb_root_zio = zio_root(zilog->zl_spa,
		    zil_lwb_flush_vdevs_done, lwb, ZIO_FLAG_CANFAIL);
		ASSERT3P(lwb->lwb_root_zio, !=, NULL);

		lwb->lwb_write_zio = zio_rewrite(lwb->lwb_root_zio,
		    zilog->zl_spa, 0, &lwb->lwb_blk, lwb_abd,
		    BP_GET_LSIZE(&lwb->lwb_blk), zil_lwb_write_done, lwb,
		    prio, ZIO_FLAG_CANFAIL | ZIO_FLAG_FASTWRITE |
		    ZIO_FLAG_VDEV_TRACE, &zb);
		ASSERT3P(lwb->lwb_write_zio, !=, NULL);

		lwb->lwb_state = LWB_STATE_OPENED;

		zil_lwb_set_zio_dependency(zilog, lwb);
		zilog->zl_last_lwb_opened = lwb;
	}
	mutex_exit(&zilog->zl_lock);

	ASSERT3P(lwb->lwb_root_zio, !=, NULL);
	ASSERT3P(lwb->lwb_write_zio, !=, NULL);
	ASSERT3S(lwb->lwb_state, ==, LWB_STATE_OPENED);
}

/*
 * Define a limited set of intent log block sizes.
 *
 * These must be a multiple of 4KB. Note only the amount used (again
 * aligned to 4KB) actually gets written. However, we can't always just
 * allocate SPA_OLD_MAXBLOCKSIZE as the slog space could be exhausted.
 */
struct {
	uint64_t	limit;
	uint64_t	blksz;
} zil_block_buckets[] = {
	{ 4096,		4096 },			/* non TX_WRITE */
	{ 8192 + 4096,	8192 + 4096 },		/* database */
	{ 32768 + 4096,	32768 + 4096 },		/* NFS writes */
	{ 65536 + 4096,	65536 + 4096 },		/* 64KB writes */
	{ 131072,	131072 },		/* < 128KB writes */
	{ 131072 +4096,	65536 + 4096 },		/* 128KB writes */
	{ UINT64_MAX,	SPA_OLD_MAXBLOCKSIZE},	/* > 128KB writes */
};

/*
 * Maximum block size used by the ZIL.  This is picked up when the ZIL is
 * initialized.  Otherwise this should not be used directly; see
 * zl_max_block_size instead.
 */
int zil_maxblocksize = SPA_OLD_MAXBLOCKSIZE;

/*
 * Start a log block write and advance to the next log block.
 * Calls are serialized.
 */
static lwb_t *
zil_lwb_write_issue(zilog_t *zilog, lwb_t *lwb)
{
	lwb_t *nlwb = NULL;
	zil_chain_t *zilc;
	spa_t *spa = zilog->zl_spa;
	blkptr_t *bp;
	dmu_tx_t *tx;
	uint64_t txg;
	uint64_t zil_blksz, wsz;
	int i, error;
	boolean_t slog;

	ASSERT(MUTEX_HELD(&zilog->zl_issuer_lock));
	ASSERT3P(lwb->lwb_root_zio, !=, NULL);
	ASSERT3P(lwb->lwb_write_zio, !=, NULL);
	ASSERT3S(lwb->lwb_state, ==, LWB_STATE_OPENED);

	if (BP_GET_CHECKSUM(&lwb->lwb_blk) == ZIO_CHECKSUM_ZILOG2) {
		zilc = (zil_chain_t *)lwb->lwb_buf;
		bp = &zilc->zc_next_blk;
	} else {
		zilc = (zil_chain_t *)(lwb->lwb_buf + lwb->lwb_sz);
		bp = &zilc->zc_next_blk;
	}

	ASSERT(lwb->lwb_nused <= lwb->lwb_sz);

	/*
	 * Allocate the next block and save its address in this block
	 * before writing it in order to establish the log chain.
	 * Note that if the allocation of nlwb synced before we wrote
	 * the block that points at it (lwb), we'd leak it if we crashed.
	 * Therefore, we don't do dmu_tx_commit() until zil_lwb_write_done().
	 * We dirty the dataset to ensure that zil_sync() will be called
	 * to clean up in the event of allocation failure or I/O failure.
	 */

	tx = dmu_tx_create(zilog->zl_os);

	/*
	 * Since we are not going to create any new dirty data, and we
	 * can even help with clearing the existing dirty data, we
	 * should not be subject to the dirty data based delays. We
	 * use DMU_TX_ASSIGN_NOTHROTTLE to bypass the delay mechanism.
	 */
	error = dmu_tx_assign(tx, DMU_TX_ASSIGN_WAIT |
	    DMU_TX_ASSIGN_NOTHROTTLE | DMU_TX_ASSIGN_NOSUSPEND);
	if (error != 0) {
		ASSERT3S(error, ==, EAGAIN);
		dmu_tx_abort(tx);
		return (NULL);
	}

	dsl_dataset_dirty(dmu_objset_ds(zilog->zl_os), tx);
	txg = dmu_tx_get_txg(tx);

	lwb->lwb_tx = tx;

	/*
	 * Log blocks are pre-allocated. Here we select the size of the next
	 * block, based on size used in the last block.
	 * - first find the smallest bucket that will fit the block from a
	 *   limited set of block sizes. This is because it's faster to write
	 *   blocks allocated from the same metaslab as they are adjacent or
	 *   close.
	 * - next find the maximum from the new suggested size and an array of
	 *   previous sizes. This lessens a picket fence effect of wrongly
	 *   guessing the size if we have a stream of say 2k, 64k, 2k, 64k
	 *   requests.
	 *
	 * Note we only write what is used, but we can't just allocate
	 * the maximum block size because we can exhaust the available
	 * pool log space.
	 */
	zil_blksz = zilog->zl_cur_used + sizeof (zil_chain_t);
	for (i = 0; zil_blksz > zil_block_buckets[i].limit; i++)
		continue;
	zil_blksz = MIN(zil_block_buckets[i].blksz, zilog->zl_max_block_size);
	zilog->zl_prev_blks[zilog->zl_prev_rotor] = zil_blksz;
	for (i = 0; i < ZIL_PREV_BLKS; i++)
		zil_blksz = MAX(zil_blksz, zilog->zl_prev_blks[i]);
	zilog->zl_prev_rotor = (zilog->zl_prev_rotor + 1) & (ZIL_PREV_BLKS - 1);

	BP_ZERO(bp);
	error = zio_alloc_zil(spa, zilog->zl_os, txg, bp, zil_blksz, &slog);
	if (slog) {
		ZIL_STAT_BUMP(zil_itx_metaslab_slog_count);
		ZIL_STAT_INCR(zil_itx_metaslab_slog_bytes, lwb->lwb_nused);
	} else {
		ZIL_STAT_BUMP(zil_itx_metaslab_normal_count);
		ZIL_STAT_INCR(zil_itx_metaslab_normal_bytes, lwb->lwb_nused);
	}
	if (error == 0) {
		ASSERT3U(bp->blk_birth, ==, txg);
		bp->blk_cksum = lwb->lwb_blk.blk_cksum;
		bp->blk_cksum.zc_word[ZIL_ZC_SEQ]++;

		/*
		 * Allocate a new log write block (lwb).
		 */
		nlwb = zil_alloc_lwb(zilog, bp, slog, txg, TRUE);
	}

	if (BP_GET_CHECKSUM(&lwb->lwb_blk) == ZIO_CHECKSUM_ZILOG2) {
		/* For Slim ZIL only write what is used. */
		wsz = P2ROUNDUP_TYPED(lwb->lwb_nused, ZIL_MIN_BLKSZ, uint64_t);
		ASSERT3U(wsz, <=, lwb->lwb_sz);
		zio_shrink(lwb->lwb_write_zio, wsz);

	} else {
		wsz = lwb->lwb_sz;
	}

	zilc->zc_pad = 0;
	zilc->zc_nused = lwb->lwb_nused;
	zilc->zc_eck.zec_cksum = lwb->lwb_blk.blk_cksum;

	/*
	 * clear unused data for security
	 */
	bzero(lwb->lwb_buf + lwb->lwb_nused, wsz - lwb->lwb_nused);

	spa_config_enter(zilog->zl_spa, SCL_STATE, lwb, RW_READER);

	lwb->lwb_issued_timestamp = gethrtime();
	lwb->lwb_state = LWB_STATE_ISSUED;

	zio_nowait(lwb->lwb_root_zio);
	zio_nowait(lwb->lwb_write_zio);

	/*
	 * If there was an allocation failure then nlwb will be null which
	 * forces a txg_wait_synced().
	 */
	return (nlwb);
}

/*
 * Maximum amount of write data that can be put into single log block.
 */
uint64_t
zil_max_log_data(zilog_t *zilog)
{
	return (zilog->zl_max_block_size -
	    sizeof (zil_chain_t) - sizeof (lr_write_t));
}

/*
 * Maximum amount of log space we agree to waste to reduce number of
 * WR_NEED_COPY chunks to reduce zl_get_data() overhead (~12%).
 */
static inline uint64_t
zil_max_waste_space(zilog_t *zilog)
{
	return (zil_max_log_data(zilog) / 8);
}

/*
 * Maximum amount of write data for WR_COPIED.  For correctness, consumers
 * must fall back to WR_NEED_COPY if we can't fit the entire record into one
 * maximum sized log block, because each WR_COPIED record must fit in a
 * single log block.  For space efficiency, we want to fit two records into a
 * max-sized log block.
 */
uint64_t
zil_max_copied_data(zilog_t *zilog)
{
	return ((zilog->zl_max_block_size - sizeof (zil_chain_t)) / 2 -
	    sizeof (lr_write_t));
}

static lwb_t *
zil_lwb_commit(zilog_t *zilog, itx_t *itx, lwb_t *lwb)
{
	lr_t *lrcb, *lrc;
	lr_write_t *lrwb, *lrw;
	char *lr_buf;
	uint64_t dlen, dnow, dpad, lwb_sp, reclen, txg, max_log_data;

	ASSERT(MUTEX_HELD(&zilog->zl_issuer_lock));
	ASSERT3P(lwb, !=, NULL);
	ASSERT3P(lwb->lwb_buf, !=, NULL);

	zil_lwb_write_open(zilog, lwb);

	lrc = &itx->itx_lr;
	lrw = (lr_write_t *)lrc;

	/*
	 * A commit itx doesn't represent any on-disk state; instead
	 * it's simply used as a place holder on the commit list, and
	 * provides a mechanism for attaching a "commit waiter" onto the
	 * correct lwb (such that the waiter can be signalled upon
	 * completion of that lwb). Thus, we don't process this itx's
	 * log record if it's a commit itx (these itx's don't have log
	 * records), and instead link the itx's waiter onto the lwb's
	 * list of waiters.
	 *
	 * For more details, see the comment above zil_commit().
	 */
	if (lrc->lrc_txtype == TX_COMMIT) {
		mutex_enter(&zilog->zl_lock);
		zil_commit_waiter_link_lwb(itx->itx_private, lwb);
		itx->itx_private = NULL;
		mutex_exit(&zilog->zl_lock);
		return (lwb);
	}

	if (lrc->lrc_txtype == TX_WRITE && itx->itx_wr_state == WR_NEED_COPY) {
		dlen = P2ROUNDUP_TYPED(
		    lrw->lr_length, sizeof (uint64_t), uint64_t);
		dpad = dlen - lrw->lr_length;
	} else {
		dlen = dpad = 0;
	}
	reclen = lrc->lrc_reclen;
	zilog->zl_cur_used += (reclen + dlen);
	txg = lrc->lrc_txg;

	ASSERT3U(zilog->zl_cur_used, <, UINT64_MAX - (reclen + dlen));

cont:
	/*
	 * If this record won't fit in the current log block, start a new one.
	 * For WR_NEED_COPY optimize layout for minimal number of chunks.
	 */
	lwb_sp = lwb->lwb_sz - lwb->lwb_nused;
	max_log_data = zil_max_log_data(zilog);
	if (reclen > lwb_sp || (reclen + dlen > lwb_sp &&
	    lwb_sp < zil_max_waste_space(zilog) &&
	    (dlen % max_log_data == 0 ||
	    lwb_sp < reclen + dlen % max_log_data))) {
		lwb = zil_lwb_write_issue(zilog, lwb);
		if (lwb == NULL)
			return (NULL);
		zil_lwb_write_open(zilog, lwb);
		ASSERT(LWB_EMPTY(lwb));
		lwb_sp = lwb->lwb_sz - lwb->lwb_nused;

		/*
		 * There must be enough space in the new, empty log block to
		 * hold reclen.  For WR_COPIED, we need to fit the whole
		 * record in one block, and reclen is the header size + the
		 * data size. For WR_NEED_COPY, we can create multiple
		 * records, splitting the data into multiple blocks, so we
		 * only need to fit one word of data per block; in this case
		 * reclen is just the header size (no data).
		 */
		ASSERT3U(reclen + MIN(dlen, sizeof (uint64_t)), <=, lwb_sp);
	}

	dnow = MIN(dlen, lwb_sp - reclen);
	lr_buf = lwb->lwb_buf + lwb->lwb_nused;
	bcopy(lrc, lr_buf, reclen);
	lrcb = (lr_t *)lr_buf;		/* Like lrc, but inside lwb. */
	lrwb = (lr_write_t *)lrcb;	/* Like lrw, but inside lwb. */

	ZIL_STAT_BUMP(zil_itx_count);

	/*
	 * If it's a write, fetch the data or get its blkptr as appropriate.
	 */
	if (lrc->lrc_txtype == TX_WRITE) {
		if (txg > spa_freeze_txg(zilog->zl_spa))
			/*
			 * We're allowed to block here as freeze is only used by
			 * ztest, which should always be a stable and controlled
			 * environment.
			 */
			txg_wait_synced(zilog->zl_dmu_pool, txg);
		if (itx->itx_wr_state == WR_COPIED) {
			ZIL_STAT_BUMP(zil_itx_copied_count);
			ZIL_STAT_INCR(zil_itx_copied_bytes, lrw->lr_length);
		} else {
			char *dbuf;
			int error;

			if (itx->itx_wr_state == WR_NEED_COPY) {
				dbuf = lr_buf + reclen;
				lrcb->lrc_reclen += dnow;
				if (lrwb->lr_length > dnow)
					lrwb->lr_length = dnow;
				lrw->lr_offset += dnow;
				lrw->lr_length -= dnow;
				ZIL_STAT_BUMP(zil_itx_needcopy_count);
				ZIL_STAT_INCR(zil_itx_needcopy_bytes, dnow);
			} else {
				ASSERT3S(itx->itx_wr_state, ==, WR_INDIRECT);
				dbuf = NULL;
				ZIL_STAT_BUMP(zil_itx_indirect_count);
				ZIL_STAT_INCR(zil_itx_indirect_bytes,
				    lrw->lr_length);
			}

			/*
			 * We pass in the "lwb_write_zio" rather than
			 * "lwb_root_zio" so that the "lwb_write_zio"
			 * becomes the parent of any zio's created by
			 * the "zl_get_data" callback. The vdevs are
			 * flushed after the "lwb_write_zio" completes,
			 * so we want to make sure that completion
			 * callback waits for these additional zio's,
			 * such that the vdevs used by those zio's will
			 * be included in the lwb's vdev tree, and those
			 * vdevs will be properly flushed. If we passed
			 * in "lwb_root_zio" here, then these additional
			 * vdevs may not be flushed; e.g. if these zio's
			 * completed after "lwb_write_zio" completed.
			 */
			error = zilog->zl_get_data(itx->itx_private,
			    itx->itx_gen, lrwb, dbuf, lwb,
			    lwb->lwb_write_zio);
			if (dbuf != NULL && error == 0 && dnow == dlen)
				/* Zero any padding bytes in the last block. */
				bzero((char *)dbuf + lrwb->lr_length, dpad);

			if (error == EIO) {
				/*
				 * The zl_get_data callback couldn't give us
				 * the data. This is almost certainly because
				 * the pool suspended in dmu_sync_late_arrival()
				 * but we attempt to force the txg out anyway
				 * just to be sure. If it really is suspended,
				 * it'll return immediately.
				 */
				error = txg_wait_synced_flags(
				    zilog->zl_dmu_pool, txg,
				    TXG_WAIT_F_NOSUSPEND);
				if (error == 0)
					/* Surprising, but we'll take it. */
					return (lwb);

				/*
				 * The pool has suspended, and we're being
				 * asked to commit an itx whose associated data
				 * has no block pointer. This can't be
				 * resolved, so the itx can't be committed to
				 * this lwb. All we can do is signal this fact
				 * to the caller and let it clean up.
				 */
				ASSERT3S(error, ==, EAGAIN);
				return (NULL);
			}

			if (error != 0) {
				ASSERT(error == ENOENT || error == EEXIST ||
				    error == EALREADY);
				return (lwb);
			}
		}
	}

	/*
	 * We're actually making an entry, so update lrc_seq to be the
	 * log record sequence number.  Note that this is generally not
	 * equal to the itx sequence number because not all transactions
	 * are synchronous, and sometimes spa_sync() gets there first.
	 */
	lrcb->lrc_seq = ++zilog->zl_lr_seq;
	lwb->lwb_nused += reclen + dnow;

	zil_lwb_add_txg(lwb, txg);

	ASSERT3U(lwb->lwb_nused, <=, lwb->lwb_sz);
	ASSERT0(P2PHASE(lwb->lwb_nused, sizeof (uint64_t)));

	dlen -= dnow;
	if (dlen > 0) {
		zilog->zl_cur_used += reclen;
		goto cont;
	}

	return (lwb);
}

itx_t *
zil_itx_create(uint64_t txtype, size_t olrsize)
{
	size_t itxsize, lrsize;
	itx_t *itx;

	lrsize = P2ROUNDUP_TYPED(olrsize, sizeof (uint64_t), size_t);
	itxsize = offsetof(itx_t, itx_lr) + lrsize;

	itx = zio_data_buf_alloc(itxsize);
	itx->itx_lr.lrc_txtype = txtype;
	itx->itx_lr.lrc_reclen = lrsize;
	itx->itx_lr.lrc_seq = 0;	/* defensive */
	bzero((char *)&itx->itx_lr + olrsize, lrsize - olrsize);
	itx->itx_sync = B_TRUE;		/* default is synchronous */
	itx->itx_callback = NULL;
	itx->itx_callback_data = NULL;
	itx->itx_size = itxsize;

	return (itx);
}

void
zil_itx_destroy(itx_t *itx)
{
	IMPLY(itx->itx_lr.lrc_txtype == TX_COMMIT, itx->itx_callback == NULL);
	IMPLY(itx->itx_callback != NULL, itx->itx_lr.lrc_txtype != TX_COMMIT);

	if (itx->itx_callback != NULL)
		itx->itx_callback(itx->itx_callback_data);

	zio_data_buf_free(itx, itx->itx_size);
}

/*
 * Free up the sync and async itxs. The itxs_t has already been detached
 * so no locks are needed.
 */
static void
zil_itxg_clean(void *arg)
{
	itx_t *itx;
	list_t *list;
	avl_tree_t *t;
	void *cookie;
	itxs_t *itxs = arg;
	itx_async_node_t *ian;

	list = &itxs->i_sync_list;
	while ((itx = list_head(list)) != NULL) {
		/*
		 * In the general case, commit itxs will not be found
		 * here, as they'll be committed to an lwb via
		 * zil_lwb_commit(), and free'd in that function. Having
		 * said that, it is still possible for commit itxs to be
		 * found here, due to the following race:
		 *
		 *	- a thread calls zil_commit() which assigns the
		 *	  commit itx to a per-txg i_sync_list
		 *	- zil_itxg_clean() is called (e.g. via spa_sync())
		 *	  while the waiter is still on the i_sync_list
		 *
		 * There's nothing to prevent syncing the txg while the
		 * waiter is on the i_sync_list. This normally doesn't
		 * happen because spa_sync() is slower than zil_commit(),
		 * but if zil_commit() calls txg_wait_synced() (e.g.
		 * because zil_create() or zil_commit_writer_stall() is
		 * called) we will hit this case.
		 */
		if (itx->itx_lr.lrc_txtype == TX_COMMIT)
			zil_commit_waiter_skip(itx->itx_private);

		list_remove(list, itx);
		zil_itx_destroy(itx);
	}

	cookie = NULL;
	t = &itxs->i_async_tree;
	while ((ian = avl_destroy_nodes(t, &cookie)) != NULL) {
		list = &ian->ia_list;
		while ((itx = list_head(list)) != NULL) {
			list_remove(list, itx);
			/* commit itxs should never be on the async lists. */
			ASSERT3U(itx->itx_lr.lrc_txtype, !=, TX_COMMIT);
			zil_itx_destroy(itx);
		}
		list_destroy(list);
		kmem_free(ian, sizeof (itx_async_node_t));
	}
	avl_destroy(t);

	kmem_free(itxs, sizeof (itxs_t));
}

static int
zil_aitx_compare(const void *x1, const void *x2)
{
	const uint64_t o1 = ((itx_async_node_t *)x1)->ia_foid;
	const uint64_t o2 = ((itx_async_node_t *)x2)->ia_foid;

	return (TREE_CMP(o1, o2));
}

/*
 * Remove all async itx with the given oid.
 */
void
zil_remove_async(zilog_t *zilog, uint64_t oid)
{
	uint64_t otxg, txg;
	itx_async_node_t *ian;
	avl_tree_t *t;
	avl_index_t where;
	list_t clean_list;
	itx_t *itx;

	ASSERT(oid != 0);
	list_create(&clean_list, sizeof (itx_t), offsetof(itx_t, itx_node));

	if (spa_freeze_txg(zilog->zl_spa) != UINT64_MAX) /* ziltest support */
		otxg = ZILTEST_TXG;
	else
		otxg = spa_last_synced_txg(zilog->zl_spa) + 1;

	for (txg = otxg; txg < (otxg + TXG_CONCURRENT_STATES); txg++) {
		itxg_t *itxg = &zilog->zl_itxg[txg & TXG_MASK];

		mutex_enter(&itxg->itxg_lock);
		if (itxg->itxg_txg != txg) {
			mutex_exit(&itxg->itxg_lock);
			continue;
		}

		/*
		 * Locate the object node and append its list.
		 */
		t = &itxg->itxg_itxs->i_async_tree;
		ian = avl_find(t, &oid, &where);
		if (ian != NULL)
			list_move_tail(&clean_list, &ian->ia_list);
		mutex_exit(&itxg->itxg_lock);
	}
	while ((itx = list_head(&clean_list)) != NULL) {
		list_remove(&clean_list, itx);
		/* commit itxs should never be on the async lists. */
		ASSERT3U(itx->itx_lr.lrc_txtype, !=, TX_COMMIT);
		zil_itx_destroy(itx);
	}
	list_destroy(&clean_list);
}

static itxs_t *
zil_alloc_itxs(void)
{
	itxs_t *itxs = kmem_zalloc(sizeof (itxs_t), KM_SLEEP);
	list_create(&itxs->i_sync_list, sizeof (itx_t),
	    offsetof(itx_t, itx_node));
	avl_create(&itxs->i_async_tree, zil_aitx_compare,
	    sizeof (itx_async_node_t), offsetof(itx_async_node_t, ia_node));
	return (itxs);
}

void
zil_itx_assign(zilog_t *zilog, itx_t *itx, dmu_tx_t *tx)
{
	uint64_t txg;
	itxg_t *itxg;
	itxs_t *itxs, *clean = NULL;

	/*
	 * Ensure the data of a renamed file is committed before the rename.
	 */
	if ((itx->itx_lr.lrc_txtype & ~TX_CI) == TX_RENAME)
		zil_async_to_sync(zilog, itx->itx_oid);

	if (spa_freeze_txg(zilog->zl_spa) != UINT64_MAX)
		txg = ZILTEST_TXG;
	else
		txg = dmu_tx_get_txg(tx);

	itxg = &zilog->zl_itxg[txg & TXG_MASK];
	mutex_enter(&itxg->itxg_lock);

	itxs = itxg->itxg_itxs;
	if (itxg->itxg_txg != txg) {
		if (itxs != NULL) {
			/*
			 * The zil_clean callback hasn't got around to cleaning
			 * this itxg. Save the itxs for release below.
			 * This should be rare.
			 */
			zfs_dbgmsg("zil_itx_assign: missed itx cleanup for "
			    "txg %llu", (u_longlong_t)itxg->itxg_txg);
			clean = itxg->itxg_itxs;
		}
		itxg->itxg_txg = txg;
		itxs = itxg->itxg_itxs = zil_alloc_itxs();
	}
	if (itx->itx_sync) {
		list_insert_tail(&itxs->i_sync_list, itx);
	} else {
		avl_tree_t *t = &itxs->i_async_tree;
		uint64_t foid =
		    LR_FOID_GET_OBJ(((lr_ooo_t *)&itx->itx_lr)->lr_foid);
		itx_async_node_t *ian;
		avl_index_t where;

		ian = avl_find(t, &foid, &where);
		if (ian == NULL) {
			ian = kmem_alloc(sizeof (itx_async_node_t),
			    KM_SLEEP);
			list_create(&ian->ia_list, sizeof (itx_t),
			    offsetof(itx_t, itx_node));
			ian->ia_foid = foid;
			avl_insert(t, ian, where);
		}
		list_insert_tail(&ian->ia_list, itx);
	}

	itx->itx_lr.lrc_txg = dmu_tx_get_txg(tx);

	/*
	 * We don't want to dirty the ZIL using ZILTEST_TXG, because
	 * zil_clean() will never be called using ZILTEST_TXG. Thus, we
	 * need to be careful to always dirty the ZIL using the "real"
	 * TXG (not itxg_txg) even when the SPA is frozen.
	 */
	zilog_dirty(zilog, dmu_tx_get_txg(tx));
	mutex_exit(&itxg->itxg_lock);

	/* Release the old itxs now we've dropped the lock */
	if (clean != NULL)
		zil_itxg_clean(clean);
}

static void
zil_itxg_clean_failed(zilog_t *zilog, uint64_t synced_txg)
{
	itxg_t *fail_itxg = &zilog->zl_fail_itxg;

	ASSERT(MUTEX_HELD(&fail_itxg->itxg_lock));

	ASSERT3U(fail_itxg->itxg_txg, >, 0);
	ASSERT3U(fail_itxg->itxg_txg, <=, synced_txg);

	uint64_t next_txg = UINT64_MAX;

	itx_t *itx, *next;
	list_t *l = &fail_itxg->itxg_itxs->i_sync_list;

	next = list_head(l);
	while (next != NULL) {
		itx = next;
		next = list_next(l, itx);
		ASSERT3U(itx->itx_lr.lrc_txtype, !=, TX_COMMIT);
		if (itx->itx_lr.lrc_txg <= synced_txg) {
			list_remove(l, itx);
			zil_itx_destroy(itx);
		} else
			next_txg = MIN(next_txg, itx->itx_lr.lrc_txg);
	}

	if (next_txg < UINT64_MAX)
		fail_itxg->itxg_txg = next_txg;
	else {
		ASSERT(list_is_empty(&fail_itxg->itxg_itxs->i_sync_list));
		avl_destroy(&fail_itxg->itxg_itxs->i_async_tree);
		kmem_free(fail_itxg->itxg_itxs, sizeof (itxs_t));
		fail_itxg->itxg_itxs = NULL;
		fail_itxg->itxg_txg = 0;
	}

	ASSERT(MUTEX_HELD(&fail_itxg->itxg_lock));
}

/*
 * If there are any in-memory intent log transactions which have now been
 * synced then start up a taskq to free them. We should only do this after we
 * have written out the uberblocks (i.e. txg has been committed) so that
 * don't inadvertently clean out in-memory log records that would be required
 * by zil_commit().
 */
void
zil_clean(zilog_t *zilog, uint64_t synced_txg)
{
	itxg_t *itxg = &zilog->zl_itxg[synced_txg & TXG_MASK];
	itxg_t *fail_itxg = &zilog->zl_fail_itxg;
	itxs_t *clean_me;

	ASSERT3U(synced_txg, <, ZILTEST_TXG);

	/*
	 * If zil_fail() is currently running, we will pause here until its
	 * done. If its not (ie most of the time), we're the only one checking,
	 * once per txg sync, so we should always get the lock without fuss.
	 */
	mutex_enter(&fail_itxg->itxg_lock);
	if (fail_itxg->itxg_txg > 0 && fail_itxg->itxg_txg <= synced_txg)
		/* We've moved past the restart point, so prune any old itxs. */
		zil_itxg_clean_failed(zilog, synced_txg);
	mutex_exit(&fail_itxg->itxg_lock);

	mutex_enter(&itxg->itxg_lock);
	if (itxg->itxg_itxs == NULL || itxg->itxg_txg == ZILTEST_TXG) {
		mutex_exit(&itxg->itxg_lock);
		return;
	}
	ASSERT3U(itxg->itxg_txg, <=, synced_txg);
	ASSERT3U(itxg->itxg_txg, !=, 0);
	clean_me = itxg->itxg_itxs;
	itxg->itxg_itxs = NULL;
	itxg->itxg_txg = 0;
	mutex_exit(&itxg->itxg_lock);
	/*
	 * Preferably start a task queue to free up the old itxs but
	 * if taskq_dispatch can't allocate resources to do that then
	 * free it in-line. This should be rare. Note, using TQ_SLEEP
	 * created a bad performance problem.
	 */
	ASSERT3P(zilog->zl_dmu_pool, !=, NULL);
	ASSERT3P(zilog->zl_dmu_pool->dp_zil_clean_taskq, !=, NULL);
	taskqid_t id = taskq_dispatch(zilog->zl_dmu_pool->dp_zil_clean_taskq,
	    zil_itxg_clean, clean_me, TQ_NOSLEEP);
	if (id == TASKQID_INVALID)
		zil_itxg_clean(clean_me);
}

/*
 * This function will traverse the queue of itxs that need to be
 * committed, and move them onto the ZIL's zl_itx_commit_list.
 */
static void
zil_get_commit_list(zilog_t *zilog)
{
	uint64_t otxg, txg;
	list_t *commit_list = &zilog->zl_itx_commit_list;

	ASSERT(MUTEX_HELD(&zilog->zl_issuer_lock));

	if (spa_freeze_txg(zilog->zl_spa) != UINT64_MAX) /* ziltest support */
		otxg = ZILTEST_TXG;
	else
		otxg = spa_last_synced_txg(zilog->zl_spa) + 1;

	/*
	 * This is inherently racy, since there is nothing to prevent
	 * the last synced txg from changing. That's okay since we'll
	 * only commit things in the future.
	 */
	for (txg = otxg; txg < (otxg + TXG_CONCURRENT_STATES); txg++) {
		itxg_t *itxg = &zilog->zl_itxg[txg & TXG_MASK];

		mutex_enter(&itxg->itxg_lock);
		if (itxg->itxg_txg != txg) {
			mutex_exit(&itxg->itxg_lock);
			continue;
		}
		if (itxg->itxg_failed) {
			/*
			 * If we failed, do nothing, as we want the itxs to
			 * remain here. zil_fail() is under zl_issuer_lock,
			 * which we hold, so there's no way for us to race them.
			 */
			mutex_exit(&itxg->itxg_lock);
			return;
		}

		/*
		 * If we're adding itx records to the zl_itx_commit_list,
		 * then the zil better be dirty in this "txg". We can assert
		 * that here since we're holding the itxg_lock which will
		 * prevent spa_sync from cleaning it. Once we add the itxs
		 * to the zl_itx_commit_list we must commit it to disk even
		 * if it's unnecessary (i.e. the txg was synced).
		 */
		ASSERT(zilog_is_dirty_in_txg(zilog, txg) ||
		    spa_freeze_txg(zilog->zl_spa) != UINT64_MAX);
		list_move_tail(commit_list, &itxg->itxg_itxs->i_sync_list);

		mutex_exit(&itxg->itxg_lock);
	}
}

/*
 * Move the async itxs for a specified object to commit into sync lists.
 */
void
zil_async_to_sync(zilog_t *zilog, uint64_t foid)
{
	uint64_t otxg, txg;
	itx_async_node_t *ian;
	avl_tree_t *t;
	avl_index_t where;

	if (spa_freeze_txg(zilog->zl_spa) != UINT64_MAX) /* ziltest support */
		otxg = ZILTEST_TXG;
	else
		otxg = spa_last_synced_txg(zilog->zl_spa) + 1;

	/*
	 * This is inherently racy, since there is nothing to prevent
	 * the last synced txg from changing.
	 */
	for (txg = otxg; txg < (otxg + TXG_CONCURRENT_STATES); txg++) {
		itxg_t *itxg = &zilog->zl_itxg[txg & TXG_MASK];

		mutex_enter(&itxg->itxg_lock);
		if (itxg->itxg_txg != txg) {
			mutex_exit(&itxg->itxg_lock);
			continue;
		}

		/*
		 * If a foid is specified then find that node and append its
		 * list. Otherwise walk the tree appending all the lists
		 * to the sync list. We add to the end rather than the
		 * beginning to ensure the create has happened.
		 */
		t = &itxg->itxg_itxs->i_async_tree;
		if (foid != 0) {
			ian = avl_find(t, &foid, &where);
			if (ian != NULL) {
				list_move_tail(&itxg->itxg_itxs->i_sync_list,
				    &ian->ia_list);
			}
		} else {
			void *cookie = NULL;

			while ((ian = avl_destroy_nodes(t, &cookie)) != NULL) {
				list_move_tail(&itxg->itxg_itxs->i_sync_list,
				    &ian->ia_list);
				list_destroy(&ian->ia_list);
				kmem_free(ian, sizeof (itx_async_node_t));
			}
		}
		mutex_exit(&itxg->itxg_lock);
	}
}

/*
 * This function will prune commit itxs that are at the head of the
 * commit list (it won't prune past the first non-commit itx), and
 * either: a) attach them to the last lwb that's still pending
 * completion, or b) skip them altogether.
 *
 * This is used as a performance optimization to prevent commit itxs
 * from generating new lwbs when it's unnecessary to do so.
 */
static void
zil_prune_commit_list(zilog_t *zilog)
{
	itx_t *itx;

	ASSERT(MUTEX_HELD(&zilog->zl_issuer_lock));

	while ((itx = list_head(&zilog->zl_itx_commit_list)) != NULL) {
		lr_t *lrc = &itx->itx_lr;
		if (lrc->lrc_txtype != TX_COMMIT)
			break;

		mutex_enter(&zilog->zl_lock);

		lwb_t *last_lwb = zilog->zl_last_lwb_opened;
		if (last_lwb == NULL ||
		    last_lwb->lwb_state == LWB_STATE_FLUSH_DONE) {
			/*
			 * All of the itxs this waiter was waiting on
			 * must have already completed (or there were
			 * never any itx's for it to wait on), so it's
			 * safe to skip this waiter and mark it done.
			 */
			zil_commit_waiter_skip(itx->itx_private);
		} else {
			zil_commit_waiter_link_lwb(itx->itx_private, last_lwb);
			itx->itx_private = NULL;
		}

		mutex_exit(&zilog->zl_lock);

		list_remove(&zilog->zl_itx_commit_list, itx);
		zil_itx_destroy(itx);
	}

	IMPLY(itx != NULL, itx->itx_lr.lrc_txtype != TX_COMMIT);
}

static int
zil_commit_writer_stall(zilog_t *zilog)
{
	/*
	 * When zio_alloc_zil() fails to allocate the next lwb block on
	 * disk, we must call txg_wait_synced_flags() to ensure all of the
	 * lwbs in the zilog's zl_lwb_list are synced and then freed (in
	 * zil_sync()), such that any subsequent ZIL writer (i.e. a call
	 * to zil_process_commit_list()) will have to call zil_create(),
	 * and start a new ZIL chain.
	 *
	 * Since zil_alloc_zil() failed, the lwb that was previously
	 * issued does not have a pointer to the "next" lwb on disk.
	 * Thus, if another ZIL writer thread was to allocate the "next"
	 * on-disk lwb, that block could be leaked in the event of a
	 * crash (because the previous lwb on-disk would not point to
	 * it).
	 *
	 * We must hold the zilog's zl_issuer_lock while we do this, to
	 * ensure no new threads enter zil_process_commit_list() until
	 * all lwb's in the zl_lwb_list have been synced and freed
	 * (which is achieved via the txg_wait_synced_flags() call).
	 */
	ASSERT(MUTEX_HELD(&zilog->zl_issuer_lock));
	int error = txg_wait_synced_flags(zilog->zl_dmu_pool, 0,
	    TXG_WAIT_F_NOSUSPEND);
	if (error != 0) {
		ASSERT3S(error, ==, EAGAIN);
		return (EAGAIN);
	}
	ASSERT3P(list_tail(&zilog->zl_lwb_list), ==, NULL);
	return (0);
}

/*
 * This function will traverse the commit list, creating new lwbs as
 * needed, and committing the itxs from the commit list to these newly
 * created lwbs. Additionally, as a new lwb is created, the previous
 * lwb will be issued to the zio layer to be written to disk.
 */
static void
zil_process_commit_list(zilog_t *zilog)
{
	spa_t *spa = zilog->zl_spa;
	list_t nolwb_itxs;
	list_t nolwb_waiters;
	lwb_t *lwb;
	itx_t *itx;

	ASSERT(MUTEX_HELD(&zilog->zl_issuer_lock));

	/*
	 * Return if there's nothing to commit before we dirty the fs by
	 * calling zil_create().
	 */
	if (list_head(&zilog->zl_itx_commit_list) == NULL)
		return;

	list_create(&nolwb_itxs, sizeof (itx_t), offsetof(itx_t, itx_node));
	list_create(&nolwb_waiters, sizeof (zil_commit_waiter_t),
	    offsetof(zil_commit_waiter_t, zcw_node));

	lwb = list_tail(&zilog->zl_lwb_list);
	if (lwb == NULL) {
		lwb = zil_create(zilog);
		if (lwb == NULL) {
			/*
			 * Couldn't allocate the first block or write the
			 * header. Immediately fail the ZIL. The itxs are on the
			 * commit list, which means not much to do.
			 */
			mutex_enter(&zilog->zl_lock);
			zil_fail(zilog);
			mutex_exit(&zilog->zl_lock);
			return;
		}
	} else {
		ASSERT3S(lwb->lwb_state, !=, LWB_STATE_ISSUED);
		ASSERT3S(lwb->lwb_state, !=, LWB_STATE_WRITE_DONE);
		ASSERT3S(lwb->lwb_state, !=, LWB_STATE_FLUSH_DONE);
	}

	while ((itx = list_head(&zilog->zl_itx_commit_list)) != NULL) {
		lr_t *lrc = &itx->itx_lr;
		uint64_t txg = lrc->lrc_txg;

		ASSERT3U(txg, !=, 0);

		if (lrc->lrc_txtype == TX_COMMIT) {
			DTRACE_PROBE2(zil__process__commit__itx,
			    zilog_t *, zilog, itx_t *, itx);
		} else {
			DTRACE_PROBE2(zil__process__normal__itx,
			    zilog_t *, zilog, itx_t *, itx);
		}

		list_remove(&zilog->zl_itx_commit_list, itx);

		boolean_t synced = txg <= spa_last_synced_txg(spa);
		boolean_t frozen = txg > spa_freeze_txg(spa);

		/*
		 * If the txg of this itx has already been synced out, then
		 * we don't need to commit this itx to an lwb. This is
		 * because the data of this itx will have already been
		 * written to the main pool. This is inherently racy, and
		 * it's still ok to commit an itx whose txg has already
		 * been synced; this will result in a write that's
		 * unnecessary, but will do no harm.
		 *
		 * With that said, we always want to commit TX_COMMIT itxs
		 * to an lwb, regardless of whether or not that itx's txg
		 * has been synced out. We do this to ensure any OPENED lwb
		 * will always have at least one zil_commit_waiter_t linked
		 * to the lwb.
		 *
		 * As a counter-example, if we skipped TX_COMMIT itx's
		 * whose txg had already been synced, the following
		 * situation could occur if we happened to be racing with
		 * spa_sync:
		 *
		 * 1. We commit a non-TX_COMMIT itx to an lwb, where the
		 *    itx's txg is 10 and the last synced txg is 9.
		 * 2. spa_sync finishes syncing out txg 10.
		 * 3. We move to the next itx in the list, it's a TX_COMMIT
		 *    whose txg is 10, so we skip it rather than committing
		 *    it to the lwb used in (1).
		 *
		 * If the itx that is skipped in (3) is the last TX_COMMIT
		 * itx in the commit list, than it's possible for the lwb
		 * used in (1) to remain in the OPENED state indefinitely.
		 *
		 * To prevent the above scenario from occurring, ensuring
		 * that once an lwb is OPENED it will transition to ISSUED
		 * and eventually DONE, we always commit TX_COMMIT itx's to
		 * an lwb here, even if that itx's txg has already been
		 * synced.
		 *
		 * Finally, if the pool is frozen, we _always_ commit the
		 * itx.  The point of freezing the pool is to prevent data
		 * from being written to the main pool via spa_sync, and
		 * instead rely solely on the ZIL to persistently store the
		 * data; i.e.  when the pool is frozen, the last synced txg
		 * value can't be trusted.
		 */
		if (frozen || !synced || lrc->lrc_txtype == TX_COMMIT) {
			if (lwb != NULL) {
				lwb = zil_lwb_commit(zilog, itx, lwb);

				if (lwb == NULL)
					list_insert_tail(&nolwb_itxs, itx);
				else
					list_insert_tail(&lwb->lwb_itxs, itx);
			} else {
				if (lrc->lrc_txtype == TX_COMMIT) {
					zil_commit_waiter_link_nolwb(
					    itx->itx_private, &nolwb_waiters);
				}

				list_insert_tail(&nolwb_itxs, itx);
			}
		} else {
			ASSERT3S(lrc->lrc_txtype, !=, TX_COMMIT);
			zil_itx_destroy(itx);
		}
	}

	if (lwb == NULL) {
		/*
		 * This indicates either zio_alloc_zil() failed to allocate the
		 * "next" lwb on-disk, or the pool suspended while trying to
		 * assign that block to a tx (or, in rare cases, the pool
		 * suspended while trying to commit a TX_WRITE).
		 *
		 * If the lwb was issued, we can try to recover by stalling the
		 * ZIL write pipeline; see the comment within
		 * zil_commit_writer_stall() for more details.
		 *
		 * If the lwb was not issued, stalling is not an option. Almost
		 * certainly it will fail anyway, but if the pool was restored
		 * and it succeeded, we'd be left with an unissued lwb carrying
		 * itxs for data that is already on the main pool, and no one
		 * has responsibility for calling the waiters.
		 *
		 * Regardless, if the stall fails or would be unsafe, we have no
		 * choice but to fail the ZIL entirely.
		 */

		lwb = list_tail(&zilog->zl_lwb_list);
		ASSERT3S(lwb->lwb_state, !=, LWB_STATE_CLOSED);

		if (lwb->lwb_state != LWB_STATE_OPENED &&
		    zil_commit_writer_stall(zilog) == 0) {
			/*
			 * Additionally, we have to signal and mark the "nolwb"
			 * waiters as "done" here, since without an lwb, we
			 * can't do this via zil_lwb_flush_vdevs_done() like
			 * normal.
			 */
			zil_commit_waiter_t *zcw;
			while ((zcw = list_head(&nolwb_waiters)) != NULL) {
				list_remove(&nolwb_waiters, zcw);
				zil_commit_waiter_skip(zcw);
			}

			/*
			 * And finally, we have to destroy the itx's that
			 * couldn't be committed to an lwb; this will also call
			 * the itx's callback if one exists for the itx.
			 */
			while ((itx = list_head(&nolwb_itxs)) != NULL) {
				list_remove(&nolwb_itxs, itx);
				zil_itx_destroy(itx);
			}
		} else {
			ASSERT(spa_suspended(spa));
			ASSERT(!zil_failed(zilog));

			/*
			 * The pool suspended, so all we can do is fail the ZIL
			 * and keep the itxs alive in case it ever comes back.
			 */

			ASSERT(MUTEX_HELD(&zilog->zl_issuer_lock));
			mutex_enter(&zilog->zl_lock);

			if (lwb->lwb_state == LWB_STATE_OPENED) {
				/*
				 * The LWB was never issued, so will never get
				 * cleaned up in zil_lwb_flush_vdevs_done(). Do
				 * that now.
				 */
				ASSERT3P(lwb->lwb_buf, !=, NULL);
				ASSERT3P(lwb->lwb_tx, !=, NULL);
				ASSERT3P(lwb->lwb_root_zio, ==, NULL);
				ASSERT3P(lwb->lwb_write_zio, ==, NULL);

				zio_buf_free(lwb->lwb_buf, lwb->lwb_sz);
				dmu_tx_t *tx = lwb->lwb_tx;
				lwb->lwb_buf = NULL;
				lwb->lwb_tx = NULL;
				dmu_tx_commit(tx);
			}

			/*
			 * Attach the nolwb itxs and waiters to the lead lwb.
			 * It doesn't matter that they're not going to be
			 * issued; we just need them in a place that zil_fail()
			 * can get at them.
			 */
			list_move_tail(&lwb->lwb_itxs, &nolwb_itxs);
			list_move_tail(&lwb->lwb_waiters, &nolwb_waiters);

			zil_fail(zilog);

			mutex_exit(&zilog->zl_lock);
		}
	} else {
		ASSERT(list_is_empty(&nolwb_waiters));
		ASSERT3P(lwb, !=, NULL);
		ASSERT3S(lwb->lwb_state, !=, LWB_STATE_ISSUED);
		ASSERT3S(lwb->lwb_state, !=, LWB_STATE_WRITE_DONE);
		ASSERT3S(lwb->lwb_state, !=, LWB_STATE_FLUSH_DONE);

		/*
		 * At this point, the ZIL block pointed at by the "lwb"
		 * variable is in one of the following states: "closed"
		 * or "open".
		 *
		 * If it's "closed", then no itxs have been committed to
		 * it, so there's no point in issuing its zio (i.e. it's
		 * "empty").
		 *
		 * If it's "open", then it contains one or more itxs that
		 * eventually need to be committed to stable storage. In
		 * this case we intentionally do not issue the lwb's zio
		 * to disk yet, and instead rely on one of the following
		 * two mechanisms for issuing the zio:
		 *
		 * 1. Ideally, there will be more ZIL activity occurring
		 * on the system, such that this function will be
		 * immediately called again (not necessarily by the same
		 * thread) and this lwb's zio will be issued via
		 * zil_lwb_commit(). This way, the lwb is guaranteed to
		 * be "full" when it is issued to disk, and we'll make
		 * use of the lwb's size the best we can.
		 *
		 * 2. If there isn't sufficient ZIL activity occurring on
		 * the system, such that this lwb's zio isn't issued via
		 * zil_lwb_commit(), zil_commit_waiter() will issue the
		 * lwb's zio. If this occurs, the lwb is not guaranteed
		 * to be "full" by the time its zio is issued, and means
		 * the size of the lwb was "too large" given the amount
		 * of ZIL activity occurring on the system at that time.
		 *
		 * We do this for a couple of reasons:
		 *
		 * 1. To try and reduce the number of IOPs needed to
		 * write the same number of itxs. If an lwb has space
		 * available in its buffer for more itxs, and more itxs
		 * will be committed relatively soon (relative to the
		 * latency of performing a write), then it's beneficial
		 * to wait for these "next" itxs. This way, more itxs
		 * can be committed to stable storage with fewer writes.
		 *
		 * 2. To try and use the largest lwb block size that the
		 * incoming rate of itxs can support. Again, this is to
		 * try and pack as many itxs into as few lwbs as
		 * possible, without significantly impacting the latency
		 * of each individual itx.
		 */
	}
}

/*
 * This function is responsible for ensuring the passed in commit waiter
 * (and associated commit itx) is committed to an lwb. If the waiter is
 * not already committed to an lwb, all itxs in the zilog's queue of
 * itxs will be processed. The assumption is the passed in waiter's
 * commit itx will found in the queue just like the other non-commit
 * itxs, such that when the entire queue is processed, the waiter will
 * have been committed to an lwb.
 *
 * The lwb associated with the passed in waiter is not guaranteed to
 * have been issued by the time this function completes. If the lwb is
 * not issued, we rely on future calls to zil_commit_writer() to issue
 * the lwb, or the timeout mechanism found in zil_commit_waiter().
 */
static void
zil_commit_writer(zilog_t *zilog, zil_commit_waiter_t *zcw)
{
	ASSERT(!MUTEX_HELD(&zilog->zl_lock));
	ASSERT(spa_writeable(zilog->zl_spa));

	mutex_enter(&zilog->zl_issuer_lock);

	/*
	 * This is first time we've taken a ZIL-wide lock since zil_commit()
	 * was called. zil_fail() may have completed since we first checked the
	 * fail state in zil_commit(), so we need to recheck it now that we
	 * have the issuer lock. If its failed, we eject right now.
	 */
	if (zil_failed(zilog))
		goto out;

	if (zcw->zcw_lwb != NULL || zcw->zcw_done) {
		/*
		 * It's possible that, while we were waiting to acquire
		 * the "zl_issuer_lock", another thread committed this
		 * waiter to an lwb. If that occurs, we bail out early,
		 * without processing any of the zilog's queue of itxs.
		 *
		 * On certain workloads and system configurations, the
		 * "zl_issuer_lock" can become highly contended. In an
		 * attempt to reduce this contention, we immediately drop
		 * the lock if the waiter has already been processed.
		 *
		 * We've measured this optimization to reduce CPU spent
		 * contending on this lock by up to 5%, using a system
		 * with 32 CPUs, low latency storage (~50 usec writes),
		 * and 1024 threads performing sync writes.
		 */
		goto out;
	}

	ZIL_STAT_BUMP(zil_commit_writer_count);

	zil_get_commit_list(zilog);
	zil_prune_commit_list(zilog);
	zil_process_commit_list(zilog);

out:
	mutex_exit(&zilog->zl_issuer_lock);
	ASSERT(!MUTEX_HELD(&zilog->zl_lock));
}

static void
zil_commit_waiter_timeout(zilog_t *zilog, zil_commit_waiter_t *zcw)
{
	ASSERT(!MUTEX_HELD(&zilog->zl_issuer_lock));
	ASSERT(MUTEX_HELD(&zcw->zcw_lock));
	ASSERT3B(zcw->zcw_done, ==, B_FALSE);

	lwb_t *lwb = zcw->zcw_lwb;
	ASSERT3P(lwb, !=, NULL);
	ASSERT3S(lwb->lwb_state, !=, LWB_STATE_CLOSED);

	/*
	 * If the lwb has already been issued by another thread, we can
	 * immediately return since there's no work to be done (the
	 * point of this function is to issue the lwb). Additionally, we
	 * do this prior to acquiring the zl_issuer_lock, to avoid
	 * acquiring it when it's not necessary to do so.
	 */
	if (lwb->lwb_state == LWB_STATE_ISSUED ||
	    lwb->lwb_state == LWB_STATE_WRITE_DONE ||
	    lwb->lwb_state == LWB_STATE_FLUSH_DONE)
		return;

	/*
	 * In order to call zil_lwb_write_issue() we must hold the
	 * zilog's "zl_issuer_lock". We can't simply acquire that lock,
	 * since we're already holding the commit waiter's "zcw_lock",
	 * and those two locks are acquired in the opposite order
	 * elsewhere.
	 */
	mutex_exit(&zcw->zcw_lock);
	mutex_enter(&zilog->zl_issuer_lock);
	mutex_enter(&zcw->zcw_lock);

	/*
	 * Since we just dropped and re-acquired the commit waiter's
	 * lock, we have to re-check to see if the waiter was marked
	 * "done" during that process. If the waiter was marked "done",
	 * the "lwb" pointer is no longer valid (it can be free'd after
	 * the waiter is marked "done"), so without this check we could
	 * wind up with a use-after-free error below.
	 */
	if (zcw->zcw_done)
		goto out;

	ASSERT3P(lwb, ==, zcw->zcw_lwb);

	/*
	 * We've already checked this above, but since we hadn't acquired
	 * the zilog's zl_issuer_lock, we have to perform this check a
	 * second time while holding the lock.
	 *
	 * We don't need to hold the zl_lock since the lwb cannot transition
	 * from OPENED to ISSUED while we hold the zl_issuer_lock. The lwb
	 * _can_ transition from ISSUED to DONE, but it's OK to race with
	 * that transition since we treat the lwb the same, whether it's in
	 * the ISSUED or DONE states.
	 *
	 * The important thing, is we treat the lwb differently depending on
	 * if it's ISSUED or OPENED, and block any other threads that might
	 * attempt to issue this lwb. For that reason we hold the
	 * zl_issuer_lock when checking the lwb_state; we must not call
	 * zil_lwb_write_issue() if the lwb had already been issued.
	 *
	 * See the comment above the lwb_state_t structure definition for
	 * more details on the lwb states, and locking requirements.
	 */
	if (lwb->lwb_state == LWB_STATE_ISSUED ||
	    lwb->lwb_state == LWB_STATE_WRITE_DONE ||
	    lwb->lwb_state == LWB_STATE_FLUSH_DONE)
		goto out;

	ASSERT3S(lwb->lwb_state, ==, LWB_STATE_OPENED);

	/*
	 * The ZIL may have failed between zil_commit_writer() releasing
	 * zl_issuer_lock and us taking it. If it has failed, we must not issue
	 * any IO right now.
	 */
	if (zil_failed(zilog))
		goto out;

	/*
	 * As described in the comments above zil_commit_waiter() and
	 * zil_process_commit_list(), we need to issue this lwb's zio
	 * since we've reached the commit waiter's timeout and it still
	 * hasn't been issued.
	 */
	lwb_t *nlwb = zil_lwb_write_issue(zilog, lwb);

	IMPLY(nlwb != NULL, lwb->lwb_state != LWB_STATE_OPENED);

	/*
	 * Since the lwb's zio hadn't been issued by the time this thread
	 * reached its timeout, we reset the zilog's "zl_cur_used" field
	 * to influence the zil block size selection algorithm.
	 *
	 * By having to issue the lwb's zio here, it means the size of the
	 * lwb was too large, given the incoming throughput of itxs.  By
	 * setting "zl_cur_used" to zero, we communicate this fact to the
	 * block size selection algorithm, so it can take this information
	 * into account, and potentially select a smaller size for the
	 * next lwb block that is allocated.
	 */
	zilog->zl_cur_used = 0;

	if (nlwb == NULL) {
		/*
		 * When zil_lwb_write_issue() returns NULL, this
		 * indicates either zio_alloc_zil() failed to allocate
		 * the "next" lwb on-disk, or the pool suspended while
		 * trying to assign that block to a tx.
		 *
		 * If the lwb was issued, we can try to recover by
		 * stalling the ZIL write pipeline. If it was not, or
		 * the stall also fails, we must fail the ZIL instead.
		 * See the comments in zil_process_commit_list() and
		 * zil_commit_writer_stall() for more details.
		 *
		 * We must drop the commit waiter's lock prior to
		 * calling zil_commit_writer_stall() or else we can wind
		 * up with the following deadlock:
		 *
		 * - This thread is waiting for the txg to sync while
		 *   holding the waiter's lock; txg_wait_synced() is
		 *   used within txg_commit_writer_stall().
		 *
		 * - The txg can't sync because it is waiting for this
		 *   lwb's zio callback to call dmu_tx_commit().
		 *
		 * - The lwb's zio callback can't call dmu_tx_commit()
		 *   because it's blocked trying to acquire the waiter's
		 *   lock, which occurs prior to calling dmu_tx_commit()
		 */

		lwb = list_tail(&zilog->zl_lwb_list);
		ASSERT3S(lwb->lwb_state, !=, LWB_STATE_CLOSED);

		mutex_exit(&zcw->zcw_lock);

		if (lwb->lwb_state == LWB_STATE_OPENED ||
		    zil_commit_writer_stall(zilog) != 0) {
			ASSERT(spa_suspended(zilog->zl_spa));
			ASSERT(!zil_failed(zilog));

			ASSERT(MUTEX_HELD(&zilog->zl_issuer_lock));
			mutex_enter(&zilog->zl_lock);

			if (lwb->lwb_state == LWB_STATE_OPENED) {
				/*
				 * The LWB was never issued, so will never get
				 * cleaned up in zil_lwb_flush_vdevs_done(). Do
				 * that now.
				 */
				ASSERT3P(lwb->lwb_buf, !=, NULL);
				ASSERT3P(lwb->lwb_tx, !=, NULL);
				ASSERT3P(lwb->lwb_root_zio, ==, NULL);
				ASSERT3P(lwb->lwb_write_zio, ==, NULL);

				zio_buf_free(lwb->lwb_buf, lwb->lwb_sz);
				dmu_tx_t *tx = lwb->lwb_tx;
				lwb->lwb_buf = NULL;
				lwb->lwb_tx = NULL;
				dmu_tx_commit(tx);
			}

			zil_fail(zilog);

			mutex_exit(&zilog->zl_lock);
		}

		mutex_enter(&zcw->zcw_lock);
	}

out:
	mutex_exit(&zilog->zl_issuer_lock);
	ASSERT(MUTEX_HELD(&zcw->zcw_lock));
	ASSERT(!MUTEX_HELD(&zilog->zl_lock));
}

/*
 * This function is responsible for performing the following two tasks:
 *
 * 1. its primary responsibility is to block until the given "commit
 *    waiter" is considered "done".
 *
 * 2. its secondary responsibility is to issue the zio for the lwb that
 *    the given "commit waiter" is waiting on, if this function has
 *    waited "long enough" and the lwb is still in the "open" state.
 *
 * Given a sufficient amount of itxs being generated and written using
 * the ZIL, the lwb's zio will be issued via the zil_lwb_commit()
 * function. If this does not occur, this secondary responsibility will
 * ensure the lwb is issued even if there is not other synchronous
 * activity on the system.
 *
 * For more details, see zil_process_commit_list(); more specifically,
 * the comment at the bottom of that function.
 */
static void
zil_commit_waiter(zilog_t *zilog, zil_commit_waiter_t *zcw)
{
	ASSERT(!MUTEX_HELD(&zilog->zl_lock));
	ASSERT(!MUTEX_HELD(&zilog->zl_issuer_lock));
	ASSERT(spa_writeable(zilog->zl_spa));

	mutex_enter(&zcw->zcw_lock);

	if (zcw->zcw_done) {
		/*
		 * The IO may have been issued and completed, or the ZIL may
		 * have failed (signaling all outstanding waiters) since we
		 * dropped zl_issuer_lock at the end of zil_commit_writer().
		 * If so, there's no need to wait; our work here is done.
		 */
		mutex_exit(&zcw->zcw_lock);
		return;
	}

	/*
	 * Usually, the waiter will have a non-NULL lwb field here, but it's
	 * possible for it to be NULL as a result of zil_commit() racing with
	 * spa_sync().
	 *
	 * When zil_clean() is called, it's possible for the itxg list (which
	 * may be cleaned via a taskq) to contain commit itxs. When this
	 * occurs, the commit waiters linked off of these commit itxs will not
	 * be committed to an lwb.  Additionally, these commit waiters will not
	 * be marked done until zil_commit_waiter_skip() is called via
	 * zil_itxg_clean().
	 *
	 * Thus, it's possible for this commit waiter (i.e. the "zcw" variable)
	 * to be found in this "in between" state; where it's "zcw_lwb" field
	 * is NULL, and it hasn't yet been skipped, so it's "zcw_done" field is
	 * still B_FALSE.
	 */
	lwb_t *lwb = zcw->zcw_lwb;
	IMPLY(lwb != NULL, lwb->lwb_state != LWB_STATE_CLOSED);

	if (lwb != NULL && lwb->lwb_state == LWB_STATE_OPENED) {
		/*
		 * If the lwb hasn't been issued yet, then we need to wait with
		 * a timeout, in case this function needs to issue the lwb
		 * after the timeout is reached; responsibility (2) from the
		 * comment above this function.
		 *
		 * The timeout is scaled based on the lwb latency to avoid
		 * significantly impacting the latency of each individual itx.
		 * For more details, see the comment at the bottom of the
		 * zil_process_commit_list() function.
		 */
		int pct = MAX(zfs_commit_timeout_pct, 1);
		hrtime_t sleep = (zilog->zl_last_lwb_latency * pct) / 100;
		hrtime_t wakeup = gethrtime() + sleep;

		int rc = cv_timedwait_hires(&zcw->zcw_cv,
		    &zcw->zcw_lock, wakeup, USEC2NSEC(1),
		    CALLOUT_FLAG_ABSOLUTE);

		if (!zcw->zcw_done) {
			ASSERT3U(rc, ==, -1);
			/*
			 * The wait timed out and the lwb hasn't been issued,
			 * so we have to do that now.
			 */
			zil_commit_waiter_timeout(zilog, zcw);

			ASSERT(MUTEX_HELD(&zcw->zcw_lock));
		}
	}

	if (!zcw->zcw_done) {
		ASSERT(MUTEX_HELD(&zcw->zcw_lock));

		/*
		 * If we're here, then the lwb has been issued or the ZIL has
		 * failed or is failing. In all cases, there's nothing else for
		 * us to do except wait to be signaled that the data is on disk,
		 * either when the lwb is flushed, the txg of the original op
		 * completes, or the ZIL fails.
		 */
		cv_wait(&zcw->zcw_cv, &zcw->zcw_lock);
	}

	ASSERT(zcw->zcw_done);

	mutex_exit(&zcw->zcw_lock);
}

static zil_commit_waiter_t *
zil_alloc_commit_waiter(void)
{
	zil_commit_waiter_t *zcw = kmem_cache_alloc(zil_zcw_cache, KM_SLEEP);

	cv_init(&zcw->zcw_cv, NULL, CV_DEFAULT, NULL);
	mutex_init(&zcw->zcw_lock, NULL, MUTEX_DEFAULT, NULL);
	list_link_init(&zcw->zcw_node);
	zcw->zcw_lwb = NULL;
	zcw->zcw_done = B_FALSE;
	zcw->zcw_zio_error = 0;

	return (zcw);
}

static void
zil_free_commit_waiter(zil_commit_waiter_t *zcw)
{
	ASSERT(!list_link_active(&zcw->zcw_node));
	ASSERT3P(zcw->zcw_lwb, ==, NULL);
	ASSERT3B(zcw->zcw_done, ==, B_TRUE);
	mutex_destroy(&zcw->zcw_lock);
	cv_destroy(&zcw->zcw_cv);
	kmem_cache_free(zil_zcw_cache, zcw);
}

/*
 * This function is used to create a TX_COMMIT itx and assign it. This
 * way, it will be linked into the ZIL's list of synchronous itxs, and
 * then later committed to an lwb (or skipped) when
 * zil_process_commit_list() is called.
 */
static int
zil_commit_itx_assign(zilog_t *zilog, zil_commit_waiter_t *zcw)
{
	dmu_tx_t *tx = dmu_tx_create(zilog->zl_os);
	int error = 0;

	error = dmu_tx_assign(tx, DMU_TX_ASSIGN_WAIT | DMU_TX_ASSIGN_NOSUSPEND);
	if (error != 0) {
		ASSERT3S(error, ==, EAGAIN);
		dmu_tx_abort(tx);
		return (EAGAIN);
	}

	itx_t *itx = zil_itx_create(TX_COMMIT, sizeof (lr_t));
	itx->itx_sync = B_TRUE;
	itx->itx_private = zcw;

	zil_itx_assign(zilog, itx, tx);

	dmu_tx_commit(tx);

	return (0);
}

/*
 * This is a helper for when zil_commit() has to fall back to a full txg sync.
 * Callers of zil_commit() are expecting it to block or fail according to the
 * failure mode, so this is one of the few places where we honor that. It will
 * return EIO on suspend and failmode=continue so we don't have to repeat that
 * conversion multiple times at the callers.
 */
static int
zil_commit_fallback_sync(zilog_t *zilog)
{
	int err = 0;
	const txg_wait_flag_t flags =
	    spa_get_failmode(zilog->zl_spa) == ZIO_FAILURE_MODE_CONTINUE ?
	    TXG_WAIT_F_NOSUSPEND : TXG_WAIT_F_NONE;

	err = txg_wait_synced_flags(zilog->zl_dmu_pool, 0, flags);
	if (flags & TXG_WAIT_F_NOSUSPEND && err == EAGAIN)
		err = SET_ERROR(EIO);

	return (err);
}

/*
 * Commit ZFS Intent Log transactions (itxs) to stable storage.
 *
 * When writing ZIL transactions to the on-disk representation of the
 * ZIL, the itxs are committed to a Log Write Block (lwb). Multiple
 * itxs can be committed to a single lwb. Once a lwb is written and
 * committed to stable storage (i.e. the lwb is written, and vdevs have
 * been flushed), each itx that was committed to that lwb is also
 * considered to be committed to stable storage.
 *
 * When an itx is committed to an lwb, the log record (lr_t) contained
 * by the itx is copied into the lwb's zio buffer, and once this buffer
 * is written to disk, it becomes an on-disk ZIL block.
 *
 * As itxs are generated, they're inserted into the ZIL's queue of
 * uncommitted itxs. The semantics of zil_commit() are such that it will
 * block until all itxs that were in the queue when it was called, are
 * committed to stable storage.
 *
 * If "foid" is zero, this means all "synchronous" and "asynchronous"
 * itxs, for all objects in the dataset, will be committed to stable
 * storage prior to zil_commit() returning. If "foid" is non-zero, all
 * "synchronous" itxs for all objects, but only "asynchronous" itxs
 * that correspond to the foid passed in, will be committed to stable
 * storage prior to zil_commit() returning.
 *
 * Generally speaking, when zil_commit() is called, the consumer doesn't
 * actually care about _all_ of the uncommitted itxs. Instead, they're
 * simply trying to waiting for a specific itx to be committed to disk,
 * but the interface(s) for interacting with the ZIL don't allow such
 * fine-grained communication. A better interface would allow a consumer
 * to create and assign an itx, and then pass a reference to this itx to
 * zil_commit(); such that zil_commit() would return as soon as that
 * specific itx was committed to disk (instead of waiting for _all_
 * itxs to be committed).
 *
 * When a thread calls zil_commit() a special "commit itx" will be
 * generated, along with a corresponding "waiter" for this commit itx.
 * zil_commit() will wait on this waiter's CV, such that when the waiter
 * is marked done, and signaled, zil_commit() will return.
 *
 * This commit itx is inserted into the queue of uncommitted itxs. This
 * provides an easy mechanism for determining which itxs were in the
 * queue prior to zil_commit() having been called, and which itxs were
 * added after zil_commit() was called.
 *
 * The commit it is special; it doesn't have any on-disk representation.
 * When a commit itx is "committed" to an lwb, the waiter associated
 * with it is linked onto the lwb's list of waiters. Then, when that lwb
 * completes, each waiter on the lwb's list is marked done and signaled
 * -- allowing the thread waiting on the waiter to return from zil_commit().
 *
 * It's important to point out a few critical factors that allow us
 * to make use of the commit itxs, commit waiters, per-lwb lists of
 * commit waiters, and zio completion callbacks like we're doing:
 *
 *   1. The list of waiters for each lwb is traversed, and each commit
 *      waiter is marked "done" and signaled, in the zio completion
 *      callback of the lwb's zio[*].
 *
 *      * Actually, the waiters are signaled in the zio completion
 *        callback of the root zio for the DKIOCFLUSHWRITECACHE commands
 *        that are sent to the vdevs upon completion of the lwb zio.
 *
 *   2. When the itxs are inserted into the ZIL's queue of uncommitted
 *      itxs, the order in which they are inserted is preserved[*]; as
 *      itxs are added to the queue, they are added to the tail of
 *      in-memory linked lists.
 *
 *      When committing the itxs to lwbs (to be written to disk), they
 *      are committed in the same order in which the itxs were added to
 *      the uncommitted queue's linked list(s); i.e. the linked list of
 *      itxs to commit is traversed from head to tail, and each itx is
 *      committed to an lwb in that order.
 *
 *      * To clarify:
 *
 *        - the order of "sync" itxs is preserved w.r.t. other
 *          "sync" itxs, regardless of the corresponding objects.
 *        - the order of "async" itxs is preserved w.r.t. other
 *          "async" itxs corresponding to the same object.
 *        - the order of "async" itxs is *not* preserved w.r.t. other
 *          "async" itxs corresponding to different objects.
 *        - the order of "sync" itxs w.r.t. "async" itxs (or vice
 *          versa) is *not* preserved, even for itxs that correspond
 *          to the same object.
 *
 *      For more details, see: zil_itx_assign(), zil_async_to_sync(),
 *      zil_get_commit_list(), and zil_process_commit_list().
 *
 *   3. The lwbs represent a linked list of blocks on disk. Thus, any
 *      lwb cannot be considered committed to stable storage, until its
 *      "previous" lwb is also committed to stable storage. This fact,
 *      coupled with the fact described above, means that itxs are
 *      committed in (roughly) the order in which they were generated.
 *      This is essential because itxs are dependent on prior itxs.
 *      Thus, we *must not* deem an itx as being committed to stable
 *      storage, until *all* prior itxs have also been committed to
 *      stable storage.
 *
 *      To enforce this ordering of lwb zio's, while still leveraging as
 *      much of the underlying storage performance as possible, we rely
 *      on two fundamental concepts:
 *
 *          1. The creation and issuance of lwb zio's is protected by
 *             the zilog's "zl_issuer_lock", which ensures only a single
 *             thread is creating and/or issuing lwb's at a time
 *          2. The "previous" lwb is a child of the "current" lwb
 *             (leveraging the zio parent-child dependency graph)
 *
 *      By relying on this parent-child zio relationship, we can have
 *      many lwb zio's concurrently issued to the underlying storage,
 *      but the order in which they complete will be the same order in
 *      which they were created.
 */
int
zil_commit(zilog_t *zilog, uint64_t foid)
{
	/*
	 * We should never attempt to call zil_commit on a snapshot for
	 * a couple of reasons:
	 *
	 * 1. A snapshot may never be modified, thus it cannot have any
	 *    in-flight itxs that would have modified the dataset.
	 *
	 * 2. By design, when zil_commit() is called, a commit itx will
	 *    be assigned to this zilog; as a result, the zilog will be
	 *    dirtied. We must not dirty the zilog of a snapshot; there's
	 *    checks in the code that enforce this invariant, and will
	 *    cause a panic if it's not upheld.
	 */
	ASSERT3B(dmu_objset_is_snapshot(zilog->zl_os), ==, B_FALSE);

	if (zilog->zl_sync == ZFS_SYNC_DISABLED)
		return (0);

	if (!spa_writeable(zilog->zl_spa)) {
		/*
		 * If the SPA is not writable, there should never be any
		 * pending itxs waiting to be committed to disk. If that
		 * weren't true, we'd skip writing those itxs out, and
		 * would break the semantics of zil_commit(); thus, we're
		 * verifying that truth before we return to the caller.
		 */
		ASSERT(list_is_empty(&zilog->zl_lwb_list));
		ASSERT3P(zilog->zl_last_lwb_opened, ==, NULL);
		for (int i = 0; i < TXG_SIZE; i++)
			ASSERT3P(zilog->zl_itxg[i].itxg_itxs, ==, NULL);
		return (SET_ERROR(EROFS));
	}

	/*
	 * If the objset is being forced to exit, there's nothing more to do.
	 */
	if (dmu_objset_exiting(zilog->zl_os))
		return (0);

	/*
	 * If the ZIL has failed or is suspended, we don't want to dirty it by
	 * calling zil_commit_itx_assign() below, nor can we write out lwbs
	 * like would be done in zil_commit_write(). Thus, we simply rely on
	 * txg_wait_synced() to maintain the necessary semantics, and avoid
	 * calling those functions altogether.
	 *
	 * Note that we are checking failure without a lock to avoid
	 * contention, so another thread might be failing or unfailing the ZIL
	 * right now. If its going from failed to live, that's ok, as we'll end
	 * up in zil_commit_fallback_sync() here and do a normal txg sync wait,
	 * which is acceptable at this moment. If we're going from live to
	 * failed then we may continue into zil_commit_impl() here. We'll
	 * recheck the fail state further down as we acquire locks to make sure
	 * it really still isn't failed, and if it becomes so, eject before
	 * issuing IO.
	 */
	if (zil_failed(zilog) || zilog->zl_suspend > 0)
		return (zil_commit_fallback_sync(zilog));

	return (zil_commit_impl(zilog, foid));
}

int
zil_commit_impl(zilog_t *zilog, uint64_t foid)
{
	ASSERT0(zil_failed(zilog) || zilog->zl_suspend > 0);

	ZIL_STAT_BUMP(zil_commit_count);

	/*
	 * Move the "async" itxs for the specified foid to the "sync"
	 * queues, such that they will be later committed (or skipped)
	 * to an lwb when zil_process_commit_list() is called.
	 *
	 * Since these "async" itxs must be committed prior to this
	 * call to zil_commit returning, we must perform this operation
	 * before we call zil_commit_itx_assign().
	 */
	zil_async_to_sync(zilog, foid);

	/*
	 * We allocate a new "waiter" structure which will initially be
	 * linked to the commit itx using the itx's "itx_private" field.
	 * Since the commit itx doesn't represent any on-disk state,
	 * when it's committed to an lwb, rather than copying the its
	 * lr_t into the lwb's buffer, the commit itx's "waiter" will be
	 * added to the lwb's list of waiters. Then, when the lwb is
	 * committed to stable storage, each waiter in the lwb's list of
	 * waiters will be marked "done", and signalled.
	 *
	 * We must create the waiter and assign the commit itx prior to
	 * calling zil_commit_writer(), or else our specific commit itx
	 * is not guaranteed to be committed to an lwb prior to calling
	 * zil_commit_waiter().
	 */
	zil_commit_waiter_t *zcw = zil_alloc_commit_waiter();

	if (zil_commit_itx_assign(zilog, zcw) != 0) {
		/* Pool suspended while committing TX_COMMIT. */
		zil_free_commit_waiter(zcw);
		return (SET_ERROR(EIO));
	}

	zil_commit_writer(zilog, zcw);
	zil_commit_waiter(zilog, zcw);

	ASSERT(zcw->zcw_done);

	if (zcw->zcw_zio_error == 0) {
		zil_free_commit_waiter(zcw);
		return (0);
	}


	ASSERT(zil_failed(zilog));

	/*
	 * Ideally, we would just return to zil_commit() here and do the
	 * call to zil_commit_fallback_sync() from one place, but this dtrace
	 * probe has the live zcw as an argument, so we have to call it before
	 * we free it.
	 */
	DTRACE_PROBE2(zil__commit__io__error,
	    zilog_t *, zilog, zil_commit_waiter_t *, zcw);

	int err = zil_commit_fallback_sync(zilog);

	zil_free_commit_waiter(zcw);

	return (err);
}

/*
 * Called in syncing context to free committed log blocks and update log header.
 */
void
zil_sync(zilog_t *zilog, dmu_tx_t *tx)
{
	zil_header_t *zh = zil_header_in_syncing_context(zilog);
	uint64_t txg = dmu_tx_get_txg(tx);
	spa_t *spa = zilog->zl_spa;
	uint64_t *replayed_seq = &zilog->zl_replayed_seq[txg & TXG_MASK];
	lwb_t *lwb;

	/*
	 * We don't zero out zl_destroy_txg, so make sure we don't try
	 * to destroy it twice.
	 */
	if (spa_sync_pass(spa) != 1)
		return;

	mutex_enter(&zilog->zl_lock);

	ASSERT(zilog->zl_stop_sync == 0);

	if (*replayed_seq != 0) {
		ASSERT(zh->zh_replay_seq < *replayed_seq);
		zh->zh_replay_seq = *replayed_seq;
		*replayed_seq = 0;
	}

	if (zil_failed(zilog)) {
		if (zilog->zl_unfail_txg <= txg) {
			/*
			 * Everything in the failed ZIL is now on the main
			 * pool, so we can return it to service.
			 */

			/*
			 * The live txgs should be either empty or ahead of the
			 * unfail txg. The fail txg should be empty.
			 */
			for (int i = 0; i < TXG_SIZE; i++) {
				itxg_t *itxg = &zilog->zl_itxg[i];
				mutex_enter(&itxg->itxg_lock);

				ASSERT(zilog->zl_itxg[i].itxg_txg == 0 ||
				    zilog->zl_itxg[i].itxg_txg >=
				    zilog->zl_unfail_txg);

				itxg->itxg_failed = B_FALSE;
				mutex_exit(&itxg->itxg_lock);
			}

			zilog->zl_unfail_txg = 0;

			char ds_name[ZFS_MAX_DATASET_NAME_LEN];
			dsl_dataset_name(dmu_objset_ds(zilog->zl_os), ds_name);
			cmn_err(CE_WARN,
			    "ZIL resumed on pool '%s' dataset '%s': txg=%llu",
			    spa_name(zilog->zl_spa), ds_name, txg);
		} else {
			/* Can't unfail yet; re-dirty for next txg. */
			dsl_pool_t *dp = zilog->zl_dmu_pool;
			dsl_dataset_t *ds = dmu_objset_ds(zilog->zl_os);
			dmu_tx_t *next_tx = dmu_tx_create_assigned(dp, txg + 1);
			dsl_dataset_dirty(ds, next_tx);
			dmu_tx_commit(next_tx);
		}
	}

	if (zilog->zl_destroy_txg == txg) {
		blkptr_t blk = zh->zh_log;

		ASSERT(list_head(&zilog->zl_lwb_list) == NULL);

		bzero(zh, sizeof (zil_header_t));
		bzero(zilog->zl_replayed_seq, sizeof (zilog->zl_replayed_seq));

		if (zilog->zl_keep_first) {
			/*
			 * If this block was part of log chain that couldn't
			 * be claimed because a device was missing during
			 * zil_claim(), but that device later returns,
			 * then this block could erroneously appear valid.
			 * To guard against this, assign a new GUID to the new
			 * log chain so it doesn't matter what blk points to.
			 */
			zil_init_log_chain(zilog, &blk);
			zh->zh_log = blk;
		}
	}

	while ((lwb = list_head(&zilog->zl_lwb_list)) != NULL) {
		zh->zh_log = lwb->lwb_blk;
		if (lwb->lwb_buf != NULL || lwb->lwb_max_txg > txg)
			break;
		list_remove(&zilog->zl_lwb_list, lwb);
		zio_free(spa, txg, &lwb->lwb_blk);
		zil_free_lwb(zilog, lwb);

		/*
		 * If we don't have anything left in the lwb list then
		 * we've had an allocation failure and we need to zero
		 * out the zil_header blkptr so that we don't end
		 * up freeing the same block twice.
		 */
		if (list_head(&zilog->zl_lwb_list) == NULL)
			BP_ZERO(&zh->zh_log);
	}

	/*
	 * Remove fastwrite on any blocks that have been pre-allocated for
	 * the next commit. This prevents fastwrite counter pollution by
	 * unused, long-lived LWBs.
	 */
	for (; lwb != NULL; lwb = list_next(&zilog->zl_lwb_list, lwb)) {
		if (lwb->lwb_fastwrite && !lwb->lwb_write_zio) {
			metaslab_fastwrite_unmark(zilog->zl_spa, &lwb->lwb_blk);
			lwb->lwb_fastwrite = 0;
		}
	}

	mutex_exit(&zilog->zl_lock);
}

static int
zil_lwb_cons(void *vbuf, void *unused, int kmflag)
{
	(void) unused, (void) kmflag;
	lwb_t *lwb = vbuf;
	list_create(&lwb->lwb_itxs, sizeof (itx_t), offsetof(itx_t, itx_node));
	list_create(&lwb->lwb_waiters, sizeof (zil_commit_waiter_t),
	    offsetof(zil_commit_waiter_t, zcw_node));
	avl_create(&lwb->lwb_vdev_tree, zio_vdev_trace_compare,
	    sizeof (zio_vdev_trace_t), offsetof(zio_vdev_trace_t, zvt_node));
	return (0);
}

static void
zil_lwb_dest(void *vbuf, void *unused)
{
	(void) unused;
	lwb_t *lwb = vbuf;
	avl_destroy(&lwb->lwb_vdev_tree);
	list_destroy(&lwb->lwb_waiters);
	list_destroy(&lwb->lwb_itxs);
}

void
zil_init(void)
{
	zil_lwb_cache = kmem_cache_create("zil_lwb_cache",
	    sizeof (lwb_t), 0, zil_lwb_cons, zil_lwb_dest, NULL, NULL, NULL, 0);

	zil_zcw_cache = kmem_cache_create("zil_zcw_cache",
	    sizeof (zil_commit_waiter_t), 0, NULL, NULL, NULL, NULL, NULL, 0);

	zil_ksp = kstat_create("zfs", 0, "zil", "misc",
	    KSTAT_TYPE_NAMED, sizeof (zil_stats) / sizeof (kstat_named_t),
	    KSTAT_FLAG_VIRTUAL);

	if (zil_ksp != NULL) {
		zil_ksp->ks_data = &zil_stats;
		kstat_install(zil_ksp);
	}
}

void
zil_fini(void)
{
	/*
	 * XXX If the pool crashed and there is still outstanding IO, there
	 *     will still be allocated lwbs on zil_lwb_cache. On Linux this will
	 *     trip a BUG() in the underlyng __kmem_cache_shutdown. There's
	 *     really nothing we can do here unless we decouple lwbs from their
	 *     zios, either by walking all lwb zios and clearing their
	 *     io_private members under lock, and then checking for that in the
	 *     done callbacks, or by making their io_private members an index
	 *     into some global lwb lookup. These are both substantial
	 *     undertakings, so for now the guidance is simply to not unload the
	 *     module after pool failure and forced export -- robn, 2023-05-08
	 */

	kmem_cache_destroy(zil_zcw_cache);
	kmem_cache_destroy(zil_lwb_cache);

	if (zil_ksp != NULL) {
		kstat_delete(zil_ksp);
		zil_ksp = NULL;
	}
}

void
zil_set_sync(zilog_t *zilog, uint64_t sync)
{
	zilog->zl_sync = sync;
}

void
zil_set_logbias(zilog_t *zilog, uint64_t logbias)
{
	zilog->zl_logbias = logbias;
}

zilog_t *
zil_alloc(objset_t *os, zil_header_t *zh_phys)
{
	zilog_t *zilog;

	zilog = kmem_zalloc(sizeof (zilog_t), KM_SLEEP);

	zilog->zl_header = zh_phys;
	zilog->zl_os = os;
	zilog->zl_spa = dmu_objset_spa(os);
	zilog->zl_dmu_pool = dmu_objset_pool(os);
	zilog->zl_destroy_txg = TXG_INITIAL - 1;
	zilog->zl_logbias = dmu_objset_logbias(os);
	zilog->zl_sync = dmu_objset_syncprop(os);
	zilog->zl_dirty_max_txg = 0;
	zilog->zl_last_lwb_opened = NULL;
	zilog->zl_last_lwb_latency = 0;
	zilog->zl_max_block_size = zil_maxblocksize;

	mutex_init(&zilog->zl_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&zilog->zl_issuer_lock, NULL, MUTEX_DEFAULT, NULL);

	for (int i = 0; i < TXG_SIZE; i++) {
		mutex_init(&zilog->zl_itxg[i].itxg_lock, NULL,
		    MUTEX_DEFAULT, NULL);
	}
	mutex_init(&zilog->zl_fail_itxg.itxg_lock, NULL, MUTEX_DEFAULT, NULL);

	list_create(&zilog->zl_lwb_list, sizeof (lwb_t),
	    offsetof(lwb_t, lwb_node));

	list_create(&zilog->zl_itx_commit_list, sizeof (itx_t),
	    offsetof(itx_t, itx_node));

	cv_init(&zilog->zl_cv_suspend, NULL, CV_DEFAULT, NULL);

	return (zilog);
}

void
zil_free(zilog_t *zilog)
{
	int i;

	zilog->zl_stop_sync = 1;

	ASSERT0(zilog->zl_suspend);
	ASSERT0(zilog->zl_suspending);

	ASSERT(list_is_empty(&zilog->zl_lwb_list));
	list_destroy(&zilog->zl_lwb_list);

	ASSERT(list_is_empty(&zilog->zl_itx_commit_list));
	list_destroy(&zilog->zl_itx_commit_list);

	for (i = 0; i < TXG_SIZE; i++) {
		/*
		 * It's possible for an itx to be generated that doesn't dirty
		 * a txg (e.g. ztest TX_TRUNCATE). So there's no zil_clean()
		 * callback to remove the entry. We remove those here.
		 *
		 * Also free up the ziltest itxs.
		 */
		if (zilog->zl_itxg[i].itxg_itxs)
			zil_itxg_clean(zilog->zl_itxg[i].itxg_itxs);
		mutex_destroy(&zilog->zl_itxg[i].itxg_lock);
	}

	/* XXX clean out the fail itxg -- robn, 2023-04-27 */
	mutex_destroy(&zilog->zl_fail_itxg.itxg_lock);

	mutex_destroy(&zilog->zl_issuer_lock);
	mutex_destroy(&zilog->zl_lock);

	cv_destroy(&zilog->zl_cv_suspend);

	kmem_free(zilog, sizeof (zilog_t));
}

/*
 * Open an intent log.
 */
zilog_t *
zil_open(objset_t *os, zil_get_data_t *get_data)
{
	zilog_t *zilog = dmu_objset_zil(os);

	ASSERT3P(zilog->zl_get_data, ==, NULL);
	ASSERT3P(zilog->zl_last_lwb_opened, ==, NULL);
	ASSERT(list_is_empty(&zilog->zl_lwb_list));

	zilog->zl_get_data = get_data;

	return (zilog);
}

/*
 * Close an intent log.
 */
void
zil_close(zilog_t *zilog)
{
	lwb_t *lwb;
	uint64_t txg;

	if (!dmu_objset_is_snapshot(zilog->zl_os)) {
		zil_commit(zilog, 0);
	} else {
		ASSERT3P(list_tail(&zilog->zl_lwb_list), ==, NULL);
		ASSERT0(zilog->zl_dirty_max_txg);
		ASSERT3B(zilog_is_dirty(zilog), ==, B_FALSE);
	}

	mutex_enter(&zilog->zl_lock);
	lwb = list_tail(&zilog->zl_lwb_list);
	if (lwb == NULL)
		txg = zilog->zl_dirty_max_txg;
	else
		txg = MAX(zilog->zl_dirty_max_txg, lwb->lwb_max_txg);
	mutex_exit(&zilog->zl_lock);

	/*
	 * We need to use txg_wait_synced() to wait long enough for the
	 * ZIL to be clean, and to wait for all pending lwbs to be
	 * written out.
	 */
	if (!dmu_objset_exiting(zilog->zl_os)) {
		if (txg != 0)
			txg_wait_synced(zilog->zl_dmu_pool, txg);

		if (zilog_is_dirty(zilog))
			zfs_dbgmsg("zil (%px) is dirty, txg %llu", zilog,
			    (u_longlong_t)txg);
		if (txg < spa_freeze_txg(zilog->zl_spa))
			VERIFY(!zilog_is_dirty(zilog));
	}

	zilog->zl_get_data = NULL;

	/*
	 * XXX if we're exiting, we may have itxgs holding itxs that need to be
	 *     removed to allow their callbacks to fire -- robn, 2023-05-08
	 */

	/*
	 * Clean up outstanding lwbs. Under normal circumstances there should
	 * be no more than one, but if the ZIL has failed or the pool is
	 * exporting, there might be additional ones in flight, so we need to
	 * take care.
	 */
	mutex_enter(&zilog->zl_lock);
	lwb = list_head(&zilog->zl_lwb_list);
	if (lwb == NULL) {
		mutex_exit(&zilog->zl_lock);
		return;
	}

	if (lwb != list_tail(&zilog->zl_lwb_list)) {
		/* More than one; assert something is wrong. */
		ASSERT(zil_failed(zilog) || spa_exiting_any(zilog->zl_spa));
	}

	do {
		list_remove(&zilog->zl_lwb_list, lwb);

		if (lwb->lwb_state != LWB_STATE_OPENED &&
		    lwb->lwb_state != LWB_STATE_CLOSED) {
			/*
			 * If its been issued, we can't clean it up, as its zios
			 * may yet come back to us. So we just skip it here and
			 * let zil_lwb_write_done() deal with it.
			 */
			continue;
		}

		if (lwb->lwb_fastwrite)
			metaslab_fastwrite_unmark(zilog->zl_spa, &lwb->lwb_blk);

		if (lwb->lwb_buf != NULL)
			zio_buf_free(lwb->lwb_buf, lwb->lwb_sz);

		zil_free_lwb(zilog, lwb);
	} while ((lwb = list_head(&zilog->zl_lwb_list)) != NULL);

	mutex_exit(&zilog->zl_lock);
}

static char *suspend_tag = "zil suspending";

/*
 * Suspend an intent log.  While in suspended mode, we still honor
 * synchronous semantics, but we rely on txg_wait_synced() to do it.
 * On old version pools, we suspend the log briefly when taking a
 * snapshot so that it will have an empty intent log.
 *
 * Long holds are not really intended to be used the way we do here --
 * held for such a short time.  A concurrent caller of dsl_dataset_long_held()
 * could fail.  Therefore we take pains to only put a long hold if it is
 * actually necessary.  Fortunately, it will only be necessary if the
 * objset is currently mounted (or the ZVOL equivalent).  In that case it
 * will already have a long hold, so we are not really making things any worse.
 *
 * Ideally, we would locate the existing long-holder (i.e. the zfsvfs_t or
 * zvol_state_t), and use their mechanism to prevent their hold from being
 * dropped (e.g. VFS_HOLD()).  However, that would be even more pain for
 * very little gain.
 *
 * if cookiep == NULL, this does both the suspend & resume.
 * Otherwise, it returns with the dataset "long held", and the cookie
 * should be passed into zil_resume().
 */
int
zil_suspend(const char *osname, void **cookiep)
{
	objset_t *os;
	zilog_t *zilog;
	const zil_header_t *zh;
	int error;

	error = dmu_objset_hold(osname, suspend_tag, &os);
	if (error != 0)
		return (error);
	zilog = dmu_objset_zil(os);

	mutex_enter(&zilog->zl_lock);
	zh = zilog->zl_header;

	if (zh->zh_flags & ZIL_REPLAY_NEEDED) {		/* unplayed log */
		mutex_exit(&zilog->zl_lock);
		dmu_objset_rele(os, suspend_tag);
		return (SET_ERROR(EBUSY));
	}

	/*
	 * Don't put a long hold in the cases where we can avoid it.  This
	 * is when there is no cookie so we are doing a suspend & resume
	 * (i.e. called from zil_vdev_offline()), and there's nothing to do
	 * for the suspend because it's already suspended, or there's no ZIL.
	 */
	if (cookiep == NULL && !zilog->zl_suspending &&
	    (zilog->zl_suspend > 0 || BP_IS_HOLE(&zh->zh_log))) {
		mutex_exit(&zilog->zl_lock);
		dmu_objset_rele(os, suspend_tag);
		return (0);
	}

	dsl_dataset_long_hold(dmu_objset_ds(os), suspend_tag);
	dsl_pool_rele(dmu_objset_pool(os), suspend_tag);

	zilog->zl_suspend++;

	if (zilog->zl_suspend > 1) {
		/*
		 * Someone else is already suspending it.
		 * Just wait for them to finish.
		 */

		while (zilog->zl_suspending)
			cv_wait(&zilog->zl_cv_suspend, &zilog->zl_lock);
		mutex_exit(&zilog->zl_lock);

		if (cookiep == NULL)
			zil_resume(os);
		else
			*cookiep = os;
		return (0);
	}

	/*
	 * If there is no pointer to an on-disk block, this ZIL must not
	 * be active (e.g. filesystem not mounted), so there's nothing
	 * to clean up.
	 */
	if (BP_IS_HOLE(&zh->zh_log)) {
		ASSERT(cookiep != NULL); /* fast path already handled */

		*cookiep = os;
		mutex_exit(&zilog->zl_lock);
		return (0);
	}

	/*
	 * The ZIL has work to do. Ensure that the associated encryption
	 * key will remain mapped while we are committing the log by
	 * grabbing a reference to it. If the key isn't loaded we have no
	 * choice but to return an error until the wrapping key is loaded.
	 */
	if (os->os_encrypted &&
	    dsl_dataset_create_key_mapping(dmu_objset_ds(os)) != 0) {
		zilog->zl_suspend--;
		mutex_exit(&zilog->zl_lock);
		dsl_dataset_long_rele(dmu_objset_ds(os), suspend_tag);
		dsl_dataset_rele(dmu_objset_ds(os), suspend_tag);
		return (SET_ERROR(EACCES));
	}

	zilog->zl_suspending = B_TRUE;
	mutex_exit(&zilog->zl_lock);

	/*
	 * We need to use zil_commit_impl to ensure we wait for all
	 * LWB_STATE_OPENED and LWB_STATE_ISSUED lwbs to be committed
	 * to disk before proceeding. If we used zil_commit instead, it
	 * would just call txg_wait_synced(), because zl_suspend is set.
	 * txg_wait_synced() doesn't wait for these lwb's to be
	 * LWB_STATE_FLUSH_DONE before returning.
	 */
	zil_commit_impl(zilog, 0);

	/*
	 * Now that we've ensured all lwb's are LWB_STATE_FLUSH_DONE, we
	 * use txg_wait_synced() to ensure the data from the zilog has
	 * migrated to the main pool before calling zil_destroy().
	 *
	 * Its ok to block here, as this is always arrived at via administrative
	 * operations, never application IO.
	 */
	txg_wait_synced(zilog->zl_dmu_pool, 0);

	zil_destroy(zilog, B_FALSE);

	mutex_enter(&zilog->zl_lock);
	zilog->zl_suspending = B_FALSE;
	cv_broadcast(&zilog->zl_cv_suspend);
	mutex_exit(&zilog->zl_lock);

	if (os->os_encrypted)
		dsl_dataset_remove_key_mapping(dmu_objset_ds(os));

	if (cookiep == NULL)
		zil_resume(os);
	else
		*cookiep = os;
	return (0);
}

void
zil_resume(void *cookie)
{
	objset_t *os = cookie;
	zilog_t *zilog = dmu_objset_zil(os);

	mutex_enter(&zilog->zl_lock);
	ASSERT(zilog->zl_suspend != 0);
	zilog->zl_suspend--;
	mutex_exit(&zilog->zl_lock);
	dsl_dataset_long_rele(dmu_objset_ds(os), suspend_tag);
	dsl_dataset_rele(dmu_objset_ds(os), suspend_tag);
}

typedef struct zil_replay_arg {
	zil_replay_func_t **zr_replay;
	void		*zr_arg;
	boolean_t	zr_byteswap;
	char		*zr_lr;
} zil_replay_arg_t;

static int
zil_replay_error(zilog_t *zilog, const lr_t *lr, int error)
{
	char name[ZFS_MAX_DATASET_NAME_LEN];

	zilog->zl_replaying_seq--;	/* didn't actually replay this one */

	dmu_objset_name(zilog->zl_os, name);

	cmn_err(CE_WARN, "ZFS replay transaction error %d, "
	    "dataset %s, seq 0x%llx, txtype %llu %s\n", error, name,
	    (u_longlong_t)lr->lrc_seq,
	    (u_longlong_t)(lr->lrc_txtype & ~TX_CI),
	    (lr->lrc_txtype & TX_CI) ? "CI" : "");

	return (error);
}

static int
zil_replay_log_record(zilog_t *zilog, const lr_t *lr, void *zra,
    uint64_t claim_txg)
{
	zil_replay_arg_t *zr = zra;
	const zil_header_t *zh = zilog->zl_header;
	uint64_t reclen = lr->lrc_reclen;
	uint64_t txtype = lr->lrc_txtype;
	int error = 0;

	zilog->zl_replaying_seq = lr->lrc_seq;

	if (lr->lrc_seq <= zh->zh_replay_seq)	/* already replayed */
		return (0);

	if (lr->lrc_txg < claim_txg)		/* already committed */
		return (0);

	/* Strip case-insensitive bit, still present in log record */
	txtype &= ~TX_CI;

	if (txtype == 0 || txtype >= TX_MAX_TYPE)
		return (zil_replay_error(zilog, lr, EINVAL));

	/*
	 * If this record type can be logged out of order, the object
	 * (lr_foid) may no longer exist.  That's legitimate, not an error.
	 */
	if (TX_OOO(txtype)) {
		error = dmu_object_info(zilog->zl_os,
		    LR_FOID_GET_OBJ(((lr_ooo_t *)lr)->lr_foid), NULL);
		if (error == ENOENT || error == EEXIST)
			return (0);
	}

	/*
	 * Make a copy of the data so we can revise and extend it.
	 */
	bcopy(lr, zr->zr_lr, reclen);

	/*
	 * If this is a TX_WRITE with a blkptr, suck in the data.
	 */
	if (txtype == TX_WRITE && reclen == sizeof (lr_write_t)) {
		error = zil_read_log_data(zilog, (lr_write_t *)lr,
		    zr->zr_lr + reclen);
		if (error != 0)
			return (zil_replay_error(zilog, lr, error));
	}

	/*
	 * The log block containing this lr may have been byteswapped
	 * so that we can easily examine common fields like lrc_txtype.
	 * However, the log is a mix of different record types, and only the
	 * replay vectors know how to byteswap their records.  Therefore, if
	 * the lr was byteswapped, undo it before invoking the replay vector.
	 */
	if (zr->zr_byteswap)
		byteswap_uint64_array(zr->zr_lr, reclen);

	/*
	 * We must now do two things atomically: replay this log record,
	 * and update the log header sequence number to reflect the fact that
	 * we did so. At the end of each replay function the sequence number
	 * is updated if we are in replay mode.
	 */
	error = zr->zr_replay[txtype](zr->zr_arg, zr->zr_lr, zr->zr_byteswap);
	if (error != 0) {
		/*
		 * The DMU's dnode layer doesn't see removes until the txg
		 * commits, so a subsequent claim can spuriously fail with
		 * EEXIST. So if we receive any error we try syncing out
		 * any removes then retry the transaction.  Note that we
		 * specify B_FALSE for byteswap now, so we don't do it twice.
		 */
		txg_wait_synced(spa_get_dsl(zilog->zl_spa), 0);
		error = zr->zr_replay[txtype](zr->zr_arg, zr->zr_lr, B_FALSE);
		if (error != 0)
			return (zil_replay_error(zilog, lr, error));
	}
	return (0);
}

static int
zil_incr_blks(zilog_t *zilog, const blkptr_t *bp, void *arg, uint64_t claim_txg)
{
	(void) bp, (void) arg, (void) claim_txg;

	zilog->zl_replay_blks++;

	return (0);
}

/*
 * If this dataset has a non-empty intent log, replay it and destroy it.
 */
void
zil_replay(objset_t *os, void *arg, zil_replay_func_t *replay_func[TX_MAX_TYPE])
{
	zilog_t *zilog = dmu_objset_zil(os);
	const zil_header_t *zh = zilog->zl_header;
	zil_replay_arg_t zr;

	if ((zh->zh_flags & ZIL_REPLAY_NEEDED) == 0) {
		zil_destroy(zilog, B_TRUE);
		return;
	}

	zr.zr_replay = replay_func;
	zr.zr_arg = arg;
	zr.zr_byteswap = BP_SHOULD_BYTESWAP(&zh->zh_log);
	zr.zr_lr = vmem_alloc(2 * SPA_MAXBLOCKSIZE, KM_SLEEP);

	/*
	 * Wait for in-progress removes to sync before starting replay.
	 */
	txg_wait_synced(zilog->zl_dmu_pool, 0);

	zilog->zl_replay = B_TRUE;
	zilog->zl_replay_time = ddi_get_lbolt();
	ASSERT(zilog->zl_replay_blks == 0);
	(void) zil_parse(zilog, zil_incr_blks, zil_replay_log_record, &zr,
	    zh->zh_claim_txg, B_TRUE);
	vmem_free(zr.zr_lr, 2 * SPA_MAXBLOCKSIZE);

	zil_destroy(zilog, B_FALSE);
	txg_wait_synced(zilog->zl_dmu_pool, zilog->zl_destroy_txg);
	zilog->zl_replay = B_FALSE;
}

boolean_t
zil_replaying(zilog_t *zilog, dmu_tx_t *tx)
{
	if (zilog->zl_sync == ZFS_SYNC_DISABLED)
		return (B_TRUE);

	if (zilog->zl_replay) {
		dsl_dataset_dirty(dmu_objset_ds(zilog->zl_os), tx);
		zilog->zl_replayed_seq[dmu_tx_get_txg(tx) & TXG_MASK] =
		    zilog->zl_replaying_seq;
		return (B_TRUE);
	}

	return (B_FALSE);
}

int
zil_reset(const char *osname, void *arg)
{
	(void) arg;

	int error = zil_suspend(osname, NULL);
	/* EACCES means crypto key not loaded */
	if ((error == EACCES) || (error == EBUSY))
		return (SET_ERROR(error));
	if (error != 0)
		return (SET_ERROR(EEXIST));
	return (0);
}

EXPORT_SYMBOL(zil_alloc);
EXPORT_SYMBOL(zil_free);
EXPORT_SYMBOL(zil_open);
EXPORT_SYMBOL(zil_close);
EXPORT_SYMBOL(zil_replay);
EXPORT_SYMBOL(zil_replaying);
EXPORT_SYMBOL(zil_destroy);
EXPORT_SYMBOL(zil_destroy_sync);
EXPORT_SYMBOL(zil_itx_create);
EXPORT_SYMBOL(zil_itx_destroy);
EXPORT_SYMBOL(zil_itx_assign);
EXPORT_SYMBOL(zil_commit);
EXPORT_SYMBOL(zil_claim);
EXPORT_SYMBOL(zil_check_log_chain);
EXPORT_SYMBOL(zil_sync);
EXPORT_SYMBOL(zil_clean);
EXPORT_SYMBOL(zil_suspend);
EXPORT_SYMBOL(zil_resume);
EXPORT_SYMBOL(zil_bp_tree_add);
EXPORT_SYMBOL(zil_set_sync);
EXPORT_SYMBOL(zil_set_logbias);

/* BEGIN CSTYLED */
ZFS_MODULE_PARAM(zfs, zfs_, commit_timeout_pct, INT, ZMOD_RW,
	"ZIL block open timeout percentage");

ZFS_MODULE_PARAM(zfs_zil, zil_, replay_disable, INT, ZMOD_RW,
	"Disable intent logging replay");

ZFS_MODULE_PARAM(zfs_zil, zil_, nocacheflush, INT, ZMOD_RW,
	"Disable ZIL cache flushes");

ZFS_MODULE_PARAM(zfs_zil, zil_, slog_bulk, ULONG, ZMOD_RW,
	"Limit in bytes slog sync writes per commit");

ZFS_MODULE_PARAM(zfs_zil, zil_, maxblocksize, INT, ZMOD_RW,
	"Limit in bytes of ZIL log block size");
/* END CSTYLED */
