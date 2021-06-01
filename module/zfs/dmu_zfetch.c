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

/*
 * Copyright (c) 2013, 2017 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/dnode.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_zfetch.h>
#include <sys/dmu.h>
#include <sys/dbuf.h>
#include <sys/kstat.h>

/*
 * This tunable disables predictive prefetch.  Note that it leaves "prescient"
 * prefetch (e.g. prefetch for zfs send) intact.  Unlike predictive prefetch,
 * prescient prefetch never issues i/os that end up not being needed,
 * so it can't hurt performance.
 */

int zfs_prefetch_disable = B_FALSE;

/* max # of streams per zfetch */
unsigned int	zfetch_max_streams = 8;
/* min time before stream reclaim */
unsigned int	zfetch_min_sec_reap = 2;
/* max bytes to prefetch per stream (default 8MB) */
unsigned int	zfetch_max_distance = 8 * 1024 * 1024;
/* max bytes to prefetch indirects for per stream (default 64MB) */
unsigned int	zfetch_max_idistance = 64 * 1024 * 1024;
/* max number of bytes in an array_read in which we allow prefetching (1MB) */
unsigned long	zfetch_array_rd_sz = 1024 * 1024;

typedef struct zfetch_stats {
	kstat_named_t zfetchstat_hits;
	kstat_named_t zfetchstat_misses;
	kstat_named_t zfetchstat_max_streams;
	kstat_named_t zfetchstat_max_completion_us;
	kstat_named_t zfetchstat_last_completion_us;
	kstat_named_t zfetchstat_io_issued;
} zfetch_stats_t;

static zfetch_stats_t zfetch_stats = {
	{ "hits",			KSTAT_DATA_UINT64 },
	{ "misses",			KSTAT_DATA_UINT64 },
	{ "max_streams",		KSTAT_DATA_UINT64 },
	{ "max_completion_us",		KSTAT_DATA_UINT64 },
	{ "last_completion_us",		KSTAT_DATA_UINT64 },
	{ "io_issued",		KSTAT_DATA_UINT64 },
};

#define	ZFETCHSTAT_BUMP(stat) \
	atomic_inc_64(&zfetch_stats.stat.value.ui64)
#define	ZFETCHSTAT_ADD(stat, val)				\
	atomic_add_64(&zfetch_stats.stat.value.ui64, val)
#define	ZFETCHSTAT_SET(stat, val)				\
	zfetch_stats.stat.value.ui64 = val
#define	ZFETCHSTAT_GET(stat)					\
	zfetch_stats.stat.value.ui64


kstat_t		*zfetch_ksp;

void
zfetch_init(void)
{
	zfetch_ksp = kstat_create("zfs", 0, "zfetchstats", "misc",
	    KSTAT_TYPE_NAMED, sizeof (zfetch_stats) / sizeof (kstat_named_t),
	    KSTAT_FLAG_VIRTUAL);

	if (zfetch_ksp != NULL) {
		zfetch_ksp->ks_data = &zfetch_stats;
		kstat_install(zfetch_ksp);
	}
}

void
zfetch_fini(void)
{
	if (zfetch_ksp != NULL) {
		kstat_delete(zfetch_ksp);
		zfetch_ksp = NULL;
	}
}

/*
 * This takes a pointer to a zfetch structure and a dnode.  It performs the
 * necessary setup for the zfetch structure, grokking data from the
 * associated dnode.
 */
void
dmu_zfetch_init(zfetch_t *zf, dnode_t *dno)
{
	if (zf == NULL)
		return;
	zf->zf_dnode = dno;
	zf->zf_numstreams = 0;

	list_create(&zf->zf_stream, sizeof (zstream_t),
	    offsetof(zstream_t, zs_node));

	mutex_init(&zf->zf_lock, NULL, MUTEX_DEFAULT, NULL);
}

static void
dmu_zfetch_stream_fini(zstream_t *zs)
{
	mutex_destroy(&zs->zs_lock);
	kmem_free(zs, sizeof (*zs));
}

static void
dmu_zfetch_stream_remove(zfetch_t *zf, zstream_t *zs)
{
	ASSERT(MUTEX_HELD(&zf->zf_lock));
	list_remove(&zf->zf_stream, zs);
	dmu_zfetch_stream_fini(zs);
	zf->zf_numstreams--;
}

static void
dmu_zfetch_stream_orphan(zfetch_t *zf, zstream_t *zs)
{
	ASSERT(MUTEX_HELD(&zf->zf_lock));
	list_remove(&zf->zf_stream, zs);
	zs->zs_fetch = NULL;
	zf->zf_numstreams--;
}

/*
 * Clean-up state associated with a zfetch structure (e.g. destroy the
 * streams).  This doesn't free the zfetch_t itself, that's left to the caller.
 */
void
dmu_zfetch_fini(zfetch_t *zf)
{
	zstream_t *zs;

	mutex_enter(&zf->zf_lock);
	while ((zs = list_head(&zf->zf_stream)) != NULL) {
		if (zfs_refcount_count(&zs->zs_blocks) != 0)
			dmu_zfetch_stream_orphan(zf, zs);
		else
			dmu_zfetch_stream_remove(zf, zs);
	}
	mutex_exit(&zf->zf_lock);
	list_destroy(&zf->zf_stream);
	mutex_destroy(&zf->zf_lock);

	zf->zf_dnode = NULL;
}

/*
 * If there aren't too many streams already, create a new stream.
 * The "blkid" argument is the next block that we expect this stream to access.
 * While we're here, clean up old streams (which haven't been
 * accessed for at least zfetch_min_sec_reap seconds).
 */
static void
dmu_zfetch_stream_create(zfetch_t *zf, uint64_t blkid)
{
	zstream_t *zs_next;
	hrtime_t now = gethrtime();

	ASSERT(MUTEX_HELD(&zf->zf_lock));

	/*
	 * Clean up old streams.
	 */
	for (zstream_t *zs = list_head(&zf->zf_stream);
	    zs != NULL; zs = zs_next) {
		zs_next = list_next(&zf->zf_stream, zs);
		/*
		 * Skip gethrtime() call if there are still references
		 */
		if (zfs_refcount_count(&zs->zs_blocks) != 0)
			continue;
		if (((now - zs->zs_atime) / NANOSEC) >
		    zfetch_min_sec_reap)
			dmu_zfetch_stream_remove(zf, zs);
	}

	/*
	 * The maximum number of streams is normally zfetch_max_streams,
	 * but for small files we lower it such that it's at least possible
	 * for all the streams to be non-overlapping.
	 *
	 * If we are already at the maximum number of streams for this file,
	 * even after removing old streams, then don't create this stream.
	 */
	uint32_t max_streams = MAX(1, MIN(zfetch_max_streams,
	    zf->zf_dnode->dn_maxblkid * zf->zf_dnode->dn_datablksz /
	    zfetch_max_distance));
	if (zf->zf_numstreams >= max_streams) {
		ZFETCHSTAT_BUMP(zfetchstat_max_streams);
		return;
	}

	zstream_t *zs = kmem_zalloc(sizeof (*zs), KM_SLEEP);
	zs->zs_blkid = blkid;
	zs->zs_pf_blkid = blkid;
	zs->zs_ipf_blkid = blkid;
	zs->zs_atime = now;
	zs->zs_fetch = zf;
	zfs_refcount_create(&zs->zs_blocks);
	mutex_init(&zs->zs_lock, NULL, MUTEX_DEFAULT, NULL);
	zf->zf_numstreams++;
	list_insert_head(&zf->zf_stream, zs);
}

static void
dmu_zfetch_stream_done(void *arg, boolean_t io_issued)
{
	zstream_t *zs = arg;

	if (zs->zs_start_time && io_issued) {
		hrtime_t now = gethrtime();
		hrtime_t delta = NSEC2USEC(now - zs->zs_start_time);

		zs->zs_start_time = 0;
		ZFETCHSTAT_SET(zfetchstat_last_completion_us, delta);
		if (delta > ZFETCHSTAT_GET(zfetchstat_max_completion_us))
			ZFETCHSTAT_SET(zfetchstat_max_completion_us, delta);
	}

	if (zfs_refcount_remove(&zs->zs_blocks, NULL) != 0)
		return;

	/*
	 * The parent fetch structure has gone away
	 */
	if (zs->zs_fetch == NULL)
		dmu_zfetch_stream_fini(zs);
}

/*
 * This is the predictive prefetch entry point.  It associates dnode access
 * specified with blkid and nblks arguments with prefetch stream, predicts
 * further accesses based on that stats and initiates speculative prefetch.
 * fetch_data argument specifies whether actual data blocks should be fetched:
 *   FALSE -- prefetch only indirect blocks for predicted data blocks;
 *   TRUE -- prefetch predicted data blocks plus following indirect blocks.
 */
void
dmu_zfetch(zfetch_t *zf, uint64_t blkid, uint64_t nblks, boolean_t fetch_data,
    boolean_t have_lock)
{
	zstream_t *zs;
	int64_t pf_start, ipf_start, ipf_istart, ipf_iend;
	int64_t pf_ahead_blks, max_blks;
	int epbs, max_dist_blks, pf_nblks, ipf_nblks, issued;
	uint64_t end_of_access_blkid;
	end_of_access_blkid = blkid + nblks;
	spa_t *spa = zf->zf_dnode->dn_objset->os_spa;

	if (zfs_prefetch_disable)
		return;
	/*
	 * If we haven't yet loaded the indirect vdevs' mappings, we
	 * can only read from blocks that we carefully ensure are on
	 * concrete vdevs (or previously-loaded indirect vdevs).  So we
	 * can't allow the predictive prefetcher to attempt reads of other
	 * blocks (e.g. of the MOS's dnode object).
	 */
	if (!spa_indirect_vdevs_loaded(spa))
		return;

	/*
	 * As a fast path for small (single-block) files, ignore access
	 * to the first block.
	 */
	if (!have_lock && blkid == 0)
		return;

	if (!have_lock)
		rw_enter(&zf->zf_dnode->dn_struct_rwlock, RW_READER);

	/*
	 * A fast path for small files for which no prefetch will
	 * happen.
	 */
	if (zf->zf_dnode->dn_maxblkid < 2) {
		if (!have_lock)
			rw_exit(&zf->zf_dnode->dn_struct_rwlock);
		return;
	}
	mutex_enter(&zf->zf_lock);

	/*
	 * Find matching prefetch stream.  Depending on whether the accesses
	 * are block-aligned, first block of the new access may either follow
	 * the last block of the previous access, or be equal to it.
	 */
	for (zs = list_head(&zf->zf_stream); zs != NULL;
	    zs = list_next(&zf->zf_stream, zs)) {
		if (blkid == zs->zs_blkid || blkid + 1 == zs->zs_blkid) {
			mutex_enter(&zs->zs_lock);
			/*
			 * zs_blkid could have changed before we
			 * acquired zs_lock; re-check them here.
			 */
			if (blkid == zs->zs_blkid) {
				break;
			} else if (blkid + 1 == zs->zs_blkid) {
				blkid++;
				nblks--;
				if (nblks == 0) {
					/* Already prefetched this before. */
					mutex_exit(&zs->zs_lock);
					mutex_exit(&zf->zf_lock);
					if (!have_lock) {
						rw_exit(&zf->zf_dnode->
						    dn_struct_rwlock);
					}
					return;
				}
				break;
			}
			mutex_exit(&zs->zs_lock);
		}
	}

	if (zs == NULL) {
		/*
		 * This access is not part of any existing stream.  Create
		 * a new stream for it.
		 */
		ZFETCHSTAT_BUMP(zfetchstat_misses);

		dmu_zfetch_stream_create(zf, end_of_access_blkid);
		mutex_exit(&zf->zf_lock);
		if (!have_lock)
			rw_exit(&zf->zf_dnode->dn_struct_rwlock);
		return;
	}

	/*
	 * This access was to a block that we issued a prefetch for on
	 * behalf of this stream. Issue further prefetches for this stream.
	 *
	 * Normally, we start prefetching where we stopped
	 * prefetching last (zs_pf_blkid).  But when we get our first
	 * hit on this stream, zs_pf_blkid == zs_blkid, we don't
	 * want to prefetch the block we just accessed.  In this case,
	 * start just after the block we just accessed.
	 */
	pf_start = MAX(zs->zs_pf_blkid, end_of_access_blkid);

	/*
	 * Double our amount of prefetched data, but don't let the
	 * prefetch get further ahead than zfetch_max_distance.
	 */
	if (fetch_data) {
		max_dist_blks =
		    zfetch_max_distance >> zf->zf_dnode->dn_datablkshift;
		/*
		 * Previously, we were (zs_pf_blkid - blkid) ahead.  We
		 * want to now be double that, so read that amount again,
		 * plus the amount we are catching up by (i.e. the amount
		 * read just now).
		 */
		pf_ahead_blks = zs->zs_pf_blkid - blkid + nblks;
		max_blks = max_dist_blks - (pf_start - end_of_access_blkid);
		pf_nblks = MIN(pf_ahead_blks, max_blks);
	} else {
		pf_nblks = 0;
	}

	zs->zs_pf_blkid = pf_start + pf_nblks;

	/*
	 * Do the same for indirects, starting from where we stopped last,
	 * or where we will stop reading data blocks (and the indirects
	 * that point to them).
	 */
	ipf_start = MAX(zs->zs_ipf_blkid, zs->zs_pf_blkid);
	max_dist_blks = zfetch_max_idistance >> zf->zf_dnode->dn_datablkshift;
	/*
	 * We want to double our distance ahead of the data prefetch
	 * (or reader, if we are not prefetching data).  Previously, we
	 * were (zs_ipf_blkid - blkid) ahead.  To double that, we read
	 * that amount again, plus the amount we are catching up by
	 * (i.e. the amount read now + the amount of data prefetched now).
	 */
	pf_ahead_blks = zs->zs_ipf_blkid - blkid + nblks + pf_nblks;
	max_blks = max_dist_blks - (ipf_start - end_of_access_blkid);
	ipf_nblks = MIN(pf_ahead_blks, max_blks);
	zs->zs_ipf_blkid = ipf_start + ipf_nblks;

	epbs = zf->zf_dnode->dn_indblkshift - SPA_BLKPTRSHIFT;
	ipf_istart = P2ROUNDUP(ipf_start, 1 << epbs) >> epbs;
	ipf_iend = P2ROUNDUP(zs->zs_ipf_blkid, 1 << epbs) >> epbs;

	zs->zs_atime = gethrtime();
	/* no prior reads in progress */
	if (zfs_refcount_count(&zs->zs_blocks) == 0)
		zs->zs_start_time = zs->zs_atime;
	zs->zs_blkid = end_of_access_blkid;
	zfs_refcount_add_many(&zs->zs_blocks, pf_nblks + ipf_iend - ipf_istart,
	    NULL);
	mutex_exit(&zs->zs_lock);
	mutex_exit(&zf->zf_lock);
	issued = 0;

	/*
	 * dbuf_prefetch() is asynchronous (even when it needs to read
	 * indirect blocks), but we still prefer to drop our locks before
	 * calling it to reduce the time we hold them.
	 */

	for (int i = 0; i < pf_nblks; i++) {
		issued += dbuf_prefetch_impl(zf->zf_dnode, 0, pf_start + i,
		    ZIO_PRIORITY_ASYNC_READ, ARC_FLAG_PREDICTIVE_PREFETCH,
		    dmu_zfetch_stream_done, zs);
	}
	for (int64_t iblk = ipf_istart; iblk < ipf_iend; iblk++) {
		issued += dbuf_prefetch_impl(zf->zf_dnode, 1, iblk,
		    ZIO_PRIORITY_ASYNC_READ, ARC_FLAG_PREDICTIVE_PREFETCH,
		    dmu_zfetch_stream_done, zs);
	}
	if (!have_lock)
		rw_exit(&zf->zf_dnode->dn_struct_rwlock);
	ZFETCHSTAT_BUMP(zfetchstat_hits);

	if (issued)
		ZFETCHSTAT_ADD(zfetchstat_io_issued, issued);
}

/* BEGIN CSTYLED */
ZFS_MODULE_PARAM(zfs_prefetch, zfs_prefetch_, disable, INT, ZMOD_RW,
	"Disable all ZFS prefetching");

ZFS_MODULE_PARAM(zfs_prefetch, zfetch_, max_streams, UINT, ZMOD_RW,
	"Max number of streams per zfetch");

ZFS_MODULE_PARAM(zfs_prefetch, zfetch_, min_sec_reap, UINT, ZMOD_RW,
	"Min time before stream reclaim");

ZFS_MODULE_PARAM(zfs_prefetch, zfetch_, max_distance, UINT, ZMOD_RW,
	"Max bytes to prefetch per stream");

ZFS_MODULE_PARAM(zfs_prefetch, zfetch_, max_idistance, UINT, ZMOD_RW,
	"Max bytes to prefetch indirects for per stream");

ZFS_MODULE_PARAM(zfs_prefetch, zfetch_, array_rd_sz, ULONG, ZMOD_RW,
	"Number of bytes in a array_read");
/* END CSTYLED */
