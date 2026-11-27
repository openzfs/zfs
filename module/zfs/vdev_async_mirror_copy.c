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

/* Async mirror: txg replay and deferred reads. */

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
#include <sys/dsl_synctask.h>
#include <sys/dsl_scan.h>
#include <sys/dsl_dataset.h>
#include <sys/dmu_traverse.h>
#include <sys/bptree.h>
#include <sys/bpobj.h>
#include <sys/dmu_objset.h>
#include <sys/dnode.h>
#include <sys/arc.h>
#include <sys/ddt.h>
#include <sys/zio_checksum.h>
#include <sys/zil.h>
#include <sys/zil_impl.h>

static uint_t zfs_vdev_async_mirror_inject_refuse = 0;
static uint_t zfs_vdev_async_mirror_inject_rest = 0;

static uint_t zfs_vdev_async_mirror_defer_max = 16384;

#define	ASYNC_MIRROR_READ_MSGS		32

static int
async_mirror_flush(vdev_async_mirror_t *am)
{
	spa_t *spa = am->am_spa;

	ASSERT(!MUTEX_HELD(&am->am_lock));

	spa_config_enter(spa, SCL_STATE_ALL, am, RW_READER);
	zio_t *pio = zio_root(spa, NULL, NULL, ZIO_FLAG_CANFAIL);
	zio_flush_leaf(pio, am->am_lagging);
	int err = zio_wait(pio);
	spa_config_exit(spa, SCL_STATE_ALL, am);

	async_mirror_progress(am);
	return (err);
}

static int
async_mirror_write_ub(vdev_async_mirror_t *am, uberblock_t *ub)
{
	spa_t *spa = am->am_spa;
	uint64_t good = 0;
	int flags = ZIO_FLAG_CONFIG_WRITER | ZIO_FLAG_CANFAIL;

	ASSERT(!MUTEX_HELD(&am->am_lock));

	spa_config_enter(spa, SCL_STATE_ALL, am, RW_READER);
	zio_t *pio = zio_root(spa, NULL, NULL, flags);
	vdev_uberblock_write(pio, &good, ub, am->am_lagging, flags);
	(void) zio_wait(pio);
	spa_config_exit(spa, SCL_STATE_ALL, am);
	async_mirror_progress(am);

	return (good >= 1 ? 0 : SET_ERROR(EIO));
}

#define	ASYNC_MIRROR_GANG_DEPTH		16

#define	ASYNC_MIRROR_REST_CHUNK		(1ULL << 20)

typedef struct async_mirror_walk async_mirror_walk_t;

typedef struct async_mirror_blk {
	list_node_t	ab_node;	/* in ai_queue */
	async_mirror_io_t *ab_io;
	uint64_t	ab_txg;		/* the txg whose walk added it */
	boolean_t	ab_last;	/* the last try: skip it if lost */
	blkptr_t	ab_bp;
	zbookmark_phys_t ab_zb;
	uint64_t	ab_asize;	/* of the largest copy to write */
	uint64_t	ab_raw;		/* read here from the leader, or -1 */
	uint64_t	ab_off[SPA_DVAS_PER_BP]; /* the copies to write */
	uint64_t	ab_size[SPA_DVAS_PER_BP]; /* and their sizes */
	int		ab_ncopies;
	abd_t		*ab_abd;	/* ab_asize: the block, then zeros */
	arc_buf_t	*ab_buf;	/* the ARC buffer of ab_abd, or NULL */
	uint32_t	ab_writes;	/* writes in flight */
	boolean_t	ab_rest;	/* a range with no block pointer */
} async_mirror_blk_t;

typedef struct async_mirror_root {
	list_node_t	ar_node;
	uint64_t	ar_objset;
	blkptr_t	ar_bp;
	boolean_t	ar_snapshot;
} async_mirror_root_t;

struct async_mirror_io {
	vdev_async_mirror_t *ai_am;
	list_t		ai_queue;	/* the blocks to sort and issue */
	uint64_t	ai_nqueue;
	uint64_t	ai_queue_mem;	/* bytes of memory of the queue */
	zio_t		*ai_rio;	/* the parent of each I/O */
	kmutex_t	ai_lock;
	kcondvar_t	ai_cv;		/* a block ended */
	uint64_t	ai_inflight;	/* bytes of the blocks in flight */
	hrtime_t	ai_progress;	/* when the last block ended */
	uint64_t	ai_copied;	/* bytes written */
	uint64_t	ai_rest_bytes;	/* of them, with no block pointer */
	uint64_t	ai_rest_ranges;	/* writes with no block pointer */
	uint64_t	ai_data_errors;	/* lost data blocks of a log */
	int		ai_write_error;
	boolean_t	ai_giveup;	/* the give-up set ai_write_error */
	int		ai_read_error;	/* a read error, not a lost block */
	uint64_t	ai_read_txg;	/* the oldest txg with such an error */
	list_t		ai_defer;	/* the skipped blocks */
	uint_t		ai_ndefer;
	boolean_t	ai_defer_full;	/* too many blocks to defer */
	uint32_t	ai_nmsg;	/* read messages of this commit */
};

struct async_mirror_walk {
	vdev_async_mirror_t *aw_am;
	async_mirror_io_t *aw_io;
	uint64_t	aw_txg;
	zfs_range_tree_t *aw_alloc;	/* ranges that the txg allocated */
	zfs_range_tree_t *aw_done;	/* ranges written, or skipped */
	boolean_t	aw_bad;		/* a block pointer was refused */
	boolean_t	aw_rest;	/* a range with no block pointer */
	boolean_t	aw_inject;	/* the test suite skipped a block */
	uint64_t	aw_nadd;	/* block pointers added so far */
	uint_t		aw_gang_depth;	/* nesting of the gang block walked */
	list_t		aw_roots;
	uint64_t	aw_objset;	/* of the log being walked */
	int		aw_zil_error;	/* an error of a log callback */
	boolean_t	aw_last;	/* the last try: skip a lost block */
};

static int async_mirror_walk_add(async_mirror_walk_t *, const blkptr_t *,
    const zbookmark_phys_t *);
static int async_mirror_walk_cb(spa_t *, zilog_t *, const blkptr_t *,
    const zbookmark_phys_t *, const dnode_phys_t *, void *);

static boolean_t
async_mirror_bp_is_metadata(const blkptr_t *bp)
{
	return (BP_GET_LEVEL(bp) > 0 || DMU_OT_IS_METADATA(BP_GET_TYPE(bp)));
}

static boolean_t
async_mirror_walk_done(async_mirror_walk_t *aw, uint64_t offset,
    uint64_t size)
{
	uint64_t start, len;

	return (aw->aw_done != NULL && zfs_range_tree_find_in(aw->aw_done,
	    offset, size, &start, &len));
}

static void
async_mirror_walk_note(async_mirror_walk_t *aw, uint64_t offset, uint64_t size)
{
	if (aw->aw_done != NULL)
		zfs_range_tree_add(aw->aw_done, offset, size);
}

static boolean_t
async_mirror_range_ok(vdev_t *tvd, vdev_t *lag, uint64_t offset,
    uint64_t size)
{
	uint64_t end = tvd->vdev_asize;

	if (lag != NULL)
		end = MIN(end, lag->vdev_asize);
	return (size != 0 && offset <= end && size <= end - offset &&
	    size <= vdev_psize_to_asize(tvd, SPA_MAXBLOCKSIZE) &&
	    P2PHASE(offset | size, 1ULL << tvd->vdev_ashift) == 0);
}

static boolean_t
async_mirror_dva_mine(vdev_t *tvd, const dva_t *dva)
{
	return (DVA_GET_VDEV(dva) == tvd->vdev_id && !DVA_GET_GANG(dva));
}

static int
async_mirror_arc_read(spa_t *spa, const blkptr_t *bp,
    const zbookmark_phys_t *zb, arc_buf_t **bufp)
{
	arc_flags_t aflags = ARC_FLAG_WAIT | ARC_FLAG_CACHED_ONLY;

	return (arc_read(NULL, spa, bp, arc_getbuf_func, bufp,
	    ZIO_PRIORITY_ASYNC_READ, ZIO_FLAG_CANFAIL | ZIO_FLAG_RAW |
	    ZIO_FLAG_SPECULATIVE, &aflags, zb));
}

static boolean_t
async_mirror_arc_copy(spa_t *spa, const blkptr_t *bp,
    const zbookmark_phys_t *zb, abd_t *abd)
{
	uint64_t psize = BP_GET_PSIZE(bp);
	arc_buf_t *buf = NULL;
	boolean_t ok = B_FALSE;

	if (BP_GET_CHECKSUM(bp) == ZIO_CHECKSUM_OFF)
		return (B_FALSE);
	if (async_mirror_arc_read(spa, bp, zb, &buf) != 0 || buf == NULL)
		return (B_FALSE);

	if (arc_buf_size(buf) == psize) {
		abd_copy_from_buf(abd, buf->b_data, psize);
		ok = (zio_checksum_verify_bp(spa, bp, abd, psize) == 0);
	}
	arc_buf_destroy(buf, &buf);
	return (ok);
}

static boolean_t
async_mirror_arc_take(spa_t *spa, async_mirror_blk_t *ab)
{
	const blkptr_t *bp = &ab->ab_bp;
	uint64_t psize = BP_GET_PSIZE(bp);
	uint64_t asize = ab->ab_asize;

	if (zio_checksum_table[BP_GET_CHECKSUM(bp)].ci_flags &
	    ZCHECKSUM_FLAG_EMBEDDED)
		return (B_FALSE);
	if (async_mirror_arc_read(spa, bp, &ab->ab_zb, &ab->ab_buf) == 0 &&
	    ab->ab_buf != NULL && arc_buf_size(ab->ab_buf) == psize) {
		abd_t *abd;

		if (psize == asize) {
			abd = abd_get_from_buf(ab->ab_buf->b_data, psize);
		} else {
			abd = abd_alloc_for_io(asize,
			    async_mirror_bp_is_metadata(bp));
			abd_copy_from_buf(abd, ab->ab_buf->b_data, psize);
			abd_zero_off(abd, psize, asize - psize);
		}
		if (zio_checksum_verify_bp(spa, bp, abd, psize) == 0) {
			ab->ab_abd = abd;
			if (psize != asize) {
				arc_buf_destroy(ab->ab_buf, &ab->ab_buf);
				ab->ab_buf = NULL;
			}
			return (B_TRUE);
		}
		abd_free(abd);
	}
	if (ab->ab_buf != NULL) {
		arc_buf_destroy(ab->ab_buf, &ab->ab_buf);
		ab->ab_buf = NULL;
	}
	return (B_FALSE);
}

static void
async_mirror_abd_free_done(zio_t *zio)
{
	abd_free(zio->io_abd);
}

void
async_mirror_defer_list_create(list_t *list)
{
	list_create(list, sizeof (async_mirror_defer_t),
	    offsetof(async_mirror_defer_t, ad_node));
}

void
async_mirror_defer_list_free(list_t *list)
{
	async_mirror_defer_t *ad;

	while ((ad = list_remove_head(list)) != NULL)
		kmem_free(ad, sizeof (*ad));
}

static boolean_t
async_mirror_io_wanted(async_mirror_io_t *io, uint64_t txg)
{
	mutex_enter(&io->ai_lock);
	if (io->ai_write_error == 0 && io->ai_am->am_giveup) {
		io->ai_write_error = SET_ERROR(ENOSPC);
		io->ai_giveup = B_TRUE;
	}
	boolean_t wanted = (io->ai_write_error == 0 &&
	    (io->ai_read_error == 0 || txg < io->ai_read_txg));
	mutex_exit(&io->ai_lock);
	return (wanted);
}

static async_mirror_blk_t *
async_mirror_blk_alloc(async_mirror_walk_t *aw, const blkptr_t *bp,
    const zbookmark_phys_t *zb, uint64_t asize)
{
	async_mirror_blk_t *ab = kmem_zalloc(sizeof (*ab), KM_SLEEP);

	ab->ab_io = aw->aw_io;
	ab->ab_txg = aw->aw_txg;
	ab->ab_last = aw->aw_last;
	if (bp != NULL) {
		ab->ab_bp = *bp;
		ab->ab_zb = *zb;
	} else {
		ab->ab_rest = B_TRUE;
	}
	ab->ab_asize = asize;
	ab->ab_raw = UINT64_MAX;
	return (ab);
}

static void
async_mirror_blk_free(async_mirror_blk_t *ab)
{
	if (ab->ab_abd != NULL)
		abd_free(ab->ab_abd);
	if (ab->ab_buf != NULL)
		arc_buf_destroy(ab->ab_buf, &ab->ab_buf);
	kmem_free(ab, sizeof (*ab));
}

static void
async_mirror_blk_end(async_mirror_blk_t *ab)
{
	async_mirror_io_t *io = ab->ab_io;
	uint64_t asize = ab->ab_asize;

	async_mirror_blk_free(ab);
	spa_config_exit(io->ai_am->am_spa, SCL_STATE_ALL, io);
	async_mirror_progress(io->ai_am);

	mutex_enter(&io->ai_lock);
	ASSERT3U(io->ai_inflight, >=, asize);
	io->ai_inflight -= asize;
	io->ai_progress = gethrtime();
	cv_broadcast(&io->ai_cv);
	mutex_exit(&io->ai_lock);
}

static void
async_mirror_blk_write_done(zio_t *zio)
{
	async_mirror_blk_t *ab = zio->io_private;
	async_mirror_io_t *io = ab->ab_io;

	abd_free(zio->io_abd);
	mutex_enter(&io->ai_lock);
	if (zio->io_error == 0) {
		io->ai_copied += zio->io_size;
		if (ab->ab_rest) {
			io->ai_rest_bytes += zio->io_size;
			io->ai_rest_ranges++;
		}
	} else if (io->ai_write_error == 0) {
		io->ai_write_error = zio->io_error;
	}
	mutex_exit(&io->ai_lock);

	if (atomic_dec_32_nv(&ab->ab_writes) == 0)
		async_mirror_blk_end(ab);
}

static void
async_mirror_blk_write(async_mirror_blk_t *ab)
{
	async_mirror_io_t *io = ab->ab_io;
	vdev_t *lag = io->ai_am->am_lagging;
	int n = ab->ab_ncopies;

	ASSERT3S(n, >, 0);
	ab->ab_writes = n;
	for (int c = 0; c < n; c++) {
		zio_nowait(zio_write_phys(io->ai_rio, lag,
		    ab->ab_off[c] + VDEV_LABEL_START_SIZE, ab->ab_size[c],
		    abd_get_offset_size(ab->ab_abd, 0, ab->ab_size[c]),
		    ZIO_CHECKSUM_OFF, async_mirror_blk_write_done,
		    ab, ZIO_PRIORITY_ASYNC_WRITE, ZIO_FLAG_CANFAIL, B_FALSE));
	}
}

static void
async_mirror_blk_read_done(zio_t *zio)
{
	async_mirror_blk_t *ab = zio->io_private;
	async_mirror_io_t *io = ab->ab_io;
	vdev_async_mirror_t *am = io->ai_am;
	const blkptr_t *bp = &ab->ab_bp;
	const zbookmark_phys_t *zb = &ab->ab_zb;
	int err = zio->io_error;

	if (zio->io_abd != ab->ab_abd)
		abd_free(zio->io_abd);
	if (err == 0) {
		async_mirror_blk_write(ab);
		return;
	}

	boolean_t meta = ab->ab_rest || async_mirror_bp_is_metadata(bp);
	boolean_t lost = !meta && ab->ab_last &&
	    (err == EIO || err == ECKSUM) && vdev_readable(am->am_leader);
	boolean_t full = B_FALSE, first = B_FALSE;

	mutex_enter(&io->ai_lock);
	if (lost && zb->zb_level >= 0 &&
	    io->ai_ndefer >= zfs_vdev_async_mirror_defer_max) {

		lost = B_FALSE;
		full = B_TRUE;
		first = !io->ai_defer_full;
		io->ai_defer_full = B_TRUE;
	}
	if (!lost) {
		if (io->ai_read_error == 0 || ab->ab_txg < io->ai_read_txg) {
			io->ai_read_error = err;
			io->ai_read_txg = ab->ab_txg;
		}
	} else if (zb->zb_level < 0) {
		io->ai_data_errors++;
	} else {
		ASSERT(MUTEX_HELD(&io->ai_lock));
		async_mirror_defer_t *ad = kmem_zalloc(sizeof (*ad), KM_SLEEP);
		atomic_store_32(&am->am_defer_pending, B_TRUE);
		ad->ad_bp = *bp;
		ad->ad_zb = *zb;
		list_insert_tail(&io->ai_defer, ad);
		io->ai_ndefer++;
	}
	mutex_exit(&io->ai_lock);

	if (first) {
		zfs_dbgmsg("async mirror: txg %llu: more than %u data blocks "
		    "cannot be read (zfs_vdev_async_mirror_defer_max); the "
		    "lagging child freezes", (u_longlong_t)ab->ab_txg,
		    zfs_vdev_async_mirror_defer_max);
	}

	uint32_t nmsg = atomic_inc_32_nv(&io->ai_nmsg);

	if (nmsg > ASYNC_MIRROR_READ_MSGS) {

	} else if (ab->ab_rest) {
		zfs_dbgmsg("async mirror: txg %llu: range %llu, %llu bytes, "
		    "with no block pointer cannot be read, error %d",
		    (u_longlong_t)ab->ab_txg, (u_longlong_t)ab->ab_raw,
		    (u_longlong_t)ab->ab_asize, err);
	} else {
		zfs_dbgmsg("async mirror: txg %llu: %s block "
		    "<%llu, %llu, %lld, %llx> cannot be read, error %d%s%s",
		    (u_longlong_t)ab->ab_txg, meta ? "metadata" : "data",
		    (u_longlong_t)zb->zb_objset, (u_longlong_t)zb->zb_object,
		    (longlong_t)zb->zb_level, (u_longlong_t)zb->zb_blkid,
		    err, lost ? "; skipped" : full ? "; too many to skip" : "",
		    nmsg == ASYNC_MIRROR_READ_MSGS ?
		    "; no more of these messages in this commit" : "");
	}
	async_mirror_blk_end(ab);
}

static void
async_mirror_io_reserve(async_mirror_io_t *io, uint64_t size)
{
	spa_t *spa = io->ai_am->am_spa;
	clock_t timeout = MSEC_TO_TICK(zfs_deadman_ziotime_ms);

	mutex_enter(&io->ai_lock);
	while (io->ai_inflight > 0 &&
	    io->ai_inflight + size > zfs_vdev_async_mirror_copy_limit) {
		if (cv_timedwait_io(&io->ai_cv, &io->ai_lock,
		    ddi_get_lbolt() + timeout) == -1 && zfs_deadman_enabled &&
		    gethrtime() - io->ai_progress > spa_deadman_ziotime(spa)) {
			mutex_exit(&io->ai_lock);
			timeout = MSEC_TO_TICK(zfs_deadman_checktime_ms);
			zio_deadman(io->ai_rio, FTAG);
			mutex_enter(&io->ai_lock);
		}
	}
	io->ai_inflight += size;
	mutex_exit(&io->ai_lock);
}

static void
async_mirror_blk_issue(async_mirror_blk_t *ab)
{
	async_mirror_io_t *io = ab->ab_io;
	vdev_async_mirror_t *am = io->ai_am;
	spa_t *spa = am->am_spa;
	const blkptr_t *bp = &ab->ab_bp;
	uint64_t psize = BP_GET_PSIZE(bp);

	async_mirror_io_reserve(io, ab->ab_asize);

	spa_config_enter(spa, SCL_STATE_ALL, io, RW_READER);

	if (ab->ab_abd != NULL ||
	    (ab->ab_raw == UINT64_MAX && async_mirror_arc_take(spa, ab))) {
		async_mirror_blk_write(ab);
		return;
	}

	ab->ab_abd = abd_alloc_for_io(ab->ab_asize,
	    async_mirror_bp_is_metadata(bp));
	if (ab->ab_raw != UINT64_MAX) {
		zio_nowait(zio_read_phys(io->ai_rio, am->am_leader,
		    ab->ab_raw + VDEV_LABEL_START_SIZE, ab->ab_asize,
		    ab->ab_abd, ZIO_CHECKSUM_OFF, async_mirror_blk_read_done,
		    ab, ZIO_PRIORITY_ASYNC_READ, ZIO_FLAG_CANFAIL, B_FALSE));
		return;
	}
	if (ab->ab_asize > psize)
		abd_zero_off(ab->ab_abd, psize, ab->ab_asize - psize);
	zio_nowait(zio_read(io->ai_rio, spa, bp,
	    abd_get_offset_size(ab->ab_abd, 0, psize), psize,
	    async_mirror_blk_read_done, ab, ZIO_PRIORITY_ASYNC_READ,
	    ZIO_FLAG_CANFAIL | ZIO_FLAG_RAW, &ab->ab_zb));
}

static int
async_mirror_blk_compare(const void *x1, const void *x2)
{
	const async_mirror_blk_t *a = *(async_mirror_blk_t * const *)x1;
	const async_mirror_blk_t *b = *(async_mirror_blk_t * const *)x2;

	return (TREE_CMP(a->ab_off[0], b->ab_off[0]));
}

static void
async_mirror_io_drop(async_mirror_io_t *io, uint64_t txg)
{
	async_mirror_blk_t *ab, *next;

	for (ab = list_head(&io->ai_queue); ab != NULL; ab = next) {
		next = list_next(&io->ai_queue, ab);
		if (txg != 0 && ab->ab_txg != txg)
			continue;
		list_remove(&io->ai_queue, ab);
		io->ai_nqueue--;
		io->ai_queue_mem -= sizeof (*ab) + sizeof (ab) +
		    (ab->ab_abd != NULL ? ab->ab_asize : 0);
		async_mirror_blk_free(ab);
	}
}

static void
async_mirror_io_issue(async_mirror_io_t *io)
{
	uint64_t n = io->ai_nqueue;
	async_mirror_blk_t **blks, *ab;
	uint64_t i = 0;

	if (n == 0)
		return;
	blks = vmem_alloc(n * sizeof (*blks), KM_SLEEP);
	while ((ab = list_remove_head(&io->ai_queue)) != NULL)
		blks[i++] = ab;
	ASSERT3U(i, ==, n);
	io->ai_nqueue = 0;
	io->ai_queue_mem = 0;

	qsort(blks, n, sizeof (*blks), async_mirror_blk_compare);
	for (i = 0; i < n; i++) {

		if (!async_mirror_io_wanted(io, blks[i]->ab_txg)) {
			async_mirror_blk_free(blks[i]);
			continue;
		}
		uint_t ms = zfs_vdev_async_mirror_copy_delay_ms;
		if (ms != 0) {
			vdev_async_mirror_t *am = io->ai_am;
			clock_t end = ddi_get_lbolt() + MSEC_TO_TICK(ms);

			mutex_enter(&am->am_lock);
			while (!am->am_stop && !am->am_giveup &&
			    cv_timedwait_idle(&am->am_cv, &am->am_lock,
			    end) > 0)
				;
			mutex_exit(&am->am_lock);
		}
		if (async_mirror_io_wanted(io, blks[i]->ab_txg))
			async_mirror_blk_issue(blks[i]);
		else
			async_mirror_blk_free(blks[i]);
	}
	vmem_free(blks, n * sizeof (*blks));
}

static void
async_mirror_walk_queue(async_mirror_walk_t *aw, async_mirror_blk_t *ab)
{
	async_mirror_io_t *io = aw->aw_io;

	list_insert_tail(&io->ai_queue, ab);
	io->ai_nqueue++;
	io->ai_queue_mem += sizeof (*ab) + sizeof (ab) +
	    (ab->ab_abd != NULL ? ab->ab_asize : 0);

	async_mirror_progress(io->ai_am);
	if (io->ai_queue_mem >= zfs_vdev_async_mirror_sort_limit)
		async_mirror_io_issue(io);
}

static void
async_mirror_io_drain(async_mirror_io_t *io)
{
	vdev_async_mirror_t *am = io->ai_am;
	spa_t *spa = am->am_spa;

	(void) zio_wait(io->ai_rio);
	ASSERT0(io->ai_inflight);
	io->ai_rio = zio_root(spa, NULL, NULL, ZIO_FLAG_CANFAIL);
	io->ai_progress = gethrtime();

	spa_async_mirror_stats_t *sams = &spa->spa_async_mirror_stats;

	mutex_enter(&am->am_lock);
	sams->sams_data_errors += io->ai_data_errors;
	sams->sams_copied_bytes += io->ai_copied;
	sams->sams_copy_rest_bytes += io->ai_rest_bytes;
	sams->sams_copy_rest_ranges += io->ai_rest_ranges;
	async_mirror_update_stats(am);
	mutex_exit(&am->am_lock);
	io->ai_data_errors = 0;
	io->ai_copied = 0;
	io->ai_rest_bytes = 0;
	io->ai_rest_ranges = 0;
}

async_mirror_io_t *
async_mirror_io_create(vdev_async_mirror_t *am)
{
	async_mirror_io_t *io = kmem_zalloc(sizeof (*io), KM_SLEEP);

	io->ai_am = am;
	mutex_init(&io->ai_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&io->ai_cv, NULL, CV_DEFAULT, NULL);
	list_create(&io->ai_queue, sizeof (async_mirror_blk_t),
	    offsetof(async_mirror_blk_t, ab_node));
	async_mirror_defer_list_create(&io->ai_defer);
	io->ai_rio = zio_root(am->am_spa, NULL, NULL, ZIO_FLAG_CANFAIL);
	io->ai_progress = gethrtime();
	return (io);
}

void
async_mirror_io_destroy(async_mirror_io_t *io)
{
	async_mirror_io_drop(io, 0);
	(void) zio_wait(io->ai_rio);
	ASSERT0(io->ai_inflight);
	list_destroy(&io->ai_queue);
	async_mirror_defer_list_free(&io->ai_defer);
	list_destroy(&io->ai_defer);
	cv_destroy(&io->ai_cv);
	mutex_destroy(&io->ai_lock);
	kmem_free(io, sizeof (*io));
}

static int
async_mirror_walk_bad(async_mirror_walk_t *aw, const zbookmark_phys_t *zb,
    const char *why, uint64_t offset, uint64_t size)
{
	int err = SET_ERROR(ECKSUM);

	zfs_dbgmsg("async mirror: txg %llu: block <%llu, %llu, %lld, %llx> "
	    "is refused: %s (offset %llu, %llu bytes); it is not written to "
	    "the lagging child", (u_longlong_t)aw->aw_txg,
	    (u_longlong_t)zb->zb_objset, (u_longlong_t)zb->zb_object,
	    (longlong_t)zb->zb_level, (u_longlong_t)zb->zb_blkid, why,
	    (u_longlong_t)offset, (u_longlong_t)size);
	if (!aw->aw_bad) {
		aw->aw_bad = B_TRUE;
		nvlist_t *nvl = fnvlist_alloc();

		fnvlist_add_uint64(nvl, ZFS_EV_ASYNC_MIRROR_TXG, aw->aw_txg);
		fnvlist_add_int32(nvl, ZFS_EV_ASYNC_MIRROR_ERRNO, err);
		fnvlist_add_uint64(nvl, ZFS_EV_ASYNC_MIRROR_OBJSET,
		    zb->zb_objset);
		fnvlist_add_uint64(nvl, ZFS_EV_ASYNC_MIRROR_OBJECT,
		    zb->zb_object);
		fnvlist_add_int64(nvl, ZFS_EV_ASYNC_MIRROR_LEVEL, zb->zb_level);
		fnvlist_add_uint64(nvl, ZFS_EV_ASYNC_MIRROR_BLKID,
		    zb->zb_blkid);
		fnvlist_add_uint64(nvl, ZFS_EV_ASYNC_MIRROR_OFFSET, offset);
		fnvlist_add_uint64(nvl, ZFS_EV_ASYNC_MIRROR_SIZE, size);
		fnvlist_add_string(nvl, ZFS_EV_ASYNC_MIRROR_REASON, why);
		spa_event_notify(aw->aw_am->am_spa, aw->aw_am->am_leader, nvl,
		    ESC_ZFS_ASYNC_MIRROR_REFUSED);
		fnvlist_free(nvl);
	}
	return (err);
}

static int
async_mirror_walk_gang(async_mirror_walk_t *aw, const blkptr_t *bp,
    const zbookmark_phys_t *zb)
{
	vdev_async_mirror_t *am = aw->aw_am;
	spa_t *spa = am->am_spa;
	vdev_t *tvd = am->am_lagging->vdev_top;
	uint64_t asize = vdev_gang_header_asize(tvd);
	uint64_t gsize = spa_dynamic_gang_header(spa) ?
	    vdev_gang_header_psize(tvd) : SPA_OLD_GANGBLOCKSIZE;
	int err = SET_ERROR(ENXIO);

	ASSERT3U(gsize, <=, asize);

	if (aw->aw_gang_depth >= ASYNC_MIRROR_GANG_DEPTH) {
		return (async_mirror_walk_bad(aw, zb,
		    "its gang blocks are nested too deep",
		    DVA_GET_OFFSET(&bp->blk_dva[0]), asize));
	}

	for (int d = 0; d < BP_GET_NDVAS(bp); d++) {
		const dva_t *dva = &bp->blk_dva[d];
		uint64_t offset = DVA_GET_OFFSET(dva);

		if (DVA_GET_VDEV(dva) != tvd->vdev_id)
			continue;
		if (!async_mirror_range_ok(tvd, am->am_lagging, offset,
		    asize)) {
			return (async_mirror_walk_bad(aw, zb,
			    "a gang header is outside the vdev", offset,
			    asize));
		}
		if (aw->aw_alloc != NULL &&
		    !zfs_range_tree_contains(aw->aw_alloc, offset, asize)) {
			return (async_mirror_walk_bad(aw, zb,
			    "a gang header is not in the record of the txg",
			    offset, asize));
		}
	}

	abd_t *abd = abd_alloc_linear(asize, B_TRUE);

	for (int d = 0; d < BP_GET_NDVAS(bp) && err != 0; d++) {
		const dva_t *dva = &bp->blk_dva[d];

		if (DVA_GET_VDEV(dva) != tvd->vdev_id)
			continue;
		spa_config_enter(spa, SCL_STATE_ALL, am, RW_READER);
		err = zio_wait(zio_read_phys(NULL, am->am_leader,
		    DVA_GET_OFFSET(dva) + VDEV_LABEL_START_SIZE, asize, abd,
		    ZIO_CHECKSUM_OFF, NULL, NULL, ZIO_PRIORITY_ASYNC_READ,
		    ZIO_FLAG_CANFAIL, B_FALSE));
		spa_config_exit(spa, SCL_STATE_ALL, am);
		if (err != 0)
			continue;
		err = zio_checksum_verify_bp(spa, bp, abd, gsize);
		if (err != 0 && gsize > SPA_OLD_GANGBLOCKSIZE &&
		    zio_checksum_verify_bp(spa, bp, abd,
		    SPA_OLD_GANGBLOCKSIZE) == 0) {
			gsize = SPA_OLD_GANGBLOCKSIZE;
			err = 0;
		}
	}
	if (err != 0) {
		zfs_dbgmsg("async mirror: txg %llu: gang header of block "
		    "<%llu, %llu, %lld, %llx> cannot be read, error %d",
		    (u_longlong_t)aw->aw_txg, (u_longlong_t)zb->zb_objset,
		    (u_longlong_t)zb->zb_object, (longlong_t)zb->zb_level,
		    (u_longlong_t)zb->zb_blkid, err);
		abd_free(abd);
		return (err);
	}

	for (int d = 0; d < BP_GET_NDVAS(bp); d++) {
		const dva_t *dva = &bp->blk_dva[d];
		uint64_t offset = DVA_GET_OFFSET(dva);

		if (DVA_GET_VDEV(dva) != tvd->vdev_id ||
		    async_mirror_walk_done(aw, offset, asize))
			continue;

		async_mirror_blk_t *ab = async_mirror_blk_alloc(aw, bp, zb,
		    asize);

		ab->ab_off[ab->ab_ncopies] = offset;
		ab->ab_size[ab->ab_ncopies++] = asize;
		ab->ab_abd = abd_alloc_for_io(asize, B_TRUE);
		abd_copy(ab->ab_abd, abd, asize);
		async_mirror_walk_note(aw, offset, asize);
		async_mirror_walk_queue(aw, ab);
	}

	zio_gbh_phys_t *gbh = abd_to_buf(abd);

	if (BP_SHOULD_BYTESWAP(bp))
		byteswap_uint64_array(gbh, gsize);
	aw->aw_gang_depth++;
	for (int g = 0; g < gbh_nblkptrs(gsize) && err == 0; g++) {
		const blkptr_t *gbp = gbh_bp(gbh, g);

		if (!BP_IS_HOLE(gbp))
			err = async_mirror_walk_add(aw, gbp, zb);
	}
	aw->aw_gang_depth--;
	abd_free(abd);
	return (err);
}

static int
async_mirror_walk_add(async_mirror_walk_t *aw, const blkptr_t *bp,
    const zbookmark_phys_t *zb)
{
	vdev_async_mirror_t *am = aw->aw_am;
	vdev_t *tvd = am->am_lagging->vdev_top;
	async_mirror_blk_t *ab = NULL;
	const char *why;
	uint64_t bad_offset, bad_size;
	mutex_enter(&aw->aw_io->ai_lock);
	int err = (aw->aw_io->ai_write_error != 0) ?
	    aw->aw_io->ai_write_error : aw->aw_io->ai_read_error;
	mutex_exit(&aw->aw_io->ai_lock);
	int big = 0;

	if (err != 0)
		return (err);
	if (zfs_blkptr_verify(am->am_spa, bp, BLK_CONFIG_NEEDED,
	    BLK_VERIFY_LOG) != 0 || BP_IS_HOLE(bp) || BP_IS_EMBEDDED(bp)) {
		why = "the block pointer is not valid";
		bad_offset = DVA_GET_OFFSET(&bp->blk_dva[0]);
		bad_size = DVA_GET_ASIZE(&bp->blk_dva[0]);
		goto bad;
	}

	if (BP_COUNT_GANG(bp) != 0 && BP_COUNT_GANG(bp) != BP_GET_NDVAS(bp)) {
		why = "the block pointer has gang and normal copies";
		bad_offset = DVA_GET_OFFSET(&bp->blk_dva[0]);
		bad_size = DVA_GET_ASIZE(&bp->blk_dva[0]);
		goto bad;
	}

	aw->aw_nadd++;
	if (aw->aw_nadd == zfs_vdev_async_mirror_inject_refuse) {
		why = "the test suite asked for it";
		bad_offset = DVA_GET_OFFSET(&bp->blk_dva[0]);
		bad_size = DVA_GET_ASIZE(&bp->blk_dva[0]);
		goto bad;
	}
	if (aw->aw_nadd == zfs_vdev_async_mirror_inject_rest) {
		aw->aw_inject = B_TRUE;
		return (0);
	}

	boolean_t older = BP_GET_PHYSICAL_BIRTH(bp) <
	    BP_GET_LOGICAL_BIRTH(bp);

	if (BP_IS_GANG(bp) && !older)
		return (async_mirror_walk_gang(aw, bp, zb));

	for (int d = 0; d < BP_GET_NDVAS(bp); d++) {
		const dva_t *dva = &bp->blk_dva[d];
		uint64_t offset = DVA_GET_OFFSET(dva);
		uint64_t size = DVA_GET_ASIZE(dva);
		uint64_t start, len;

		if (!async_mirror_dva_mine(tvd, dva))
			continue;
		why = !async_mirror_range_ok(tvd, am->am_lagging, offset,
		    size) ?
		    "a copy is outside the vdev" :
		    size < BP_GET_PSIZE(bp) ?
		    "a copy is smaller than the block" : NULL;
		if (why == NULL &&
		    zfs_range_tree_contains(aw->aw_alloc, offset, size))
			continue;
		if (why == NULL && (!older ||
		    zfs_range_tree_find_in(aw->aw_alloc, offset, size, &start,
		    &len)))
			why = "a copy is not in the record of the txg";
		if (why != NULL) {
			bad_offset = offset;
			bad_size = size;
			goto bad;
		}
	}

	for (int d = 0; d < BP_GET_NDVAS(bp); d++) {
		const dva_t *dva = &bp->blk_dva[d];
		uint64_t size = DVA_GET_ASIZE(dva);

		if (!async_mirror_dva_mine(tvd, dva) ||
		    !zfs_range_tree_contains(aw->aw_alloc, DVA_GET_OFFSET(dva),
		    size) || async_mirror_walk_done(aw, DVA_GET_OFFSET(dva),
		    size))
			continue;
		if (ab == NULL)
			ab = async_mirror_blk_alloc(aw, bp, zb, 0);
		if (size > ab->ab_asize) {
			ab->ab_asize = size;
			big = ab->ab_ncopies;
		}
		ab->ab_off[ab->ab_ncopies] = DVA_GET_OFFSET(dva);
		ab->ab_size[ab->ab_ncopies++] = size;
		async_mirror_walk_note(aw, DVA_GET_OFFSET(dva), size);
	}
	if (ab == NULL)
		return (0);
	if (BP_GET_CHECKSUM(bp) == ZIO_CHECKSUM_OFF ||
	    BP_GET_TYPE(bp) == DMU_OT_INTENT_LOG)
		ab->ab_raw = ab->ab_off[big];
	async_mirror_walk_queue(aw, ab);
	return (0);

bad:
	return (async_mirror_walk_bad(aw, zb, why, bad_offset, bad_size));
}

static void
async_mirror_walk_root(async_mirror_walk_t *aw, uint64_t objset,
    const blkptr_t *bp, boolean_t snapshot)
{
	if (BP_IS_HOLE(bp))
		return;

	if (BP_GET_PHYSICAL_BIRTH(bp) != aw->aw_txg)
		return;
	for (async_mirror_root_t *ar = list_head(&aw->aw_roots); ar != NULL;
	    ar = list_next(&aw->aw_roots, ar)) {
		if (BP_EQUAL(&ar->ar_bp, bp)) {
			if (!snapshot)
				ar->ar_snapshot = B_FALSE;
			return;
		}
	}

	async_mirror_root_t *ar = kmem_alloc(sizeof (*ar), KM_SLEEP);

	ar->ar_objset = objset;
	ar->ar_bp = *bp;
	ar->ar_snapshot = snapshot;
	list_insert_tail(&aw->aw_roots, ar);
}

static int
async_mirror_walk_bptree(async_mirror_walk_t *aw, blkptr_t *rootbp)
{
	objset_t *os;
	dmu_buf_t *db;
	uint64_t obj;
	int err = dmu_objset_open_impl(aw->aw_am->am_spa, NULL, rootbp, &os);

	if (err != 0)
		return (err);
	err = zap_lookup(os, DMU_POOL_DIRECTORY_OBJECT, DMU_POOL_BPTREE_OBJ,
	    sizeof (uint64_t), 1, &obj);
	if (err == ENOENT) {
		dmu_objset_evict(os);
		return (0);
	}
	if (err == 0)
		err = dmu_bonus_hold(os, obj, FTAG, &db);
	if (err == 0) {
		const bptree_phys_t *bt = db->db_data;
		uint64_t begin = bt->bt_begin, end = bt->bt_end;

		dmu_buf_rele(db, FTAG);
		for (uint64_t i = begin; i < end && err == 0; i++) {
			bptree_entry_phys_t bte;

			err = dmu_read(os, obj, i * sizeof (bte), sizeof (bte),
			    &bte, DMU_READ_NO_PREFETCH);
			if (err == 0) {
				async_mirror_walk_root(aw, bte.be_zb.zb_objset,
				    &bte.be_bp, B_FALSE);
			}
		}
	}
	dmu_objset_evict(os);
	return (err);
}

static int
async_mirror_walk_cb(spa_t *spa, zilog_t *zilog, const blkptr_t *bp,
    const zbookmark_phys_t *zb, const dnode_phys_t *dnp, void *arg)
{
	(void) spa, (void) zilog;
	async_mirror_walk_t *aw = arg;

	if (zb->zb_level == ZB_DNODE_LEVEL) {
		if (zb->zb_objset != DMU_META_OBJSET ||
		    dnp->dn_bonustype != DMU_OT_DSL_DATASET)
			return (0);

		if (dnp->dn_nblkptr != 1 ||
		    dnp->dn_bonuslen < sizeof (dsl_dataset_phys_t) ||
		    dnp->dn_bonuslen >
		    DN_SLOTS_TO_BONUSLEN(dnp->dn_extra_slots + 1)) {
			return (async_mirror_walk_bad(aw, zb,
			    "the dnode of a dataset has another layout",
			    0, 0));
		}

		const dsl_dataset_phys_t *dsp = (const void *)dnp->dn_bonus;

		async_mirror_walk_root(aw, zb->zb_object, &dsp->ds_bp,
		    dsp->ds_num_children != 0);
		return (0);
	}
	if (BP_IS_HOLE(bp) || BP_IS_EMBEDDED(bp) || BP_IS_REDACTED(bp))
		return (0);

	if (BP_GET_PHYSICAL_BIRTH(bp) != aw->aw_txg) {
		if (BP_GET_DEDUP(bp) &&
		    BP_GET_LOGICAL_BIRTH(bp) == aw->aw_txg &&
		    BP_GET_PHYSICAL_BIRTH(bp) < aw->aw_txg)
			return (async_mirror_walk_add(aw, bp, zb));
		return (0);
	}
	if (zb->zb_objset == DMU_META_OBJSET && zb->zb_level == 0 &&
	    BP_GET_TYPE(bp) == DMU_OT_BPOBJ) {
		arc_flags_t aflags = ARC_FLAG_WAIT;
		arc_buf_t *buf = NULL;
		int err = arc_read(NULL, aw->aw_am->am_spa, bp,
		    arc_getbuf_func, &buf, ZIO_PRIORITY_ASYNC_READ,
		    ZIO_FLAG_CANFAIL, &aflags, zb);

		if (err != 0)
			return (err);
		const blkptr_t *bps = buf->b_data;
		uint64_t n = arc_buf_size(buf) / sizeof (blkptr_t);

		for (uint64_t i = 0; i < n && err == 0; i++) {
			if (!BP_IS_HOLE(&bps[i]) && !BP_IS_EMBEDDED(&bps[i]) &&
			    BP_GET_PHYSICAL_BIRTH(&bps[i]) == aw->aw_txg)
				err = async_mirror_walk_add(aw, &bps[i], zb);
		}
		arc_buf_destroy(buf, &buf);
		if (err != 0)
			return (err);
	}
	return (async_mirror_walk_add(aw, bp, zb));
}

static int
async_mirror_walk_zil_blk(zilog_t *zilog, const blkptr_t *bp, void *arg,
    uint64_t claim_txg)
{
	(void) zilog, (void) claim_txg;
	async_mirror_walk_t *aw = arg;
	zbookmark_phys_t zb;

	if (BP_IS_HOLE(bp))
		return (0);
	SET_BOOKMARK(&zb, aw->aw_objset, ZB_ZIL_OBJECT, ZB_ZIL_LEVEL,
	    bp->blk_cksum.zc_word[ZIL_ZC_SEQ]);
	aw->aw_zil_error = async_mirror_walk_add(aw, bp, &zb);
	return (aw->aw_zil_error);
}

static int
async_mirror_walk_zil_lr(zilog_t *zilog, const lr_t *lrc, void *arg,
    uint64_t claim_txg)
{
	(void) zilog;
	async_mirror_walk_t *aw = arg;
	zbookmark_phys_t zb;

	if (lrc->lrc_txtype != TX_WRITE ||
	    lrc->lrc_reclen < sizeof (lr_write_t))
		return (0);

	const lr_write_t *lr = (const lr_write_t *)lrc;
	const blkptr_t *bp = &lr->lr_blkptr;

	if (BP_IS_HOLE(bp) || BP_IS_EMBEDDED(bp) ||
	    BP_GET_BIRTH(bp) < claim_txg)
		return (0);

	SET_BOOKMARK(&zb, aw->aw_objset, lr->lr_foid, ZB_ZIL_LEVEL,
	    lr->lr_offset / BP_GET_LSIZE(bp));
	aw->aw_zil_error = async_mirror_walk_add(aw, bp, &zb);
	return (aw->aw_zil_error);
}

static int
async_mirror_walk_zil(async_mirror_walk_t *aw, const async_mirror_root_t *ar)
{
	spa_t *spa = aw->aw_am->am_spa;
	arc_flags_t aflags = ARC_FLAG_WAIT;
	zio_flag_t flags = ZIO_FLAG_CANFAIL;
	arc_buf_t *buf = NULL;
	zbookmark_phys_t zb;
	zil_header_t zh;

	SET_BOOKMARK(&zb, ar->ar_objset, ZB_ROOT_OBJECT, ZB_ROOT_LEVEL,
	    ZB_ROOT_BLKID);
	if (BP_IS_PROTECTED(&ar->ar_bp))
		flags |= ZIO_FLAG_RAW;

	int err = arc_read(NULL, spa, &ar->ar_bp, arc_getbuf_func, &buf,
	    ZIO_PRIORITY_ASYNC_READ, flags, &aflags, &zb);

	if (err != 0)
		return (err);
	zh = ((objset_phys_t *)buf->b_data)->os_zil_header;
	arc_buf_destroy(buf, &buf);

	if (zh.zh_claim_txg != aw->aw_txg ||
	    BP_IS_HOLE(&zh.zh_log))
		return (0);

	zilog_t *zilog = zil_alloc(spa_meta_objset(spa), &zh);

	aw->aw_objset = ar->ar_objset;
	aw->aw_zil_error = 0;
	err = zil_parse(zilog, async_mirror_walk_zil_blk,
	    async_mirror_walk_zil_lr, aw, zh.zh_claim_txg, B_FALSE);
	if (err != 0 && aw->aw_zil_error == 0 &&
	    zilog->zl_parse_blk_seq == zh.zh_claim_blk_seq)
		err = 0;
	zil_free(zilog);
	return (err);
}

typedef struct async_mirror_rec_arg {
	uint64_t	ara_vdev;
	maptype_t	ara_type;
	boolean_t	ara_clear;
	zfs_range_tree_t *ara_rt;
} async_mirror_rec_arg_t;

static int
async_mirror_rec_cb(space_map_entry_t *sme, void *arg)
{
	async_mirror_rec_arg_t *ara = arg;

	if (sme->sme_vdev != ara->ara_vdev || sme->sme_type != ara->ara_type)
		return (0);
	if (sme->sme_run == 0)
		return (SET_ERROR(ECKSUM));

	zfs_range_tree_clear(ara->ara_rt, sme->sme_offset, sme->sme_run);
	if (!ara->ara_clear)
		zfs_range_tree_add(ara->ara_rt, sme->sme_offset, sme->sme_run);
	return (0);
}

static int
async_mirror_record_ranges(vdev_async_mirror_t *am, async_mirror_txg_t *at,
    maptype_t type, boolean_t clear, zfs_range_tree_t *rt)
{
	async_mirror_rec_arg_t ara = {
		.ara_vdev = am->am_lagging->vdev_top->vdev_id,
		.ara_type = type,
		.ara_clear = clear,
		.ara_rt = rt,
	};
	space_map_t *sm = NULL;
	int err = space_map_open(&sm, spa_meta_objset(am->am_spa), at->at_obj,
	    0, UINT64_MAX, SPA_MINBLOCKSHIFT);

	if (err == 0) {
		err = space_map_iterate(sm, space_map_length(sm),
		    async_mirror_rec_cb, &ara);
		space_map_close(sm);
	}
	return (err);
}

static void
async_mirror_rt_clear_cb(void *arg, uint64_t start, uint64_t size)
{
	zfs_range_tree_clear(arg, start, size);
}

#ifdef ZFS_DEBUG
static void
async_mirror_rest_note_cb(void *arg, uint64_t start, uint64_t size)
{
	zfs_dbgmsg("async mirror: txg %llu: range %llu, %llu bytes",
	    (u_longlong_t)*(uint64_t *)arg, (u_longlong_t)start,
	    (u_longlong_t)size);
}
#endif

static void
async_mirror_walk_rest_cb(void *arg, uint64_t start, uint64_t size)
{
	async_mirror_walk_t *aw = arg;
	vdev_async_mirror_t *am = aw->aw_am;
	vdev_t *tvd = am->am_lagging->vdev_top;

	while (size > 0 && !aw->aw_bad) {
		uint64_t n = MIN(size, ASYNC_MIRROR_REST_CHUNK);

		if (!async_mirror_range_ok(tvd, am->am_lagging, start, n)) {
			zbookmark_phys_t zb = { 0 };

			(void) async_mirror_walk_bad(aw, &zb,
			    "a range of the record is outside the vdev",
			    start, n);
			break;
		}

		async_mirror_blk_t *ab = async_mirror_blk_alloc(aw, NULL,
		    NULL, n);

		ab->ab_raw = start;
		ab->ab_off[ab->ab_ncopies] = start;
		ab->ab_size[ab->ab_ncopies++] = n;
		async_mirror_walk_note(aw, start, n);
		aw->aw_rest = B_TRUE;
		async_mirror_walk_queue(aw, ab);
		start += n;
		size -= n;
	}
}

static int
async_mirror_walk_rest(async_mirror_walk_t *aw, async_mirror_txg_t *at)
{
	vdev_async_mirror_t *am = aw->aw_am;
	zfs_range_tree_t *rest = aw->aw_alloc;

	zfs_range_tree_walk(aw->aw_done, async_mirror_rt_clear_cb, rest);
	if (zfs_range_tree_is_empty(rest))
		return (0);

	int err = async_mirror_record_ranges(am, at, SM_FREE, B_TRUE, rest);

	if (err != 0 || zfs_range_tree_is_empty(rest))
		return (err);

	zfs_dbgmsg("async mirror: txg %llu: no block pointer leads to %llu "
	    "bytes in %llu ranges of the record; they are copied from the "
	    "leader as they are", (u_longlong_t)at->at_txg,
	    (u_longlong_t)zfs_range_tree_space(rest),
	    (u_longlong_t)zfs_range_tree_numsegs(rest));
#ifdef ZFS_DEBUG
	if (!aw->aw_inject && ddt_get_ddt_dsize(am->am_spa) == 0) {
		zfs_range_tree_walk(rest, async_mirror_rest_note_cb,
		    &at->at_txg);
		zfs_panic_recover("async mirror: txg %llu: the walk missed "
		    "%llu bytes of the record", (u_longlong_t)at->at_txg,
		    (u_longlong_t)zfs_range_tree_space(rest));
	}
#endif
	zfs_range_tree_walk(rest, async_mirror_walk_rest_cb, aw);
	return (aw->aw_bad ? SET_ERROR(ECKSUM) : 0);
}

static int
async_mirror_walk(vdev_async_mirror_t *am, async_mirror_io_t *io,
    async_mirror_txg_t *at, boolean_t last, async_mirror_err_t *kindp)
{
	spa_t *spa = am->am_spa;
	int flags = TRAVERSE_POST | TRAVERSE_PREFETCH_METADATA |
	    TRAVERSE_NO_DECRYPT;
	blkptr_t rootbp = at->at_ub.ub_rootbp;
	async_mirror_walk_t aw = {
		.aw_am = am,
		.aw_io = io,
		.aw_txg = at->at_txg,
		.aw_last = last,
	};
	async_mirror_root_t *ar;

	ASSERT(!MUTEX_HELD(&am->am_lock));

	aw.aw_alloc = zfs_range_tree_create(NULL, ZFS_RANGE_SEG64, NULL, 0, 0);
	aw.aw_done = zfs_range_tree_create(NULL, ZFS_RANGE_SEG64, NULL, 0, 0);
	list_create(&aw.aw_roots, sizeof (async_mirror_root_t),
	    offsetof(async_mirror_root_t, ar_node));

	int err = async_mirror_record_ranges(am, at, SM_ALLOC, B_FALSE,
	    aw.aw_alloc);

	if (err == 0 && BP_GET_LOGICAL_BIRTH(&rootbp) == at->at_txg) {
		err = traverse_root(spa, DMU_META_OBJSET, &rootbp,
		    at->at_txg - 1, flags, async_mirror_walk_cb, &aw);
		if (err == 0)
			err = async_mirror_walk_bptree(&aw, &rootbp);
		while (err == 0 &&
		    (ar = list_remove_head(&aw.aw_roots)) != NULL) {
			err = traverse_root(spa, ar->ar_objset, &ar->ar_bp,
			    at->at_txg - 1, flags, async_mirror_walk_cb, &aw);
			if (err == 0 && !ar->ar_snapshot)
				err = async_mirror_walk_zil(&aw, ar);
			kmem_free(ar, sizeof (*ar));

			async_mirror_progress(am);
		}
	}
	if (err == 0)
		err = async_mirror_walk_rest(&aw, at);

	if (err == 0 && aw.aw_rest) {
		async_mirror_io_issue(io);
		async_mirror_io_drain(io);
	}

	while ((ar = list_remove_head(&aw.aw_roots)) != NULL)
		kmem_free(ar, sizeof (*ar));
	list_destroy(&aw.aw_roots);
	zfs_range_tree_vacate(aw.aw_alloc, NULL, NULL);
	zfs_range_tree_destroy(aw.aw_alloc);
	zfs_range_tree_vacate(aw.aw_done, NULL, NULL);
	zfs_range_tree_destroy(aw.aw_done);

	if (err != 0) {

		async_mirror_io_drop(io, at->at_txg);
		async_mirror_io_drain(io);
		mutex_enter(&io->ai_lock);
		async_mirror_defer_list_free(&io->ai_defer);
		io->ai_ndefer = 0;
		io->ai_defer_full = B_FALSE;
		if (io->ai_read_error != 0 && io->ai_read_txg >= at->at_txg) {
			io->ai_read_error = 0;
			io->ai_read_txg = 0;
		}
		*kindp = (io->ai_write_error != 0) ? (io->ai_giveup ?
		    ASYNC_MIRROR_ERR_HELD : ASYNC_MIRROR_ERR_WRITE) :
		    aw.aw_bad ? ASYNC_MIRROR_ERR_BLKPTR : ASYNC_MIRROR_ERR_META;
		mutex_exit(&io->ai_lock);
		if (*kindp == ASYNC_MIRROR_ERR_META) {
			zfs_dbgmsg("async mirror: txg %llu: a metadata block "
			    "cannot be read, error %d",
			    (u_longlong_t)at->at_txg, err);
		}
	}
	return (err);
}

typedef struct async_mirror_defer_arg {
	vdev_async_mirror_t	*ada_am;
	uint64_t		ada_txg;
	list_t			*ada_def;
} async_mirror_defer_arg_t;

static void
async_mirror_defer_sync(void *arg, dmu_tx_t *tx)
{
	async_mirror_defer_arg_t *ada = arg;
	vdev_async_mirror_t *am = ada->ada_am;
	objset_t *mos = spa_meta_objset(am->am_spa);

	ASSERT(dsl_pool_sync_context(dmu_objset_pool(mos)));
	ASSERT3U(am->am_zap_obj, !=, 0);

	if (am->am_defer_zap == 0) {
		uint64_t zap = zap_create(mos, DMU_OTN_ZAP_METADATA,
		    DMU_OT_NONE, 0, tx);

		VERIFY0(zap_add(mos, DMU_POOL_DIRECTORY_OBJECT,
		    DMU_POOL_ASYNC_MIRROR_DEFERRED, sizeof (uint64_t), 1,
		    &zap, tx));
		mutex_enter(&am->am_lock);
		am->am_defer_zap = zap;
		mutex_exit(&am->am_lock);
	}
	for (async_mirror_defer_t *ad = list_head(ada->ada_def); ad != NULL;
	    ad = list_next(ada->ada_def, ad)) {
		uint64_t val[ASYNC_MIRROR_DEFER_WORDS];
		char name[96];

		val[0] = ada->ada_txg;
		memcpy(&val[1], &ad->ad_bp, sizeof (blkptr_t));
		memcpy(&val[1 + sizeof (blkptr_t) / sizeof (uint64_t)],
		    &ad->ad_zb, sizeof (zbookmark_phys_t));
		(void) snprintf(name, sizeof (name), "%llx-%llx-%llx",
		    (u_longlong_t)ada->ada_txg,
		    (u_longlong_t)DVA_GET_VDEV(&ad->ad_bp.blk_dva[0]),
		    (u_longlong_t)DVA_GET_OFFSET(&ad->ad_bp.blk_dva[0]));
		VERIFY0(zap_update(mos, am->am_defer_zap, name,
		    sizeof (uint64_t), ASYNC_MIRROR_DEFER_WORDS, val, tx));
	}
}

static int
async_mirror_defer_persist(vdev_async_mirror_t *am, async_mirror_txg_t *at,
    list_t *def, uint_t n)
{
	spa_t *spa = am->am_spa;
	dsl_pool_t *dp = spa_get_dsl(spa);
	async_mirror_defer_arg_t ada = {
		.ada_am = am,
		.ada_txg = at->at_txg,
		.ada_def = def
	};
	int err = 0;

	ASSERT(!MUTEX_HELD(&am->am_lock));
	ASSERT3U(n, >, 0);

	mutex_enter(&am->am_lock);
	am->am_persist = AMP_WAIT;
	cv_broadcast(&am->am_drain_cv);
	while ((am->am_quiesce > 0 || !spa->spa_sync_on) && !am->am_stop) {
		(void) cv_timedwait_idle(&am->am_cv, &am->am_lock,
		    ddi_get_lbolt() + hz / 10 + 1);
	}
	if (am->am_stop)
		err = SET_ERROR(ECANCELED);
	else
		am->am_persist = AMP_SYNC;
	mutex_exit(&am->am_lock);

	if (err == 0) {
		dmu_tx_t *tx = dmu_tx_create_dd(dp->dp_mos_dir);

		err = dmu_tx_assign(tx, DMU_TX_WAIT | DMU_TX_SUSPEND);
		if (err == 0) {
			uint64_t txg = dmu_tx_get_txg(tx);

			dsl_sync_task_nowait(dp, async_mirror_defer_sync, &ada,
			    tx);
			dmu_tx_commit(tx);
			txg_wait_synced(dp, txg);

			if (txg > spa_freeze_txg(spa))
				err = SET_ERROR(ECANCELED);
		} else {

			dmu_tx_abort(tx);
			err = SET_ERROR(ECANCELED);
		}
	}

	mutex_enter(&am->am_lock);
	am->am_persist = AMP_NONE;
	cv_broadcast(&am->am_drain_cv);
	if (err == 0) {
		ASSERT(list_is_empty(&at->at_defer));
		list_move_tail(&at->at_defer, def);
		at->at_ndefer_count = n;
		at->at_ndefer = n;
		at->at_defer_tries = 0;
		spa->spa_async_mirror_stats.sams_data_deferred += n;
		async_mirror_defer_floor(am);
		atomic_store_32(&am->am_defer_pending, B_FALSE);
		am->am_defer_quick = B_TRUE;
		async_mirror_update_stats(am);
	}
	mutex_exit(&am->am_lock);

	if (err != 0) {
		async_mirror_defer_list_free(def);
		zfs_dbgmsg("async mirror: txg %llu: the deferred blocks are "
		    "not durable, error %d", (u_longlong_t)ada.ada_txg, err);
	}
	return (err);
}

int
async_mirror_copy(vdev_async_mirror_t *am, async_mirror_io_t *io,
    async_mirror_txg_t *at, boolean_t last, async_mirror_err_t *kindp)
{
	int err = 0;

	ASSERT(!MUTEX_HELD(&am->am_lock));
	ASSERT(at->at_ready);
	ASSERT3U(at->at_txg, >, am->am_applied_txg);

	*kindp = ASYNC_MIRROR_ERR_WRITE;
	if (at->at_bytes > 0)
		err = async_mirror_walk(am, io, at, last, kindp);
	return (err);
}

int
async_mirror_copy_wait(async_mirror_io_t *io, async_mirror_err_t *kindp,
    uint64_t *txgp, list_t *def, uint_t *ndefp)
{
	int err;

	ASSERT(!MUTEX_HELD(&io->ai_am->am_lock));
	ASSERT(list_is_empty(def));

	async_mirror_io_issue(io);
	async_mirror_io_drain(io);

	mutex_enter(&io->ai_lock);
	*kindp = io->ai_giveup ? ASYNC_MIRROR_ERR_HELD :
	    ASYNC_MIRROR_ERR_WRITE;
	*txgp = 0;
	*ndefp = 0;
	err = io->ai_write_error;
	if (err == 0 && io->ai_read_error != 0) {
		err = io->ai_read_error;
		*kindp = ASYNC_MIRROR_ERR_META;
		*txgp = io->ai_read_txg;
	}
	if (err == 0) {
		list_move_tail(def, &io->ai_defer);
		*ndefp = io->ai_ndefer;
	} else {
		async_mirror_defer_list_free(&io->ai_defer);
	}
	io->ai_ndefer = 0;
	io->ai_defer_full = B_FALSE;
	io->ai_nmsg = 0;
	io->ai_write_error = 0;
	io->ai_giveup = B_FALSE;
	io->ai_read_error = 0;
	io->ai_read_txg = 0;
	mutex_exit(&io->ai_lock);
	return (err);
}

int
async_mirror_commit(vdev_async_mirror_t *am, async_mirror_txg_t *at,
    boolean_t wrote, list_t *def, uint_t ndef, async_mirror_err_t *kindp)
{
	int err = 0;

	ASSERT(!MUTEX_HELD(&am->am_lock));
	ASSERT(at->at_ready);

	*kindp = ASYNC_MIRROR_ERR_WRITE;
	if (wrote)
		err = async_mirror_flush(am);

	if (ndef > 0 && err == 0) {
		err = async_mirror_defer_persist(am, at, def, ndef);
		if (err == ECANCELED)
			*kindp = ASYNC_MIRROR_ERR_STOP;
	}
	async_mirror_defer_list_free(def);

	if (err == 0) {
		*kindp = ASYNC_MIRROR_ERR_WRITE;
		err = async_mirror_write_ub(am, &at->at_ub);
		if (err == 0)
			err = async_mirror_flush(am);
	}
	return (err);
}

void
async_mirror_retry_event(vdev_async_mirror_t *am, uint64_t txg, int err)
{
	nvlist_t *nvl = fnvlist_alloc();

	fnvlist_add_uint64(nvl, ZFS_EV_ASYNC_MIRROR_TXG, txg);
	fnvlist_add_int32(nvl, ZFS_EV_ASYNC_MIRROR_ERRNO, err);
	fnvlist_add_uint64(nvl, ZFS_EV_ASYNC_MIRROR_RETRIES,
	    zfs_vdev_async_mirror_read_retries);
	fnvlist_add_uint64(nvl, ZFS_EV_ASYNC_MIRROR_DELAY_MS,
	    ASYNC_MIRROR_RETRY_MS);
	spa_event_notify(am->am_spa, am->am_leader, nvl,
	    ESC_ZFS_ASYNC_MIRROR_RETRY);
	fnvlist_free(nvl);
}

boolean_t
async_mirror_retry_wait(vdev_async_mirror_t *am)
{
	clock_t end = ddi_get_lbolt() +
	    MSEC_TO_TICK(ASYNC_MIRROR_RETRY_MS);

	mutex_enter(&am->am_lock);
	am->am_spa->spa_async_mirror_stats.sams_read_retries++;
	async_mirror_update_stats(am);
	while (!am->am_stop &&
	    cv_timedwait_idle(&am->am_cv, &am->am_lock, end) > 0)
		;
	boolean_t go = !am->am_stop;

	async_mirror_progress(am);
	mutex_exit(&am->am_lock);
	return (go);
}

void
async_mirror_halt(vdev_async_mirror_t *am, uint64_t txg, int err,
    async_mirror_err_t kind)
{
	ASSERT(MUTEX_HELD(&am->am_lock));
	ASSERT3S(err, !=, 0);
	ASSERT3U(kind, !=, ASYNC_MIRROR_ERR_STOP);
	ASSERT(async_mirror_copying(am->am_state));

	boolean_t freeze = (kind != ASYNC_MIRROR_ERR_WRITE);
	boolean_t giveup = (kind == ASYNC_MIRROR_ERR_HELD);
	uint_t why = !giveup ? AMG_NONE :
	    (am->am_giveup && am->am_giveup_why != AMG_NONE) ?
	    am->am_giveup_why : AMG_HELD;

	const char *reason = giveup ? async_mirror_giveup_text(why) :
	    kind == ASYNC_MIRROR_ERR_WRITE ? "write error" :
	    kind == ASYNC_MIRROR_ERR_BLKPTR ? "block pointer refused" :
	    "read error";

	am->am_giveup = giveup;
	am->am_giveup_why = why;
	am->am_freeze_read = (freeze && !giveup &&
	    kind == ASYNC_MIRROR_ERR_META);
	if (giveup) {
		zfs_dbgmsg("async mirror: %s; the lagging child guid %llu is "
		    "frozen at txg %llu", reason,
		    (u_longlong_t)am->am_lagging->vdev_guid,
		    (u_longlong_t)am->am_applied_txg);
	} else if (freeze) {
		zfs_dbgmsg("async mirror: copy of txg %llu: a block %s, "
		    "error %d; the lagging child guid %llu is frozen at "
		    "txg %llu", (u_longlong_t)txg,
		    kind == ASYNC_MIRROR_ERR_BLKPTR ? "pointer is refused" :
		    "cannot be read", err,
		    (u_longlong_t)am->am_lagging->vdev_guid,
		    (u_longlong_t)am->am_applied_txg);
	} else {
		zfs_dbgmsg("async mirror: copy of txg %llu failed, error %d",
		    (u_longlong_t)txg, err);
	}

	am->am_fail_open_txg = spa_last_synced_txg(am->am_spa) +
	    TXG_CONCURRENT_STATES;
	am->am_fail_from = am->am_applied_txg + 1;
	if (am->am_defer_txg != 0)
		am->am_fail_from = MIN(am->am_fail_from, am->am_defer_txg);
	if (am->am_frozen_from != 0)
		am->am_fail_from = MIN(am->am_fail_from, am->am_frozen_from);
	if (freeze) {
		am->am_frozen_from = am->am_fail_from;
		async_mirror_freeze_note(am);
	}
	async_mirror_set_state(am, freeze ? AMS_FROZEN : AMS_FAILED);
	async_mirror_update_stats(am);
	spa_t *spa = am->am_spa;
	uint64_t lagging_guid = am->am_lagging->vdev_guid;
	uint64_t applied_txg = am->am_applied_txg;
	if (am->am_release_zthr != NULL) {
		mutex_exit(&am->am_lock);
		zthr_wakeup(am->am_release_zthr);
		mutex_enter(&am->am_lock);
	}

	mutex_exit(&am->am_lock);
	nvlist_t *nvl = fnvlist_alloc();
	fnvlist_add_uint64(nvl, ZFS_EV_ASYNC_MIRROR_TXG, txg);
	fnvlist_add_int32(nvl, ZFS_EV_ASYNC_MIRROR_ERRNO, err);
	fnvlist_add_uint64(nvl, ZFS_EV_VDEV_GUID, lagging_guid);
	fnvlist_add_uint64(nvl, ZFS_EV_ASYNC_MIRROR_APPLIED_TXG,
	    applied_txg);
	fnvlist_add_string(nvl, ZFS_EV_ASYNC_MIRROR_REASON, reason);
	spa_event_notify(spa, NULL, nvl, freeze ?
	    ESC_ZFS_ASYNC_MIRROR_FROZEN : ESC_ZFS_ASYNC_MIRROR_FAILED);
	fnvlist_free(nvl);
	mutex_enter(&am->am_lock);
}

void
async_mirror_defer_floor(vdev_async_mirror_t *am)
{
	ASSERT(MUTEX_HELD(&am->am_lock));

	am->am_defer_txg = 0;
	for (async_mirror_txg_t *at = list_head(&am->am_txgs); at != NULL;
	    at = list_next(&am->am_txgs, at)) {
		if (at->at_ndefer > 0) {
			am->am_defer_txg = at->at_txg;
			break;
		}
	}
	if (am->am_defer_txg == 0)
		am->am_defer_quick = B_FALSE;
	async_mirror_publish_txg(am);
}

void
async_mirror_defer_drop(vdev_async_mirror_t *am, async_mirror_txg_t *at)
{
	spa_async_mirror_stats_t *sams = &am->am_spa->spa_async_mirror_stats;
	boolean_t frozen = (am->am_state == AMS_FROZEN);

	ASSERT(MUTEX_HELD(&am->am_lock));
	ASSERT(async_mirror_draining(am->am_state));

	zfs_dbgmsg("async mirror: txg %llu: %u deferred data blocks %s",
	    (u_longlong_t)at->at_txg, at->at_ndefer, frozen ?
	    "are left to the end of the freeze of the lagging child" :
	    "are left to the resilver");
	ASSERT3U(sams->sams_data_deferred, >=, at->at_ndefer);
	sams->sams_data_deferred -= at->at_ndefer;
	at->at_ndefer = 0;
	async_mirror_defer_floor(am);
	async_mirror_update_stats(am);
}

void
async_mirror_defer_clear(vdev_async_mirror_t *am, uint64_t txg, dmu_tx_t *tx)
{
	objset_t *mos = spa_meta_objset(am->am_spa);
	zap_attribute_t *za = zap_attribute_alloc();
	nvlist_t *names = fnvlist_alloc();
	zap_cursor_t zc;
	uint64_t count = 0;

	ASSERT(dsl_pool_sync_context(dmu_objset_pool(mos)));
	ASSERT3U(am->am_defer_zap, !=, 0);

	for (zap_cursor_init(&zc, mos, am->am_defer_zap);
	    zap_cursor_retrieve(&zc, za) == 0; zap_cursor_advance(&zc)) {
		if (zfs_strtonum(za->za_name, NULL) <= txg)
			fnvlist_add_boolean(names, za->za_name);
	}
	zap_cursor_fini(&zc);
	zap_attribute_free(za);

	for (nvpair_t *nvp = nvlist_next_nvpair(names, NULL); nvp != NULL;
	    nvp = nvlist_next_nvpair(names, nvp)) {
		VERIFY0(zap_remove(mos, am->am_defer_zap, nvpair_name(nvp),
		    tx));
	}
	fnvlist_free(names);

	VERIFY0(zap_count(mos, am->am_defer_zap, &count));
	if (count == 0) {
		VERIFY0(zap_destroy(mos, am->am_defer_zap, tx));
		VERIFY0(zap_remove(mos, DMU_POOL_DIRECTORY_OBJECT,
		    DMU_POOL_ASYNC_MIRROR_DEFERRED, tx));
		mutex_enter(&am->am_lock);
		am->am_defer_zap = 0;
		mutex_exit(&am->am_lock);
	}
}

async_mirror_txg_t *
async_mirror_defer_next(vdev_async_mirror_t *am)
{
	ASSERT(MUTEX_HELD(&am->am_lock));

	if (!async_mirror_copying(am->am_state) || am->am_defer_failed)
		return (NULL);
	for (async_mirror_txg_t *at = list_head(&am->am_txgs); at != NULL &&
	    at->at_ready && at->at_txg <= am->am_applied_txg;
	    at = list_next(&am->am_txgs, at)) {
		if (at->at_ndefer > 0)
			return (at);
	}
	return (NULL);
}

typedef enum async_mirror_defer_result {
	ASYNC_MIRROR_DEFER_NONE = 0,
	ASYNC_MIRROR_DEFER_READ,
	ASYNC_MIRROR_DEFER_RECOVERED,
	ASYNC_MIRROR_DEFER_LOST,
} async_mirror_defer_result_t;

#define	ASYNC_MIRROR_DEFER_RECS		64
#define	ASYNC_MIRROR_DEFER_BATCH	256
#define	ASYNC_MIRROR_DEFER_BYTES	(64ULL << 20)

typedef struct async_mirror_defer_rec {
	uint64_t	dr_txg;
	uint_t		dr_tries;	/* its tries before this one */
	uint_t		dr_first;	/* its first block in the copy */
	uint_t		dr_count;	/* its blocks in the copy */
} async_mirror_defer_rec_t;

typedef struct async_mirror_defer_try {
	async_mirror_defer_rec_t *dt_recs;
	uint_t		dt_nrecs;
	async_mirror_defer_t *dt_def;	/* the copy of their blocks */
	uint8_t		*dt_got;	/* async_mirror_defer_result_t */
	abd_t		**dt_abds;	/* the bytes read of each block */
	uint_t		dt_n;		/* blocks in dt_def */
	vdev_t		*dt_tvd;	/* the mirror of the lagging child */
} async_mirror_defer_try_t;

static void
async_mirror_defer_try_init(vdev_async_mirror_t *am, async_mirror_txg_t *first,
    async_mirror_defer_try_t *dt)
{
	uint_t nrec = 0, n = 0;

	ASSERT(MUTEX_HELD(&am->am_lock));

	for (async_mirror_txg_t *at = first; at != NULL && at->at_ready &&
	    at->at_txg <= am->am_applied_txg;
	    at = list_next(&am->am_txgs, at)) {
		if (at->at_ndefer == 0)
			continue;
		if (nrec > 0 && (nrec == ASYNC_MIRROR_DEFER_RECS ||
		    n + at->at_ndefer_count > ASYNC_MIRROR_DEFER_BATCH))
			break;
		nrec++;
		n += at->at_ndefer_count;
	}

	dt->dt_recs = kmem_alloc(nrec * sizeof (*dt->dt_recs), KM_SLEEP);
	dt->dt_nrecs = nrec;
	dt->dt_def = vmem_alloc(n * sizeof (*dt->dt_def), KM_SLEEP);
	dt->dt_got = kmem_zalloc(n * sizeof (uint8_t), KM_SLEEP);
	dt->dt_abds = kmem_zalloc(n * sizeof (abd_t *), KM_SLEEP);
	dt->dt_n = n;
	dt->dt_tvd = am->am_lagging->vdev_top;

	uint_t r = 0, k = 0;

	for (async_mirror_txg_t *at = first; r < nrec;
	    at = list_next(&am->am_txgs, at)) {
		if (at->at_ndefer == 0)
			continue;
		dt->dt_recs[r].dr_txg = at->at_txg;
		dt->dt_recs[r].dr_tries = at->at_defer_tries;
		dt->dt_recs[r].dr_first = k;
		dt->dt_recs[r].dr_count = at->at_ndefer_count;
		for (async_mirror_defer_t *ad = list_head(&at->at_defer);
		    ad != NULL; ad = list_next(&at->at_defer, ad)) {
			dt->dt_def[k].ad_bp = ad->ad_bp;
			dt->dt_def[k].ad_zb = ad->ad_zb;
			dt->dt_def[k].ad_done = ad->ad_done;
			k++;
		}
		r++;
	}
	ASSERT3U(k, ==, n);
}

static uint_t
async_mirror_defer_try_read(vdev_async_mirror_t *am,
    async_mirror_defer_try_t *dt, uint_t *posp)
{
	uint_t nread = 0, i = *posp, r = 0;
	uint64_t bytes = 0;

	for (; i < dt->dt_n && nread < ASYNC_MIRROR_DEFER_BATCH &&
	    bytes < ASYNC_MIRROR_DEFER_BYTES; i++) {
		while (i >= dt->dt_recs[r].dr_first + dt->dt_recs[r].dr_count)
			r++;

		const async_mirror_defer_rec_t *dr = &dt->dt_recs[r];
		const async_mirror_defer_t *ad = &dt->dt_def[i];
		boolean_t last = (dr->dr_tries >=
		    zfs_vdev_async_mirror_read_retries);

		if (ad->ad_done)
			continue;
		boolean_t valid = (zfs_blkptr_verify(am->am_spa, &ad->ad_bp,
		    BLK_CONFIG_NEEDED, BLK_VERIFY_LOG) == 0 &&
		    !BP_IS_HOLE(&ad->ad_bp) && !BP_IS_EMBEDDED(&ad->ad_bp));
		if (valid) {
			for (int d = 0; d < BP_GET_NDVAS(&ad->ad_bp); d++) {
				const dva_t *dva = &ad->ad_bp.blk_dva[d];
				uint64_t size = DVA_GET_ASIZE(dva);

				if (async_mirror_dva_mine(dt->dt_tvd, dva) &&
				    (size < BP_GET_PSIZE(&ad->ad_bp) ||
				    !async_mirror_range_ok(dt->dt_tvd, NULL,
				    DVA_GET_OFFSET(dva), size))) {
					valid = B_FALSE;
					break;
				}
			}
		}
		if (!valid) {
			zfs_dbgmsg("async mirror: txg %llu: the block "
			    "pointer of a deferred data block is not "
			    "valid", (u_longlong_t)dr->dr_txg);
			continue;
		}

		uint64_t psize = BP_GET_PSIZE(&ad->ad_bp);
		uint64_t asize = vdev_psize_to_asize(dt->dt_tvd, psize);
		for (int d = 0; d < BP_GET_NDVAS(&ad->ad_bp); d++) {
			const dva_t *dva = &ad->ad_bp.blk_dva[d];

			if (async_mirror_dva_mine(dt->dt_tvd, dva))
				asize = MAX(asize, DVA_GET_ASIZE(dva));
		}
		abd_t *abd = abd_alloc_for_io(asize, B_FALSE);

		if (asize > psize)
			abd_zero_off(abd, psize, asize - psize);
		int err = 0;

		if (!async_mirror_arc_copy(am->am_spa, &ad->ad_bp,
		    &ad->ad_zb, abd)) {
			zio_flag_t flags = ZIO_FLAG_CANFAIL | ZIO_FLAG_RAW;

			if (!last)
				flags |= ZIO_FLAG_SPECULATIVE;
			abd_t *pabd = abd_get_offset_size(abd, 0, psize);
			err = zio_wait(zio_read(NULL, am->am_spa, &ad->ad_bp,
			    pabd, psize, NULL, NULL, ZIO_PRIORITY_ASYNC_READ,
			    flags, &ad->ad_zb));
			abd_free(pabd);
		}
		if (err == 0) {
			dt->dt_abds[i] = abd;
			dt->dt_got[i] = ASYNC_MIRROR_DEFER_READ;
			bytes += asize;
			nread++;
		} else {
			abd_free(abd);
		}
	}
	*posp = i;
	return (nread);
}

static int
async_mirror_defer_try_write(vdev_async_mirror_t *am,
    async_mirror_defer_try_t *dt, uint_t lo, uint_t hi, boolean_t *wrotep)
{
	spa_t *spa = am->am_spa;
	vdev_t *tvd = am->am_lagging->vdev_top;
	int err = 0;

	spa_config_enter(spa, SCL_STATE_ALL, am, RW_READER);
	if (!*wrotep) {
		mutex_enter(&am->am_lock);
		*wrotep = !async_mirror_draining(am->am_state);
		if (*wrotep)
			am->am_defer_io = B_TRUE;
		mutex_exit(&am->am_lock);
	}

	if (*wrotep) {
		zio_t *pio = zio_root(spa, NULL, NULL, ZIO_FLAG_CANFAIL);

		for (uint_t i = lo; i < hi; i++) {
			if (dt->dt_got[i] != ASYNC_MIRROR_DEFER_READ)
				continue;
			const blkptr_t *bp = &dt->dt_def[i].ad_bp;
			uint64_t asize = abd_get_size(dt->dt_abds[i]);

			for (int d = 0; d < BP_GET_NDVAS(bp); d++) {
				const dva_t *dva = &bp->blk_dva[d];
				uint64_t size = DVA_GET_ASIZE(dva);

				if (!async_mirror_dva_mine(tvd, dva) ||
				    size > asize || !async_mirror_range_ok(tvd,
				    am->am_lagging, DVA_GET_OFFSET(dva), size))
					continue;
				abd_t *pabd = abd_get_offset_size(
				    dt->dt_abds[i], 0, size);

				zio_nowait(zio_write_phys(pio, am->am_lagging,
				    DVA_GET_OFFSET(dva) + VDEV_LABEL_START_SIZE,
				    size, pabd,
				    ZIO_CHECKSUM_OFF,
				    async_mirror_abd_free_done,
				    NULL, ZIO_PRIORITY_ASYNC_WRITE,
				    ZIO_FLAG_CANFAIL, B_FALSE));
			}
		}
		err = zio_wait(pio);
		if (err == 0) {
			pio = zio_root(spa, NULL, NULL, ZIO_FLAG_CANFAIL);
			zio_flush_leaf(pio, am->am_lagging);
			err = zio_wait(pio);
		}
	}
	spa_config_exit(spa, SCL_STATE_ALL, am);
	return (err);
}

static uint_t
async_mirror_defer_try_account(vdev_async_mirror_t *am,
    async_mirror_defer_try_t *dt, boolean_t wrote, int err)
{
	spa_async_mirror_stats_t *sams = &am->am_spa->spa_async_mirror_stats;
	uint_t recovered = 0, lost = 0, left = 0;

	mutex_enter(&am->am_lock);
	if (wrote) {
		am->am_defer_io = B_FALSE;
		cv_broadcast(&am->am_drain_cv);
	}
	if (err != 0) {

		zfs_dbgmsg("async mirror: txg %llu: write of deferred data "
		    "blocks failed, error %d",
		    (u_longlong_t)dt->dt_recs[0].dr_txg, err);
		am->am_defer_failed = B_TRUE;
		cv_broadcast(&am->am_cv);
		wrote = B_FALSE;
	}

	async_mirror_txg_t *at = async_mirror_defer_next(am);

	for (uint_t r = 0; r < dt->dt_nrecs; r++) {
		const async_mirror_defer_rec_t *dr = &dt->dt_recs[r];
		boolean_t last = (dr->dr_tries >=
		    zfs_vdev_async_mirror_read_retries);
		uint_t rrecovered = 0, rlost = 0, rleft = 0;

		while (at != NULL && at->at_txg < dr->dr_txg)
			at = list_next(&am->am_txgs, at);
		if (at == NULL || at->at_txg != dr->dr_txg ||
		    at->at_ndefer == 0) {
			memset(&dt->dt_got[dr->dr_first], 0, dr->dr_count);
			continue;
		}
		ASSERT3U(at->at_ndefer_count, ==, dr->dr_count);

		uint_t i = dr->dr_first;

		for (async_mirror_defer_t *ad = list_head(&at->at_defer);
		    ad != NULL; ad = list_next(&at->at_defer, ad), i++) {
			if (ad->ad_done) {
				dt->dt_got[i] = ASYNC_MIRROR_DEFER_NONE;
			} else if (wrote &&
			    dt->dt_got[i] == ASYNC_MIRROR_DEFER_READ) {
				ad->ad_done = B_TRUE;
				dt->dt_got[i] = ASYNC_MIRROR_DEFER_RECOVERED;
				rrecovered++;
			} else if (last) {
				ad->ad_done = B_TRUE;
				dt->dt_got[i] = ASYNC_MIRROR_DEFER_LOST;
				rlost++;
			} else {
				dt->dt_got[i] = ASYNC_MIRROR_DEFER_NONE;
				rleft++;
			}
		}
		ASSERT3U(at->at_ndefer, ==, rrecovered + rlost + rleft);
		at->at_ndefer = rleft;
		at->at_defer_tries = dr->dr_tries + 1;
		recovered += rrecovered;
		lost += rlost;
		left += rleft;
	}
	if (recovered + lost + left > 0) {
		ASSERT3U(sams->sams_data_deferred, >=, recovered + lost);
		sams->sams_data_deferred -= recovered + lost;
		sams->sams_data_recovered += recovered;
		sams->sams_data_errors += lost;

		if (recovered > 0)
			am->am_defer_quick = B_FALSE;
		async_mirror_defer_floor(am);
		async_mirror_update_stats(am);
	}
	mutex_exit(&am->am_lock);
	return (left);
}

static void
async_mirror_defer_try_report(vdev_async_mirror_t *am,
    const async_mirror_defer_try_t *dt)
{
	const async_mirror_defer_t *first_recovered = NULL;
	uint64_t recovered = 0, recovered_txg = 0;

	for (uint_t r = 0; r < dt->dt_nrecs; r++) {
		const async_mirror_defer_rec_t *dr = &dt->dt_recs[r];

		for (uint_t i = dr->dr_first; i < dr->dr_first + dr->dr_count;
		    i++) {
			const async_mirror_defer_t *ad = &dt->dt_def[i];
			const zbookmark_phys_t *zb = &ad->ad_zb;

			if (dt->dt_got[i] == ASYNC_MIRROR_DEFER_RECOVERED) {

				zbookmark_phys_t zbc = *zb;

				spa_remove_error(am->am_spa, &zbc,
				    BP_GET_PHYSICAL_BIRTH(&ad->ad_bp));
				if (first_recovered == NULL) {
					first_recovered = ad;
					recovered_txg = dr->dr_txg;
				}
				recovered++;
			} else if (dt->dt_got[i] == ASYNC_MIRROR_DEFER_LOST) {
				zfs_dbgmsg("async mirror: txg %llu: deferred "
				    "data block <%llu, %llu, %lld, %llx> is "
				    "lost", (u_longlong_t)dr->dr_txg,
				    (u_longlong_t)zb->zb_objset,
				    (u_longlong_t)zb->zb_object,
				    (longlong_t)zb->zb_level,
				    (u_longlong_t)zb->zb_blkid);
			}
		}
	}
	if (recovered > 0) {
		const zbookmark_phys_t *zb = &first_recovered->ad_zb;
		nvlist_t *nvl = fnvlist_alloc();

		zfs_dbgmsg("async mirror: txg %llu: copied %llu deferred data "
		    "blocks; first block <%llu, %llu, %lld, %llx>",
		    (u_longlong_t)recovered_txg, (u_longlong_t)recovered,
		    (u_longlong_t)zb->zb_objset, (u_longlong_t)zb->zb_object,
		    (longlong_t)zb->zb_level, (u_longlong_t)zb->zb_blkid);
		fnvlist_add_uint64(nvl, ZFS_EV_ASYNC_MIRROR_TXG, recovered_txg);
		fnvlist_add_uint64(nvl, ZFS_EV_ASYNC_MIRROR_COUNT, recovered);
		fnvlist_add_uint64(nvl, ZFS_EV_ASYNC_MIRROR_OBJSET,
		    zb->zb_objset);
		fnvlist_add_uint64(nvl, ZFS_EV_ASYNC_MIRROR_OBJECT,
		    zb->zb_object);
		fnvlist_add_int64(nvl, ZFS_EV_ASYNC_MIRROR_LEVEL, zb->zb_level);
		fnvlist_add_uint64(nvl, ZFS_EV_ASYNC_MIRROR_BLKID,
		    zb->zb_blkid);
		spa_event_notify(am->am_spa, am->am_leader, nvl,
		    ESC_ZFS_ASYNC_MIRROR_RECOVERED);
		fnvlist_free(nvl);
	}
}

void
async_mirror_defer_retry(vdev_async_mirror_t *am, zthr_t *zthr)
{
	async_mirror_defer_try_t dt;
	boolean_t wrote = B_FALSE;
	int err = 0;

	mutex_enter(&am->am_lock);
	async_mirror_txg_t *first = async_mirror_defer_next(am);

	if (first == NULL) {
		mutex_exit(&am->am_lock);
		return;
	}
	async_mirror_defer_try_init(am, first, &dt);
	mutex_exit(&am->am_lock);

	uint_t pos = 0;
	while (pos < dt.dt_n && err == 0) {
		uint_t lo = pos;

		if (async_mirror_defer_try_read(am, &dt, &pos) > 0)
			err = async_mirror_defer_try_write(am, &dt, lo, pos,
			    &wrote);
		for (uint_t i = lo; i < pos; i++) {
			if (dt.dt_abds[i] != NULL) {
				abd_free(dt.dt_abds[i]);
				dt.dt_abds[i] = NULL;
			}
		}
	}
	uint_t left = async_mirror_defer_try_account(am, &dt, wrote, err);
	async_mirror_defer_try_report(am, &dt);
	for (uint_t i = 0; i < dt.dt_n; i++) {
		if (dt.dt_abds[i] != NULL)
			abd_free(dt.dt_abds[i]);
	}
	kmem_free(dt.dt_abds, dt.dt_n * sizeof (abd_t *));
	kmem_free(dt.dt_got, dt.dt_n * sizeof (uint8_t));
	vmem_free(dt.dt_def, dt.dt_n * sizeof (*dt.dt_def));
	kmem_free(dt.dt_recs, dt.dt_nrecs * sizeof (*dt.dt_recs));

	if (left > 0) {
		clock_t end = ddi_get_lbolt() +
		    MSEC_TO_TICK(ASYNC_MIRROR_RETRY_MS);

		while (!zthr_iscancelled(zthr)) {
			mutex_enter(&am->am_lock);
			clock_t now = ddi_get_lbolt();
			boolean_t wait = !async_mirror_draining(am->am_state) &&
			    now < end;

			if (wait) {
				(void) cv_timedwait_idle(&am->am_cv,
				    &am->am_lock, MIN(end, now + hz / 10 + 1));
			}
			mutex_exit(&am->am_lock);
			if (!wait)
				break;
		}
	}
}

ZFS_MODULE_PARAM(zfs_vdev_async_mirror, zfs_vdev_async_mirror_,
	defer_max, UINT, ZMOD_RW,
	"Maximum data blocks of one txg that the copy defers; above it the "
	"lagging child freezes");

ZFS_MODULE_PARAM(zfs_vdev_async_mirror, zfs_vdev_async_mirror_,
	inject_refuse, UINT, ZMOD_RW,
	"Refuse this block pointer of the copy of each txg (for testing)");

ZFS_MODULE_PARAM(zfs_vdev_async_mirror, zfs_vdev_async_mirror_,
	inject_rest, UINT, ZMOD_RW,
	"Copy this block of the copy of each txg with no block pointer "
	"(for testing)");
