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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/dmu.h>
#include <sys/zap.h>
#include <sys/arc.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/zil.h>
#include <sys/zil_impl.h>
#include <sys/dsl_dataset.h>
#include <sys/vdev.h>
#include <sys/dmu_tx.h>

/*
 * The zfs intent log (ZIL) saves transaction records of system calls
 * that change the file system in memory with enough information
 * to be able to replay them. These are stored in memory until
 * either the DMU transaction group (txg) commits them to the stable pool
 * and they can be discarded, or they are flushed to the stable log
 * (also in the pool) due to a fsync, O_DSYNC or other synchronous
 * requirement. In the event of a panic or power fail then those log
 * records (transactions) are replayed.
 *
 * There is one ZIL per file system. Its on-disk (pool) format consists
 * of 3 parts:
 *
 * 	- ZIL header
 * 	- ZIL blocks
 * 	- ZIL records
 *
 * A log record holds a system call transaction. Log blocks can
 * hold many log records and the blocks are chained together.
 * Each ZIL block contains a block pointer (blkptr_t) to the next
 * ZIL block in the chain. The ZIL header points to the first
 * block in the chain. Note there is not a fixed place in the pool
 * to hold blocks. They are dynamically allocated and freed as
 * needed from the blocks available. Figure X shows the ZIL structure:
 */

/*
 * This global ZIL switch affects all pools
 */
int zil_disable = 0;	/* disable intent logging */

/*
 * Tunable parameter for debugging or performance analysis.  Setting
 * zfs_nocacheflush will cause corruption on power loss if a volatile
 * out-of-order write cache is enabled.
 */
boolean_t zfs_nocacheflush = B_FALSE;

static kmem_cache_t *zil_lwb_cache;

static int
zil_dva_compare(const void *x1, const void *x2)
{
	const dva_t *dva1 = x1;
	const dva_t *dva2 = x2;

	if (DVA_GET_VDEV(dva1) < DVA_GET_VDEV(dva2))
		return (-1);
	if (DVA_GET_VDEV(dva1) > DVA_GET_VDEV(dva2))
		return (1);

	if (DVA_GET_OFFSET(dva1) < DVA_GET_OFFSET(dva2))
		return (-1);
	if (DVA_GET_OFFSET(dva1) > DVA_GET_OFFSET(dva2))
		return (1);

	return (0);
}

static void
zil_dva_tree_init(avl_tree_t *t)
{
	avl_create(t, zil_dva_compare, sizeof (zil_dva_node_t),
	    offsetof(zil_dva_node_t, zn_node));
}

static void
zil_dva_tree_fini(avl_tree_t *t)
{
	zil_dva_node_t *zn;
	void *cookie = NULL;

	while ((zn = avl_destroy_nodes(t, &cookie)) != NULL)
		kmem_free(zn, sizeof (zil_dva_node_t));

	avl_destroy(t);
}

static int
zil_dva_tree_add(avl_tree_t *t, dva_t *dva)
{
	zil_dva_node_t *zn;
	avl_index_t where;

	if (avl_find(t, dva, &where) != NULL)
		return (EEXIST);

	zn = kmem_alloc(sizeof (zil_dva_node_t), KM_SLEEP);
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

	zc->zc_word[ZIL_ZC_GUID_0] = spa_get_random(-1ULL);
	zc->zc_word[ZIL_ZC_GUID_1] = spa_get_random(-1ULL);
	zc->zc_word[ZIL_ZC_OBJSET] = dmu_objset_id(zilog->zl_os);
	zc->zc_word[ZIL_ZC_SEQ] = 1ULL;
}

/*
 * Read a log block, make sure it's valid, and byteswap it if necessary.
 */
static int
zil_read_log_block(zilog_t *zilog, const blkptr_t *bp, arc_buf_t **abufpp)
{
	blkptr_t blk = *bp;
	zbookmark_t zb;
	uint32_t aflags = ARC_WAIT;
	int error;

	zb.zb_objset = bp->blk_cksum.zc_word[ZIL_ZC_OBJSET];
	zb.zb_object = 0;
	zb.zb_level = -1;
	zb.zb_blkid = bp->blk_cksum.zc_word[ZIL_ZC_SEQ];

	*abufpp = NULL;

	/*
	 * We shouldn't be doing any scrubbing while we're doing log
	 * replay, it's OK to not lock.
	 */
	error = arc_read_nolock(NULL, zilog->zl_spa, &blk,
	    arc_getbuf_func, abufpp, ZIO_PRIORITY_SYNC_READ, ZIO_FLAG_CANFAIL |
	    ZIO_FLAG_SPECULATIVE | ZIO_FLAG_SCRUB, &aflags, &zb);

	if (error == 0) {
		char *data = (*abufpp)->b_data;
		uint64_t blksz = BP_GET_LSIZE(bp);
		zil_trailer_t *ztp = (zil_trailer_t *)(data + blksz) - 1;
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

		if (bcmp(&cksum, &ztp->zit_next_blk.blk_cksum,
		    sizeof (cksum)) || BP_IS_HOLE(&ztp->zit_next_blk) ||
		    (ztp->zit_nused > (blksz - sizeof (zil_trailer_t)))) {
			error = ECKSUM;
		}

		if (error) {
			VERIFY(arc_buf_remove_ref(*abufpp, abufpp) == 1);
			*abufpp = NULL;
		}
	}

	dprintf("error %d on %llu:%llu\n", error, zb.zb_objset, zb.zb_blkid);

	return (error);
}

/*
 * Parse the intent log, and call parse_func for each valid record within.
 * Return the highest sequence number.
 */
uint64_t
zil_parse(zilog_t *zilog, zil_parse_blk_func_t *parse_blk_func,
    zil_parse_lr_func_t *parse_lr_func, void *arg, uint64_t txg)
{
	const zil_header_t *zh = zilog->zl_header;
	uint64_t claim_seq = zh->zh_claim_seq;
	uint64_t seq = 0;
	uint64_t max_seq = 0;
	blkptr_t blk = zh->zh_log;
	arc_buf_t *abuf;
	char *lrbuf, *lrp;
	zil_trailer_t *ztp;
	int reclen, error;

	if (BP_IS_HOLE(&blk))
		return (max_seq);

	/*
	 * Starting at the block pointed to by zh_log we read the log chain.
	 * For each block in the chain we strongly check that block to
	 * ensure its validity.  We stop when an invalid block is found.
	 * For each block pointer in the chain we call parse_blk_func().
	 * For each record in each valid block we call parse_lr_func().
	 * If the log has been claimed, stop if we encounter a sequence
	 * number greater than the highest claimed sequence number.
	 */
	zil_dva_tree_init(&zilog->zl_dva_tree);
	for (;;) {
		seq = blk.blk_cksum.zc_word[ZIL_ZC_SEQ];

		if (claim_seq != 0 && seq > claim_seq)
			break;

		ASSERT(max_seq < seq);
		max_seq = seq;

		error = zil_read_log_block(zilog, &blk, &abuf);

		if (parse_blk_func != NULL)
			parse_blk_func(zilog, &blk, arg, txg);

		if (error)
			break;

		lrbuf = abuf->b_data;
		ztp = (zil_trailer_t *)(lrbuf + BP_GET_LSIZE(&blk)) - 1;
		blk = ztp->zit_next_blk;

		if (parse_lr_func == NULL) {
			VERIFY(arc_buf_remove_ref(abuf, &abuf) == 1);
			continue;
		}

		for (lrp = lrbuf; lrp < lrbuf + ztp->zit_nused; lrp += reclen) {
			lr_t *lr = (lr_t *)lrp;
			reclen = lr->lrc_reclen;
			ASSERT3U(reclen, >=, sizeof (lr_t));
			parse_lr_func(zilog, lr, arg, txg);
		}
		VERIFY(arc_buf_remove_ref(abuf, &abuf) == 1);
	}
	zil_dva_tree_fini(&zilog->zl_dva_tree);

	return (max_seq);
}

/* ARGSUSED */
static void
zil_claim_log_block(zilog_t *zilog, blkptr_t *bp, void *tx, uint64_t first_txg)
{
	spa_t *spa = zilog->zl_spa;
	int err;

	/*
	 * Claim log block if not already committed and not already claimed.
	 */
	if (bp->blk_birth >= first_txg &&
	    zil_dva_tree_add(&zilog->zl_dva_tree, BP_IDENTITY(bp)) == 0) {
		err = zio_wait(zio_claim(NULL, spa, first_txg, bp, NULL, NULL,
		    ZIO_FLAG_MUSTSUCCEED));
		ASSERT(err == 0);
	}
}

static void
zil_claim_log_record(zilog_t *zilog, lr_t *lrc, void *tx, uint64_t first_txg)
{
	if (lrc->lrc_txtype == TX_WRITE) {
		lr_write_t *lr = (lr_write_t *)lrc;
		zil_claim_log_block(zilog, &lr->lr_blkptr, tx, first_txg);
	}
}

/* ARGSUSED */
static void
zil_free_log_block(zilog_t *zilog, blkptr_t *bp, void *tx, uint64_t claim_txg)
{
	zio_free_blk(zilog->zl_spa, bp, dmu_tx_get_txg(tx));
}

static void
zil_free_log_record(zilog_t *zilog, lr_t *lrc, void *tx, uint64_t claim_txg)
{
	/*
	 * If we previously claimed it, we need to free it.
	 */
	if (claim_txg != 0 && lrc->lrc_txtype == TX_WRITE) {
		lr_write_t *lr = (lr_write_t *)lrc;
		blkptr_t *bp = &lr->lr_blkptr;
		if (bp->blk_birth >= claim_txg &&
		    !zil_dva_tree_add(&zilog->zl_dva_tree, BP_IDENTITY(bp))) {
			(void) arc_free(NULL, zilog->zl_spa,
			    dmu_tx_get_txg(tx), bp, NULL, NULL, ARC_WAIT);
		}
	}
}

/*
 * Create an on-disk intent log.
 */
static void
zil_create(zilog_t *zilog)
{
	const zil_header_t *zh = zilog->zl_header;
	lwb_t *lwb;
	uint64_t txg = 0;
	dmu_tx_t *tx = NULL;
	blkptr_t blk;
	int error = 0;

	/*
	 * Wait for any previous destroy to complete.
	 */
	txg_wait_synced(zilog->zl_dmu_pool, zilog->zl_destroy_txg);

	ASSERT(zh->zh_claim_txg == 0);
	ASSERT(zh->zh_replay_seq == 0);

	blk = zh->zh_log;

	/*
	 * If we don't already have an initial log block or we have one
	 * but it's the wrong endianness then allocate one.
	 */
	if (BP_IS_HOLE(&blk) || BP_SHOULD_BYTESWAP(&blk)) {
		tx = dmu_tx_create(zilog->zl_os);
		(void) dmu_tx_assign(tx, TXG_WAIT);
		dsl_dataset_dirty(dmu_objset_ds(zilog->zl_os), tx);
		txg = dmu_tx_get_txg(tx);

		if (!BP_IS_HOLE(&blk)) {
			zio_free_blk(zilog->zl_spa, &blk, txg);
			BP_ZERO(&blk);
		}

		error = zio_alloc_blk(zilog->zl_spa, ZIL_MIN_BLKSZ, &blk,
		    NULL, txg);

		if (error == 0)
			zil_init_log_chain(zilog, &blk);
	}

	/*
	 * Allocate a log write buffer (lwb) for the first log block.
	 */
	if (error == 0) {
		lwb = kmem_cache_alloc(zil_lwb_cache, KM_SLEEP);
		lwb->lwb_zilog = zilog;
		lwb->lwb_blk = blk;
		lwb->lwb_nused = 0;
		lwb->lwb_sz = BP_GET_LSIZE(&lwb->lwb_blk);
		lwb->lwb_buf = zio_buf_alloc(lwb->lwb_sz);
		lwb->lwb_max_txg = txg;
		lwb->lwb_zio = NULL;

		mutex_enter(&zilog->zl_lock);
		list_insert_tail(&zilog->zl_lwb_list, lwb);
		mutex_exit(&zilog->zl_lock);
	}

	/*
	 * If we just allocated the first log block, commit our transaction
	 * and wait for zil_sync() to stuff the block poiner into zh_log.
	 * (zh is part of the MOS, so we cannot modify it in open context.)
	 */
	if (tx != NULL) {
		dmu_tx_commit(tx);
		txg_wait_synced(zilog->zl_dmu_pool, txg);
	}

	ASSERT(bcmp(&blk, &zh->zh_log, sizeof (blk)) == 0);
}

/*
 * In one tx, free all log blocks and clear the log header.
 * If keep_first is set, then we're replaying a log with no content.
 * We want to keep the first block, however, so that the first
 * synchronous transaction doesn't require a txg_wait_synced()
 * in zil_create().  We don't need to txg_wait_synced() here either
 * when keep_first is set, because both zil_create() and zil_destroy()
 * will wait for any in-progress destroys to complete.
 */
void
zil_destroy(zilog_t *zilog, boolean_t keep_first)
{
	const zil_header_t *zh = zilog->zl_header;
	lwb_t *lwb;
	dmu_tx_t *tx;
	uint64_t txg;

	/*
	 * Wait for any previous destroy to complete.
	 */
	txg_wait_synced(zilog->zl_dmu_pool, zilog->zl_destroy_txg);

	if (BP_IS_HOLE(&zh->zh_log))
		return;

	tx = dmu_tx_create(zilog->zl_os);
	(void) dmu_tx_assign(tx, TXG_WAIT);
	dsl_dataset_dirty(dmu_objset_ds(zilog->zl_os), tx);
	txg = dmu_tx_get_txg(tx);

	mutex_enter(&zilog->zl_lock);

	/*
	 * It is possible for the ZIL to get the previously mounted zilog
	 * structure of the same dataset if quickly remounted and the dbuf
	 * eviction has not completed. In this case we can see a non
	 * empty lwb list and keep_first will be set. We fix this by
	 * clearing the keep_first. This will be slower but it's very rare.
	 */
	if (!list_is_empty(&zilog->zl_lwb_list) && keep_first)
		keep_first = B_FALSE;

	ASSERT3U(zilog->zl_destroy_txg, <, txg);
	zilog->zl_destroy_txg = txg;
	zilog->zl_keep_first = keep_first;

	if (!list_is_empty(&zilog->zl_lwb_list)) {
		ASSERT(zh->zh_claim_txg == 0);
		ASSERT(!keep_first);
		while ((lwb = list_head(&zilog->zl_lwb_list)) != NULL) {
			list_remove(&zilog->zl_lwb_list, lwb);
			if (lwb->lwb_buf != NULL)
				zio_buf_free(lwb->lwb_buf, lwb->lwb_sz);
			zio_free_blk(zilog->zl_spa, &lwb->lwb_blk, txg);
			kmem_cache_free(zil_lwb_cache, lwb);
		}
	} else {
		if (!keep_first) {
			(void) zil_parse(zilog, zil_free_log_block,
			    zil_free_log_record, tx, zh->zh_claim_txg);
		}
	}
	mutex_exit(&zilog->zl_lock);

	dmu_tx_commit(tx);
}

/*
 * return true if the initial log block is not valid
 */
static boolean_t
zil_empty(zilog_t *zilog)
{
	const zil_header_t *zh = zilog->zl_header;
	arc_buf_t *abuf = NULL;

	if (BP_IS_HOLE(&zh->zh_log))
		return (B_TRUE);

	if (zil_read_log_block(zilog, &zh->zh_log, &abuf) != 0)
		return (B_TRUE);

	VERIFY(arc_buf_remove_ref(abuf, &abuf) == 1);
	return (B_FALSE);
}

int
zil_claim(char *osname, void *txarg)
{
	dmu_tx_t *tx = txarg;
	uint64_t first_txg = dmu_tx_get_txg(tx);
	zilog_t *zilog;
	zil_header_t *zh;
	objset_t *os;
	int error;

	error = dmu_objset_open(osname, DMU_OST_ANY, DS_MODE_USER, &os);
	if (error) {
		cmn_err(CE_WARN, "can't open objset for %s", osname);
		return (0);
	}

	zilog = dmu_objset_zil(os);
	zh = zil_header_in_syncing_context(zilog);

	if (zilog->zl_spa->spa_log_state == SPA_LOG_CLEAR) {
		if (!BP_IS_HOLE(&zh->zh_log))
			zio_free_blk(zilog->zl_spa, &zh->zh_log, first_txg);
		BP_ZERO(&zh->zh_log);
		dsl_dataset_dirty(dmu_objset_ds(os), tx);
	}

	/*
	 * Record here whether the zil has any records to replay.
	 * If the header block pointer is null or the block points
	 * to the stubby then we know there are no valid log records.
	 * We use the header to store this state as the the zilog gets
	 * freed later in dmu_objset_close().
	 * The flags (and the rest of the header fields) are cleared in
	 * zil_sync() as a result of a zil_destroy(), after replaying the log.
	 *
	 * Note, the intent log can be empty but still need the
	 * stubby to be claimed.
	 */
	if (!zil_empty(zilog)) {
		zh->zh_flags |= ZIL_REPLAY_NEEDED;
		dsl_dataset_dirty(dmu_objset_ds(os), tx);
	}

	/*
	 * Claim all log blocks if we haven't already done so, and remember
	 * the highest claimed sequence number.  This ensures that if we can
	 * read only part of the log now (e.g. due to a missing device),
	 * but we can read the entire log later, we will not try to replay
	 * or destroy beyond the last block we successfully claimed.
	 */
	ASSERT3U(zh->zh_claim_txg, <=, first_txg);
	if (zh->zh_claim_txg == 0 && !BP_IS_HOLE(&zh->zh_log)) {
		zh->zh_claim_txg = first_txg;
		zh->zh_claim_seq = zil_parse(zilog, zil_claim_log_block,
		    zil_claim_log_record, tx, first_txg);
		dsl_dataset_dirty(dmu_objset_ds(os), tx);
	}

	ASSERT3U(first_txg, ==, (spa_last_synced_txg(zilog->zl_spa) + 1));
	dmu_objset_close(os);
	return (0);
}

/*
 * Check the log by walking the log chain.
 * Checksum errors are ok as they indicate the end of the chain.
 * Any other error (no device or read failure) returns an error.
 */
/* ARGSUSED */
int
zil_check_log_chain(char *osname, void *txarg)
{
	zilog_t *zilog;
	zil_header_t *zh;
	blkptr_t blk;
	arc_buf_t *abuf;
	objset_t *os;
	char *lrbuf;
	zil_trailer_t *ztp;
	int error;

	error = dmu_objset_open(osname, DMU_OST_ANY, DS_MODE_USER, &os);
	if (error) {
		cmn_err(CE_WARN, "can't open objset for %s", osname);
		return (0);
	}

	zilog = dmu_objset_zil(os);
	zh = zil_header_in_syncing_context(zilog);
	blk = zh->zh_log;
	if (BP_IS_HOLE(&blk)) {
		dmu_objset_close(os);
		return (0); /* no chain */
	}

	for (;;) {
		error = zil_read_log_block(zilog, &blk, &abuf);
		if (error)
			break;
		lrbuf = abuf->b_data;
		ztp = (zil_trailer_t *)(lrbuf + BP_GET_LSIZE(&blk)) - 1;
		blk = ztp->zit_next_blk;
		VERIFY(arc_buf_remove_ref(abuf, &abuf) == 1);
	}
	dmu_objset_close(os);
	if (error == ECKSUM)
		return (0); /* normal end of chain */
	return (error);
}

static int
zil_vdev_compare(const void *x1, const void *x2)
{
	uint64_t v1 = ((zil_vdev_node_t *)x1)->zv_vdev;
	uint64_t v2 = ((zil_vdev_node_t *)x2)->zv_vdev;

	if (v1 < v2)
		return (-1);
	if (v1 > v2)
		return (1);

	return (0);
}

void
zil_add_block(zilog_t *zilog, blkptr_t *bp)
{
	avl_tree_t *t = &zilog->zl_vdev_tree;
	avl_index_t where;
	zil_vdev_node_t *zv, zvsearch;
	int ndvas = BP_GET_NDVAS(bp);
	int i;

	if (zfs_nocacheflush)
		return;

	ASSERT(zilog->zl_writer);

	/*
	 * Even though we're zl_writer, we still need a lock because the
	 * zl_get_data() callbacks may have dmu_sync() done callbacks
	 * that will run concurrently.
	 */
	mutex_enter(&zilog->zl_vdev_lock);
	for (i = 0; i < ndvas; i++) {
		zvsearch.zv_vdev = DVA_GET_VDEV(&bp->blk_dva[i]);
		if (avl_find(t, &zvsearch, &where) == NULL) {
			zv = kmem_alloc(sizeof (*zv), KM_SLEEP);
			zv->zv_vdev = zvsearch.zv_vdev;
			avl_insert(t, zv, where);
		}
	}
	mutex_exit(&zilog->zl_vdev_lock);
}

void
zil_flush_vdevs(zilog_t *zilog)
{
	spa_t *spa = zilog->zl_spa;
	avl_tree_t *t = &zilog->zl_vdev_tree;
	void *cookie = NULL;
	zil_vdev_node_t *zv;
	zio_t *zio;

	ASSERT(zilog->zl_writer);

	/*
	 * We don't need zl_vdev_lock here because we're the zl_writer,
	 * and all zl_get_data() callbacks are done.
	 */
	if (avl_numnodes(t) == 0)
		return;

	spa_config_enter(spa, SCL_STATE, FTAG, RW_READER);

	zio = zio_root(spa, NULL, NULL, ZIO_FLAG_CANFAIL);

	while ((zv = avl_destroy_nodes(t, &cookie)) != NULL) {
		vdev_t *vd = vdev_lookup_top(spa, zv->zv_vdev);
		if (vd != NULL)
			zio_flush(zio, vd);
		kmem_free(zv, sizeof (*zv));
	}

	/*
	 * Wait for all the flushes to complete.  Not all devices actually
	 * support the DKIOCFLUSHWRITECACHE ioctl, so it's OK if it fails.
	 */
	(void) zio_wait(zio);

	spa_config_exit(spa, SCL_STATE, FTAG);
}

/*
 * Function called when a log block write completes
 */
static void
zil_lwb_write_done(zio_t *zio)
{
	lwb_t *lwb = zio->io_private;
	zilog_t *zilog = lwb->lwb_zilog;

	ASSERT(BP_GET_COMPRESS(zio->io_bp) == ZIO_COMPRESS_OFF);
	ASSERT(BP_GET_CHECKSUM(zio->io_bp) == ZIO_CHECKSUM_ZILOG);
	ASSERT(BP_GET_TYPE(zio->io_bp) == DMU_OT_INTENT_LOG);
	ASSERT(BP_GET_LEVEL(zio->io_bp) == 0);
	ASSERT(BP_GET_BYTEORDER(zio->io_bp) == ZFS_HOST_BYTEORDER);
	ASSERT(!BP_IS_GANG(zio->io_bp));
	ASSERT(!BP_IS_HOLE(zio->io_bp));
	ASSERT(zio->io_bp->blk_fill == 0);

	/*
	 * Ensure the lwb buffer pointer is cleared before releasing
	 * the txg. If we have had an allocation failure and
	 * the txg is waiting to sync then we want want zil_sync()
	 * to remove the lwb so that it's not picked up as the next new
	 * one in zil_commit_writer(). zil_sync() will only remove
	 * the lwb if lwb_buf is null.
	 */
	zio_buf_free(lwb->lwb_buf, lwb->lwb_sz);
	mutex_enter(&zilog->zl_lock);
	lwb->lwb_buf = NULL;
	if (zio->io_error)
		zilog->zl_log_error = B_TRUE;
	mutex_exit(&zilog->zl_lock);

	/*
	 * Now that we've written this log block, we have a stable pointer
	 * to the next block in the chain, so it's OK to let the txg in
	 * which we allocated the next block sync.
	 */
	txg_rele_to_sync(&lwb->lwb_txgh);
}

/*
 * Initialize the io for a log block.
 */
static void
zil_lwb_write_init(zilog_t *zilog, lwb_t *lwb)
{
	zbookmark_t zb;

	zb.zb_objset = lwb->lwb_blk.blk_cksum.zc_word[ZIL_ZC_OBJSET];
	zb.zb_object = 0;
	zb.zb_level = -1;
	zb.zb_blkid = lwb->lwb_blk.blk_cksum.zc_word[ZIL_ZC_SEQ];

	if (zilog->zl_root_zio == NULL) {
		zilog->zl_root_zio = zio_root(zilog->zl_spa, NULL, NULL,
		    ZIO_FLAG_CANFAIL);
	}
	if (lwb->lwb_zio == NULL) {
		lwb->lwb_zio = zio_rewrite(zilog->zl_root_zio, zilog->zl_spa,
		    0, &lwb->lwb_blk, lwb->lwb_buf, lwb->lwb_sz,
		    zil_lwb_write_done, lwb, ZIO_PRIORITY_LOG_WRITE,
		    ZIO_FLAG_CANFAIL | ZIO_FLAG_SPECULATIVE, &zb);
	}
}

/*
 * Start a log block write and advance to the next log block.
 * Calls are serialized.
 */
static lwb_t *
zil_lwb_write_start(zilog_t *zilog, lwb_t *lwb)
{
	lwb_t *nlwb;
	zil_trailer_t *ztp = (zil_trailer_t *)(lwb->lwb_buf + lwb->lwb_sz) - 1;
	spa_t *spa = zilog->zl_spa;
	blkptr_t *bp = &ztp->zit_next_blk;
	uint64_t txg;
	uint64_t zil_blksz;
	int error;

	ASSERT(lwb->lwb_nused <= ZIL_BLK_DATA_SZ(lwb));

	/*
	 * Allocate the next block and save its address in this block
	 * before writing it in order to establish the log chain.
	 * Note that if the allocation of nlwb synced before we wrote
	 * the block that points at it (lwb), we'd leak it if we crashed.
	 * Therefore, we don't do txg_rele_to_sync() until zil_lwb_write_done().
	 */
	txg = txg_hold_open(zilog->zl_dmu_pool, &lwb->lwb_txgh);
	txg_rele_to_quiesce(&lwb->lwb_txgh);

	/*
	 * Pick a ZIL blocksize. We request a size that is the
	 * maximum of the previous used size, the current used size and
	 * the amount waiting in the queue.
	 */
	zil_blksz = MAX(zilog->zl_prev_used,
	    zilog->zl_cur_used + sizeof (*ztp));
	zil_blksz = MAX(zil_blksz, zilog->zl_itx_list_sz + sizeof (*ztp));
	zil_blksz = P2ROUNDUP_TYPED(zil_blksz, ZIL_MIN_BLKSZ, uint64_t);
	if (zil_blksz > ZIL_MAX_BLKSZ)
		zil_blksz = ZIL_MAX_BLKSZ;

	BP_ZERO(bp);
	/* pass the old blkptr in order to spread log blocks across devs */
	error = zio_alloc_blk(spa, zil_blksz, bp, &lwb->lwb_blk, txg);
	if (error) {
		dmu_tx_t *tx = dmu_tx_create_assigned(zilog->zl_dmu_pool, txg);

		/*
		 * We dirty the dataset to ensure that zil_sync() will
		 * be called to remove this lwb from our zl_lwb_list.
		 * Failing to do so, may leave an lwb with a NULL lwb_buf
		 * hanging around on the zl_lwb_list.
		 */
		dsl_dataset_dirty(dmu_objset_ds(zilog->zl_os), tx);
		dmu_tx_commit(tx);

		/*
		 * Since we've just experienced an allocation failure so we
		 * terminate the current lwb and send it on its way.
		 */
		ztp->zit_pad = 0;
		ztp->zit_nused = lwb->lwb_nused;
		ztp->zit_bt.zbt_cksum = lwb->lwb_blk.blk_cksum;
		zio_nowait(lwb->lwb_zio);

		/*
		 * By returning NULL the caller will call tx_wait_synced()
		 */
		return (NULL);
	}

	ASSERT3U(bp->blk_birth, ==, txg);
	ztp->zit_pad = 0;
	ztp->zit_nused = lwb->lwb_nused;
	ztp->zit_bt.zbt_cksum = lwb->lwb_blk.blk_cksum;
	bp->blk_cksum = lwb->lwb_blk.blk_cksum;
	bp->blk_cksum.zc_word[ZIL_ZC_SEQ]++;

	/*
	 * Allocate a new log write buffer (lwb).
	 */
	nlwb = kmem_cache_alloc(zil_lwb_cache, KM_SLEEP);

	nlwb->lwb_zilog = zilog;
	nlwb->lwb_blk = *bp;
	nlwb->lwb_nused = 0;
	nlwb->lwb_sz = BP_GET_LSIZE(&nlwb->lwb_blk);
	nlwb->lwb_buf = zio_buf_alloc(nlwb->lwb_sz);
	nlwb->lwb_max_txg = txg;
	nlwb->lwb_zio = NULL;

	/*
	 * Put new lwb at the end of the log chain
	 */
	mutex_enter(&zilog->zl_lock);
	list_insert_tail(&zilog->zl_lwb_list, nlwb);
	mutex_exit(&zilog->zl_lock);

	/* Record the block for later vdev flushing */
	zil_add_block(zilog, &lwb->lwb_blk);

	/*
	 * kick off the write for the old log block
	 */
	dprintf_bp(&lwb->lwb_blk, "lwb %p txg %llu: ", lwb, txg);
	ASSERT(lwb->lwb_zio);
	zio_nowait(lwb->lwb_zio);

	return (nlwb);
}

static lwb_t *
zil_lwb_commit(zilog_t *zilog, itx_t *itx, lwb_t *lwb)
{
	lr_t *lrc = &itx->itx_lr; /* common log record */
	lr_write_t *lr = (lr_write_t *)lrc;
	uint64_t txg = lrc->lrc_txg;
	uint64_t reclen = lrc->lrc_reclen;
	uint64_t dlen;

	if (lwb == NULL)
		return (NULL);
	ASSERT(lwb->lwb_buf != NULL);

	if (lrc->lrc_txtype == TX_WRITE && itx->itx_wr_state == WR_NEED_COPY)
		dlen = P2ROUNDUP_TYPED(
		    lr->lr_length, sizeof (uint64_t), uint64_t);
	else
		dlen = 0;

	zilog->zl_cur_used += (reclen + dlen);

	zil_lwb_write_init(zilog, lwb);

	/*
	 * If this record won't fit in the current log block, start a new one.
	 */
	if (lwb->lwb_nused + reclen + dlen > ZIL_BLK_DATA_SZ(lwb)) {
		lwb = zil_lwb_write_start(zilog, lwb);
		if (lwb == NULL)
			return (NULL);
		zil_lwb_write_init(zilog, lwb);
		ASSERT(lwb->lwb_nused == 0);
		if (reclen + dlen > ZIL_BLK_DATA_SZ(lwb)) {
			txg_wait_synced(zilog->zl_dmu_pool, txg);
			return (lwb);
		}
	}

	/*
	 * Update the lrc_seq, to be log record sequence number. See zil.h
	 * Then copy the record to the log buffer.
	 */
	lrc->lrc_seq = ++zilog->zl_lr_seq; /* we are single threaded */
	bcopy(lrc, lwb->lwb_buf + lwb->lwb_nused, reclen);

	/*
	 * If it's a write, fetch the data or get its blkptr as appropriate.
	 */
	if (lrc->lrc_txtype == TX_WRITE) {
		if (txg > spa_freeze_txg(zilog->zl_spa))
			txg_wait_synced(zilog->zl_dmu_pool, txg);
		if (itx->itx_wr_state != WR_COPIED) {
			char *dbuf;
			int error;

			/* alignment is guaranteed */
			lr = (lr_write_t *)(lwb->lwb_buf + lwb->lwb_nused);
			if (dlen) {
				ASSERT(itx->itx_wr_state == WR_NEED_COPY);
				dbuf = lwb->lwb_buf + lwb->lwb_nused + reclen;
				lr->lr_common.lrc_reclen += dlen;
			} else {
				ASSERT(itx->itx_wr_state == WR_INDIRECT);
				dbuf = NULL;
			}
			error = zilog->zl_get_data(
			    itx->itx_private, lr, dbuf, lwb->lwb_zio);
			if (error) {
				ASSERT(error == ENOENT || error == EEXIST ||
				    error == EALREADY);
				return (lwb);
			}
		}
	}

	lwb->lwb_nused += reclen + dlen;
	lwb->lwb_max_txg = MAX(lwb->lwb_max_txg, txg);
	ASSERT3U(lwb->lwb_nused, <=, ZIL_BLK_DATA_SZ(lwb));
	ASSERT3U(P2PHASE(lwb->lwb_nused, sizeof (uint64_t)), ==, 0);

	return (lwb);
}

itx_t *
zil_itx_create(uint64_t txtype, size_t lrsize)
{
	itx_t *itx;

	lrsize = P2ROUNDUP_TYPED(lrsize, sizeof (uint64_t), size_t);

	itx = kmem_alloc(offsetof(itx_t, itx_lr) + lrsize, KM_SLEEP);
	itx->itx_lr.lrc_txtype = txtype;
	itx->itx_lr.lrc_reclen = lrsize;
	itx->itx_sod = lrsize; /* if write & WR_NEED_COPY will be increased */
	itx->itx_lr.lrc_seq = 0;	/* defensive */

	return (itx);
}

uint64_t
zil_itx_assign(zilog_t *zilog, itx_t *itx, dmu_tx_t *tx)
{
	uint64_t seq;

	ASSERT(itx->itx_lr.lrc_seq == 0);

	mutex_enter(&zilog->zl_lock);
	list_insert_tail(&zilog->zl_itx_list, itx);
	zilog->zl_itx_list_sz += itx->itx_sod;
	itx->itx_lr.lrc_txg = dmu_tx_get_txg(tx);
	itx->itx_lr.lrc_seq = seq = ++zilog->zl_itx_seq;
	mutex_exit(&zilog->zl_lock);

	return (seq);
}

/*
 * Free up all in-memory intent log transactions that have now been synced.
 */
static void
zil_itx_clean(zilog_t *zilog)
{
	uint64_t synced_txg = spa_last_synced_txg(zilog->zl_spa);
	uint64_t freeze_txg = spa_freeze_txg(zilog->zl_spa);
	list_t clean_list;
	itx_t *itx;

	list_create(&clean_list, sizeof (itx_t), offsetof(itx_t, itx_node));

	mutex_enter(&zilog->zl_lock);
	/* wait for a log writer to finish walking list */
	while (zilog->zl_writer) {
		cv_wait(&zilog->zl_cv_writer, &zilog->zl_lock);
	}

	/*
	 * Move the sync'd log transactions to a separate list so we can call
	 * kmem_free without holding the zl_lock.
	 *
	 * There is no need to set zl_writer as we don't drop zl_lock here
	 */
	while ((itx = list_head(&zilog->zl_itx_list)) != NULL &&
	    itx->itx_lr.lrc_txg <= MIN(synced_txg, freeze_txg)) {
		list_remove(&zilog->zl_itx_list, itx);
		zilog->zl_itx_list_sz -= itx->itx_sod;
		list_insert_tail(&clean_list, itx);
	}
	cv_broadcast(&zilog->zl_cv_writer);
	mutex_exit(&zilog->zl_lock);

	/* destroy sync'd log transactions */
	while ((itx = list_head(&clean_list)) != NULL) {
		list_remove(&clean_list, itx);
		kmem_free(itx, offsetof(itx_t, itx_lr)
		    + itx->itx_lr.lrc_reclen);
	}
	list_destroy(&clean_list);
}

/*
 * If there are any in-memory intent log transactions which have now been
 * synced then start up a taskq to free them.
 */
void
zil_clean(zilog_t *zilog)
{
	itx_t *itx;

	mutex_enter(&zilog->zl_lock);
	itx = list_head(&zilog->zl_itx_list);
	if ((itx != NULL) &&
	    (itx->itx_lr.lrc_txg <= spa_last_synced_txg(zilog->zl_spa))) {
		(void) taskq_dispatch(zilog->zl_clean_taskq,
		    (task_func_t *)zil_itx_clean, zilog, TQ_SLEEP);
	}
	mutex_exit(&zilog->zl_lock);
}

static void
zil_commit_writer(zilog_t *zilog, uint64_t seq, uint64_t foid)
{
	uint64_t txg;
	uint64_t commit_seq = 0;
	itx_t *itx, *itx_next = (itx_t *)-1;
	lwb_t *lwb;
	spa_t *spa;

	zilog->zl_writer = B_TRUE;
	ASSERT(zilog->zl_root_zio == NULL);
	spa = zilog->zl_spa;

	if (zilog->zl_suspend) {
		lwb = NULL;
	} else {
		lwb = list_tail(&zilog->zl_lwb_list);
		if (lwb == NULL) {
			/*
			 * Return if there's nothing to flush before we
			 * dirty the fs by calling zil_create()
			 */
			if (list_is_empty(&zilog->zl_itx_list)) {
				zilog->zl_writer = B_FALSE;
				return;
			}
			mutex_exit(&zilog->zl_lock);
			zil_create(zilog);
			mutex_enter(&zilog->zl_lock);
			lwb = list_tail(&zilog->zl_lwb_list);
		}
	}

	/* Loop through in-memory log transactions filling log blocks. */
	DTRACE_PROBE1(zil__cw1, zilog_t *, zilog);
	for (;;) {
		/*
		 * Find the next itx to push:
		 * Push all transactions related to specified foid and all
		 * other transactions except TX_WRITE, TX_TRUNCATE,
		 * TX_SETATTR and TX_ACL for all other files.
		 */
		if (itx_next != (itx_t *)-1)
			itx = itx_next;
		else
			itx = list_head(&zilog->zl_itx_list);
		for (; itx != NULL; itx = list_next(&zilog->zl_itx_list, itx)) {
			if (foid == 0) /* push all foids? */
				break;
			if (itx->itx_sync) /* push all O_[D]SYNC */
				break;
			switch (itx->itx_lr.lrc_txtype) {
			case TX_SETATTR:
			case TX_WRITE:
			case TX_TRUNCATE:
			case TX_ACL:
				/* lr_foid is same offset for these records */
				if (((lr_write_t *)&itx->itx_lr)->lr_foid
				    != foid) {
					continue; /* skip this record */
				}
			}
			break;
		}
		if (itx == NULL)
			break;

		if ((itx->itx_lr.lrc_seq > seq) &&
		    ((lwb == NULL) || (lwb->lwb_nused == 0) ||
		    (lwb->lwb_nused + itx->itx_sod > ZIL_BLK_DATA_SZ(lwb)))) {
			break;
		}

		/*
		 * Save the next pointer.  Even though we soon drop
		 * zl_lock all threads that may change the list
		 * (another writer or zil_itx_clean) can't do so until
		 * they have zl_writer.
		 */
		itx_next = list_next(&zilog->zl_itx_list, itx);
		list_remove(&zilog->zl_itx_list, itx);
		zilog->zl_itx_list_sz -= itx->itx_sod;
		mutex_exit(&zilog->zl_lock);
		txg = itx->itx_lr.lrc_txg;
		ASSERT(txg);

		if (txg > spa_last_synced_txg(spa) ||
		    txg > spa_freeze_txg(spa))
			lwb = zil_lwb_commit(zilog, itx, lwb);
		kmem_free(itx, offsetof(itx_t, itx_lr)
		    + itx->itx_lr.lrc_reclen);
		mutex_enter(&zilog->zl_lock);
	}
	DTRACE_PROBE1(zil__cw2, zilog_t *, zilog);
	/* determine commit sequence number */
	itx = list_head(&zilog->zl_itx_list);
	if (itx)
		commit_seq = itx->itx_lr.lrc_seq;
	else
		commit_seq = zilog->zl_itx_seq;
	mutex_exit(&zilog->zl_lock);

	/* write the last block out */
	if (lwb != NULL && lwb->lwb_zio != NULL)
		lwb = zil_lwb_write_start(zilog, lwb);

	zilog->zl_prev_used = zilog->zl_cur_used;
	zilog->zl_cur_used = 0;

	/*
	 * Wait if necessary for the log blocks to be on stable storage.
	 */
	if (zilog->zl_root_zio) {
		DTRACE_PROBE1(zil__cw3, zilog_t *, zilog);
		(void) zio_wait(zilog->zl_root_zio);
		zilog->zl_root_zio = NULL;
		DTRACE_PROBE1(zil__cw4, zilog_t *, zilog);
		zil_flush_vdevs(zilog);
	}

	if (zilog->zl_log_error || lwb == NULL) {
		zilog->zl_log_error = 0;
		txg_wait_synced(zilog->zl_dmu_pool, 0);
	}

	mutex_enter(&zilog->zl_lock);
	zilog->zl_writer = B_FALSE;

	ASSERT3U(commit_seq, >=, zilog->zl_commit_seq);
	zilog->zl_commit_seq = commit_seq;
}

/*
 * Push zfs transactions to stable storage up to the supplied sequence number.
 * If foid is 0 push out all transactions, otherwise push only those
 * for that file or might have been used to create that file.
 */
void
zil_commit(zilog_t *zilog, uint64_t seq, uint64_t foid)
{
	if (zilog == NULL || seq == 0)
		return;

	mutex_enter(&zilog->zl_lock);

	seq = MIN(seq, zilog->zl_itx_seq);	/* cap seq at largest itx seq */

	while (zilog->zl_writer) {
		cv_wait(&zilog->zl_cv_writer, &zilog->zl_lock);
		if (seq < zilog->zl_commit_seq) {
			mutex_exit(&zilog->zl_lock);
			return;
		}
	}
	zil_commit_writer(zilog, seq, foid); /* drops zl_lock */
	/* wake up others waiting on the commit */
	cv_broadcast(&zilog->zl_cv_writer);
	mutex_exit(&zilog->zl_lock);
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
	lwb_t *lwb;

	/*
	 * We don't zero out zl_destroy_txg, so make sure we don't try
	 * to destroy it twice.
	 */
	if (spa_sync_pass(spa) != 1)
		return;

	mutex_enter(&zilog->zl_lock);

	ASSERT(zilog->zl_stop_sync == 0);

	zh->zh_replay_seq = zilog->zl_replayed_seq[txg & TXG_MASK];

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
		zio_free_blk(spa, &lwb->lwb_blk, txg);
		kmem_cache_free(zil_lwb_cache, lwb);

		/*
		 * If we don't have anything left in the lwb list then
		 * we've had an allocation failure and we need to zero
		 * out the zil_header blkptr so that we don't end
		 * up freeing the same block twice.
		 */
		if (list_head(&zilog->zl_lwb_list) == NULL)
			BP_ZERO(&zh->zh_log);
	}
	mutex_exit(&zilog->zl_lock);
}

void
zil_init(void)
{
	zil_lwb_cache = kmem_cache_create("zil_lwb_cache",
	    sizeof (struct lwb), 0, NULL, NULL, NULL, NULL, NULL, 0);
}

void
zil_fini(void)
{
	kmem_cache_destroy(zil_lwb_cache);
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

	mutex_init(&zilog->zl_lock, NULL, MUTEX_DEFAULT, NULL);

	list_create(&zilog->zl_itx_list, sizeof (itx_t),
	    offsetof(itx_t, itx_node));

	list_create(&zilog->zl_lwb_list, sizeof (lwb_t),
	    offsetof(lwb_t, lwb_node));

	mutex_init(&zilog->zl_vdev_lock, NULL, MUTEX_DEFAULT, NULL);

	avl_create(&zilog->zl_vdev_tree, zil_vdev_compare,
	    sizeof (zil_vdev_node_t), offsetof(zil_vdev_node_t, zv_node));

	cv_init(&zilog->zl_cv_writer, NULL, CV_DEFAULT, NULL);
	cv_init(&zilog->zl_cv_suspend, NULL, CV_DEFAULT, NULL);

	return (zilog);
}

void
zil_free(zilog_t *zilog)
{
	lwb_t *lwb;

	zilog->zl_stop_sync = 1;

	while ((lwb = list_head(&zilog->zl_lwb_list)) != NULL) {
		list_remove(&zilog->zl_lwb_list, lwb);
		if (lwb->lwb_buf != NULL)
			zio_buf_free(lwb->lwb_buf, lwb->lwb_sz);
		kmem_cache_free(zil_lwb_cache, lwb);
	}
	list_destroy(&zilog->zl_lwb_list);

	avl_destroy(&zilog->zl_vdev_tree);
	mutex_destroy(&zilog->zl_vdev_lock);

	ASSERT(list_head(&zilog->zl_itx_list) == NULL);
	list_destroy(&zilog->zl_itx_list);
	mutex_destroy(&zilog->zl_lock);

	cv_destroy(&zilog->zl_cv_writer);
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

	zilog->zl_get_data = get_data;
	zilog->zl_clean_taskq = taskq_create("zil_clean", 1, minclsyspri,
	    2, 2, TASKQ_PREPOPULATE);

	return (zilog);
}

/*
 * Close an intent log.
 */
void
zil_close(zilog_t *zilog)
{
	/*
	 * If the log isn't already committed, mark the objset dirty
	 * (so zil_sync() will be called) and wait for that txg to sync.
	 */
	if (!zil_is_committed(zilog)) {
		uint64_t txg;
		dmu_tx_t *tx = dmu_tx_create(zilog->zl_os);
		(void) dmu_tx_assign(tx, TXG_WAIT);
		dsl_dataset_dirty(dmu_objset_ds(zilog->zl_os), tx);
		txg = dmu_tx_get_txg(tx);
		dmu_tx_commit(tx);
		txg_wait_synced(zilog->zl_dmu_pool, txg);
	}

	taskq_destroy(zilog->zl_clean_taskq);
	zilog->zl_clean_taskq = NULL;
	zilog->zl_get_data = NULL;

	zil_itx_clean(zilog);
	ASSERT(list_head(&zilog->zl_itx_list) == NULL);
}

/*
 * Suspend an intent log.  While in suspended mode, we still honor
 * synchronous semantics, but we rely on txg_wait_synced() to do it.
 * We suspend the log briefly when taking a snapshot so that the snapshot
 * contains all the data it's supposed to, and has an empty intent log.
 */
int
zil_suspend(zilog_t *zilog)
{
	const zil_header_t *zh = zilog->zl_header;

	mutex_enter(&zilog->zl_lock);
	if (zh->zh_flags & ZIL_REPLAY_NEEDED) {		/* unplayed log */
		mutex_exit(&zilog->zl_lock);
		return (EBUSY);
	}
	if (zilog->zl_suspend++ != 0) {
		/*
		 * Someone else already began a suspend.
		 * Just wait for them to finish.
		 */
		while (zilog->zl_suspending)
			cv_wait(&zilog->zl_cv_suspend, &zilog->zl_lock);
		mutex_exit(&zilog->zl_lock);
		return (0);
	}
	zilog->zl_suspending = B_TRUE;
	mutex_exit(&zilog->zl_lock);

	zil_commit(zilog, UINT64_MAX, 0);

	/*
	 * Wait for any in-flight log writes to complete.
	 */
	mutex_enter(&zilog->zl_lock);
	while (zilog->zl_writer)
		cv_wait(&zilog->zl_cv_writer, &zilog->zl_lock);
	mutex_exit(&zilog->zl_lock);

	zil_destroy(zilog, B_FALSE);

	mutex_enter(&zilog->zl_lock);
	zilog->zl_suspending = B_FALSE;
	cv_broadcast(&zilog->zl_cv_suspend);
	mutex_exit(&zilog->zl_lock);

	return (0);
}

void
zil_resume(zilog_t *zilog)
{
	mutex_enter(&zilog->zl_lock);
	ASSERT(zilog->zl_suspend != 0);
	zilog->zl_suspend--;
	mutex_exit(&zilog->zl_lock);
}

typedef struct zil_replay_arg {
	objset_t	*zr_os;
	zil_replay_func_t **zr_replay;
	void		*zr_arg;
	boolean_t	zr_byteswap;
	char		*zr_lrbuf;
} zil_replay_arg_t;

static void
zil_replay_log_record(zilog_t *zilog, lr_t *lr, void *zra, uint64_t claim_txg)
{
	zil_replay_arg_t *zr = zra;
	const zil_header_t *zh = zilog->zl_header;
	uint64_t reclen = lr->lrc_reclen;
	uint64_t txtype = lr->lrc_txtype;
	char *name;
	int pass, error;

	if (!zilog->zl_replay)			/* giving up */
		return;

	if (lr->lrc_txg < claim_txg)		/* already committed */
		return;

	if (lr->lrc_seq <= zh->zh_replay_seq)	/* already replayed */
		return;

	/* Strip case-insensitive bit, still present in log record */
	txtype &= ~TX_CI;

	if (txtype == 0 || txtype >= TX_MAX_TYPE) {
		error = EINVAL;
		goto bad;
	}

	/*
	 * Make a copy of the data so we can revise and extend it.
	 */
	bcopy(lr, zr->zr_lrbuf, reclen);

	/*
	 * The log block containing this lr may have been byteswapped
	 * so that we can easily examine common fields like lrc_txtype.
	 * However, the log is a mix of different data types, and only the
	 * replay vectors know how to byteswap their records.  Therefore, if
	 * the lr was byteswapped, undo it before invoking the replay vector.
	 */
	if (zr->zr_byteswap)
		byteswap_uint64_array(zr->zr_lrbuf, reclen);

	/*
	 * If this is a TX_WRITE with a blkptr, suck in the data.
	 */
	if (txtype == TX_WRITE && reclen == sizeof (lr_write_t)) {
		lr_write_t *lrw = (lr_write_t *)lr;
		blkptr_t *wbp = &lrw->lr_blkptr;
		uint64_t wlen = lrw->lr_length;
		char *wbuf = zr->zr_lrbuf + reclen;

		if (BP_IS_HOLE(wbp)) {	/* compressed to a hole */
			bzero(wbuf, wlen);
		} else {
			/*
			 * A subsequent write may have overwritten this block,
			 * in which case wbp may have been been freed and
			 * reallocated, and our read of wbp may fail with a
			 * checksum error.  We can safely ignore this because
			 * the later write will provide the correct data.
			 */
			zbookmark_t zb;

			zb.zb_objset = dmu_objset_id(zilog->zl_os);
			zb.zb_object = lrw->lr_foid;
			zb.zb_level = -1;
			zb.zb_blkid = lrw->lr_offset / BP_GET_LSIZE(wbp);

			(void) zio_wait(zio_read(NULL, zilog->zl_spa,
			    wbp, wbuf, BP_GET_LSIZE(wbp), NULL, NULL,
			    ZIO_PRIORITY_SYNC_READ,
			    ZIO_FLAG_CANFAIL | ZIO_FLAG_SPECULATIVE, &zb));
			(void) memmove(wbuf, wbuf + lrw->lr_blkoff, wlen);
		}
	}

	/*
	 * We must now do two things atomically: replay this log record,
	 * and update the log header sequence number to reflect the fact that
	 * we did so. At the end of each replay function the sequence number
	 * is updated if we are in replay mode.
	 */
	for (pass = 1; pass <= 2; pass++) {
		zilog->zl_replaying_seq = lr->lrc_seq;
		/* Only byteswap (if needed) on the 1st pass.  */
		error = zr->zr_replay[txtype](zr->zr_arg, zr->zr_lrbuf,
		    zr->zr_byteswap && pass == 1);

		if (!error)
			return;

		/*
		 * The DMU's dnode layer doesn't see removes until the txg
		 * commits, so a subsequent claim can spuriously fail with
		 * EEXIST. So if we receive any error we try syncing out
		 * any removes then retry the transaction.
		 */
		if (pass == 1)
			txg_wait_synced(spa_get_dsl(zilog->zl_spa), 0);
	}

bad:
	ASSERT(error);
	name = kmem_alloc(MAXNAMELEN, KM_SLEEP);
	dmu_objset_name(zr->zr_os, name);
	cmn_err(CE_WARN, "ZFS replay transaction error %d, "
	    "dataset %s, seq 0x%llx, txtype %llu %s\n",
	    error, name, (u_longlong_t)lr->lrc_seq, (u_longlong_t)txtype,
	    (lr->lrc_txtype & TX_CI) ? "CI" : "");
	zilog->zl_replay = B_FALSE;
	kmem_free(name, MAXNAMELEN);
}

/* ARGSUSED */
static void
zil_incr_blks(zilog_t *zilog, blkptr_t *bp, void *arg, uint64_t claim_txg)
{
	zilog->zl_replay_blks++;
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

	zr.zr_os = os;
	zr.zr_replay = replay_func;
	zr.zr_arg = arg;
	zr.zr_byteswap = BP_SHOULD_BYTESWAP(&zh->zh_log);
	zr.zr_lrbuf = kmem_alloc(2 * SPA_MAXBLOCKSIZE, KM_SLEEP);

	/*
	 * Wait for in-progress removes to sync before starting replay.
	 */
	txg_wait_synced(zilog->zl_dmu_pool, 0);

	zilog->zl_replay = B_TRUE;
	zilog->zl_replay_time = lbolt;
	ASSERT(zilog->zl_replay_blks == 0);
	(void) zil_parse(zilog, zil_incr_blks, zil_replay_log_record, &zr,
	    zh->zh_claim_txg);
	kmem_free(zr.zr_lrbuf, 2 * SPA_MAXBLOCKSIZE);

	zil_destroy(zilog, B_FALSE);
	txg_wait_synced(zilog->zl_dmu_pool, zilog->zl_destroy_txg);
	zilog->zl_replay = B_FALSE;
}

/*
 * Report whether all transactions are committed
 */
int
zil_is_committed(zilog_t *zilog)
{
	lwb_t *lwb;
	int ret;

	mutex_enter(&zilog->zl_lock);
	while (zilog->zl_writer)
		cv_wait(&zilog->zl_cv_writer, &zilog->zl_lock);

	/* recent unpushed intent log transactions? */
	if (!list_is_empty(&zilog->zl_itx_list)) {
		ret = B_FALSE;
		goto out;
	}

	/* intent log never used? */
	lwb = list_head(&zilog->zl_lwb_list);
	if (lwb == NULL) {
		ret = B_TRUE;
		goto out;
	}

	/*
	 * more than 1 log buffer means zil_sync() hasn't yet freed
	 * entries after a txg has committed
	 */
	if (list_next(&zilog->zl_lwb_list, lwb)) {
		ret = B_FALSE;
		goto out;
	}

	ASSERT(zil_empty(zilog));
	ret = B_TRUE;
out:
	cv_broadcast(&zilog->zl_cv_writer);
	mutex_exit(&zilog->zl_lock);
	return (ret);
}

/* ARGSUSED */
int
zil_vdev_offline(char *osname, void *arg)
{
	objset_t *os;
	zilog_t *zilog;
	int error;

	error = dmu_objset_open(osname, DMU_OST_ANY, DS_MODE_USER, &os);
	if (error)
		return (error);

	zilog = dmu_objset_zil(os);
	if (zil_suspend(zilog) != 0)
		error = EEXIST;
	else
		zil_resume(zilog);
	dmu_objset_close(os);
	return (error);
}
