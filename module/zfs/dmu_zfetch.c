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
#include <sys/wmsum.h>

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
	kstat_named_t zfetchstat_io_issued;
} zfetch_stats_t;

static zfetch_stats_t zfetch_stats = {
	{ "hits",			KSTAT_DATA_UINT64 },
	{ "misses",			KSTAT_DATA_UINT64 },
	{ "max_streams",		KSTAT_DATA_UINT64 },
	{ "io_issued",		KSTAT_DATA_UINT64 },
};

struct {
	wmsum_t zfetchstat_hits;
	wmsum_t zfetchstat_misses;
	wmsum_t zfetchstat_max_streams;
	wmsum_t zfetchstat_io_issued;
} zfetch_sums;

#define	ZFETCHSTAT_BUMP(stat)					\
	wmsum_add(&zfetch_sums.stat, 1)
#define	ZFETCHSTAT_ADD(stat, val)				\
	wmsum_add(&zfetch_sums.stat, val)


kstat_t		*zfetch_ksp;

static int
zfetch_kstats_update(kstat_t *ksp, int rw)
{
	zfetch_stats_t *zs = ksp->ks_data;

	if (rw == KSTAT_WRITE)
		return (EACCES);
	zs->zfetchstat_hits.value.ui64 =
	    wmsum_value(&zfetch_sums.zfetchstat_hits);
	zs->zfetchstat_misses.value.ui64 =
	    wmsum_value(&zfetch_sums.zfetchstat_misses);
	zs->zfetchstat_max_streams.value.ui64 =
	    wmsum_value(&zfetch_sums.zfetchstat_max_streams);
	zs->zfetchstat_io_issued.value.ui64 =
	    wmsum_value(&zfetch_sums.zfetchstat_io_issued);
	return (0);
}

void
zfetch_init(void)
{
	wmsum_init(&zfetch_sums.zfetchstat_hits, 0);
	wmsum_init(&zfetch_sums.zfetchstat_misses, 0);
	wmsum_init(&zfetch_sums.zfetchstat_max_streams, 0);
	wmsum_init(&zfetch_sums.zfetchstat_io_issued, 0);

	zfetch_ksp = kstat_create("zfs", 0, "zfetchstats", "misc",
	    KSTAT_TYPE_NAMED, sizeof (zfetch_stats) / sizeof (kstat_named_t),
	    KSTAT_FLAG_VIRTUAL);

	if (zfetch_ksp != NULL) {
		zfetch_ksp->ks_data = &zfetch_stats;
		zfetch_ksp->ks_update = zfetch_kstats_update;
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

	wmsum_fini(&zfetch_sums.zfetchstat_hits);
	wmsum_fini(&zfetch_sums.zfetchstat_misses);
	wmsum_fini(&zfetch_sums.zfetchstat_max_streams);
	wmsum_fini(&zfetch_sums.zfetchstat_io_issued);
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
	ASSERT(!list_link_active(&zs->zs_node));
	zfs_refcount_destroy(&zs->zs_callers);
	zfs_refcount_destroy(&zs->zs_refs);
	kmem_free(zs, sizeof (*zs));
}

static void
dmu_zfetch_stream_remove(zfetch_t *zf, zstream_t *zs)
{
	ASSERT(MUTEX_HELD(&zf->zf_lock));
	list_remove(&zf->zf_stream, zs);
	zf->zf_numstreams--;
	membar_producer();
	if (zfs_refcount_remove(&zs->zs_refs, NULL) == 0)
		dmu_zfetch_stream_fini(zs);
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
	while ((zs = list_head(&zf->zf_stream)) != NULL)
		dmu_zfetch_stream_remove(zf, zs);
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
		 * Skip if still active.  1 -- zf_stream reference.
		 */
		if (zfs_refcount_count(&zs->zs_refs) != 1)
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
	zs->zs_pf_blkid1 = blkid;
	zs->zs_pf_blkid = blkid;
	zs->zs_ipf_blkid1 = blkid;
	zs->zs_ipf_blkid = blkid;
	zs->zs_atime = now;
	zs->zs_fetch = zf;
	zs->zs_missed = B_FALSE;
	zfs_refcount_create(&zs->zs_callers);
	zfs_refcount_create(&zs->zs_refs);
	/* One reference for zf_stream. */
	zfs_refcount_add(&zs->zs_refs, NULL);
	zf->zf_numstreams++;
	list_insert_head(&zf->zf_stream, zs);
}

static void
dmu_zfetch_stream_done(void *arg, boolean_t io_issued)
{
	zstream_t *zs = arg;

	if (zfs_refcount_remove(&zs->zs_refs, NULL) == 0)
		dmu_zfetch_stream_fini(zs);
}

/*
 * This is the predictive prefetch entry point.  dmu_zfetch_prepare()
 * associates dnode access specified with blkid and nblks arguments with
 * prefetch stream, predicts further accesses based on that stats and returns
 * the stream pointer on success.  That pointer must later be passed to
 * dmu_zfetch_run() to initiate the speculative prefetch for the stream and
 * release it.  dmu_zfetch() is a wrapper for simple cases when window between
 * prediction and prefetch initiation is not needed.
 * fetch_data argument specifies whether actual data blocks should be fetched:
 *   FALSE -- prefetch only indirect blocks for predicted data blocks;
 *   TRUE -- prefetch predicted data blocks plus following indirect blocks.
 */
zstream_t *
dmu_zfetch_prepare(zfetch_t *zf, uint64_t blkid, uint64_t nblks,
    boolean_t fetch_data, boolean_t have_lock)
{
	zstream_t *zs;
	int64_t pf_start, ipf_start;
	int64_t pf_ahead_blks, max_blks;
	int max_dist_blks, pf_nblks, ipf_nblks;
	uint64_t end_of_access_blkid, maxblkid;
	end_of_access_blkid = blkid + nblks;
	spa_t *spa = zf->zf_dnode->dn_objset->os_spa;

	if (zfs_prefetch_disable)
		return (NULL);
	/*
	 * If we haven't yet loaded the indirect vdevs' mappings, we
	 * can only read from blocks that we carefully ensure are on
	 * concrete vdevs (or previously-loaded indirect vdevs).  So we
	 * can't allow the predictive prefetcher to attempt reads of other
	 * blocks (e.g. of the MOS's dnode object).
	 */
	if (!spa_indirect_vdevs_loaded(spa))
		return (NULL);

	/*
	 * As a fast path for small (single-block) files, ignore access
	 * to the first block.
	 */
	if (!have_lock && blkid == 0)
		return (NULL);

	if (!have_lock)
		rw_enter(&zf->zf_dnode->dn_struct_rwlock, RW_READER);

	/*
	 * A fast path for small files for which no prefetch will
	 * happen.
	 */
	maxblkid = zf->zf_dnode->dn_maxblkid;
	if (maxblkid < 2) {
		if (!have_lock)
			rw_exit(&zf->zf_dnode->dn_struct_rwlock);
		return (NULL);
	}
	mutex_enter(&zf->zf_lock);

	/*
	 * Find matching prefetch stream.  Depending on whether the accesses
	 * are block-aligned, first block of the new access may either follow
	 * the last block of the previous access, or be equal to it.
	 */
	for (zs = list_head(&zf->zf_stream); zs != NULL;
	    zs = list_next(&zf->zf_stream, zs)) {
		if (blkid == zs->zs_blkid) {
			break;
		} else if (blkid + 1 == zs->zs_blkid) {
			blkid++;
			nblks--;
			break;
		}
	}

	/*
	 * If the file is ending, remove the matching stream if found.
	 * If not found then it is too late to create a new one now.
	 */
	if (end_of_access_blkid >= maxblkid) {
		if (zs != NULL)
			dmu_zfetch_stream_remove(zf, zs);
		mutex_exit(&zf->zf_lock);
		if (!have_lock)
			rw_exit(&zf->zf_dnode->dn_struct_rwlock);
		return (NULL);
	}

	/* Exit if we already prefetched this block before. */
	if (nblks == 0) {
		mutex_exit(&zf->zf_lock);
		if (!have_lock)
			rw_exit(&zf->zf_dnode->dn_struct_rwlock);
		return (NULL);
	}

	if (zs == NULL) {
		/*
		 * This access is not part of any existing stream.  Create
		 * a new stream for it.
		 */
		dmu_zfetch_stream_create(zf, end_of_access_blkid);
		mutex_exit(&zf->zf_lock);
		if (!have_lock)
			rw_exit(&zf->zf_dnode->dn_struct_rwlock);
		ZFETCHSTAT_BUMP(zfetchstat_misses);
		return (NULL);
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
	if (zs->zs_pf_blkid1 < end_of_access_blkid)
		zs->zs_pf_blkid1 = end_of_access_blkid;
	if (zs->zs_ipf_blkid1 < end_of_access_blkid)
		zs->zs_ipf_blkid1 = end_of_access_blkid;

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
	max_blks = max_dist_blks - (ipf_start - zs->zs_pf_blkid);
	ipf_nblks = MIN(pf_ahead_blks, max_blks);
	zs->zs_ipf_blkid = ipf_start + ipf_nblks;

	zs->zs_blkid = end_of_access_blkid;
	/* Protect the stream from reclamation. */
	zs->zs_atime = gethrtime();
	zfs_refcount_add(&zs->zs_refs, NULL);
	/* Count concurrent callers. */
	zfs_refcount_add(&zs->zs_callers, NULL);
	mutex_exit(&zf->zf_lock);

	if (!have_lock)
		rw_exit(&zf->zf_dnode->dn_struct_rwlock);

	ZFETCHSTAT_BUMP(zfetchstat_hits);
	return (zs);
}

void
dmu_zfetch_run(zstream_t *zs, boolean_t missed, boolean_t have_lock)
{
	zfetch_t *zf = zs->zs_fetch;
	int64_t pf_start, pf_end, ipf_start, ipf_end;
	int epbs, issued;

	if (missed)
		zs->zs_missed = missed;

	/*
	 * Postpone the prefetch if there are more concurrent callers.
	 * It happens when multiple requests are waiting for the same
	 * indirect block.  The last one will run the prefetch for all.
	 */
	if (zfs_refcount_remove(&zs->zs_callers, NULL) != 0) {
		/* Drop reference taken in dmu_zfetch_prepare(). */
		if (zfs_refcount_remove(&zs->zs_refs, NULL) == 0)
			dmu_zfetch_stream_fini(zs);
		return;
	}

	mutex_enter(&zf->zf_lock);
	if (zs->zs_missed) {
		pf_start = zs->zs_pf_blkid1;
		pf_end = zs->zs_pf_blkid1 = zs->zs_pf_blkid;
	} else {
		pf_start = pf_end = 0;
	}
	ipf_start = MAX(zs->zs_pf_blkid1, zs->zs_ipf_blkid1);
	ipf_end = zs->zs_ipf_blkid1 = zs->zs_ipf_blkid;
	mutex_exit(&zf->zf_lock);
	ASSERT3S(pf_start, <=, pf_end);
	ASSERT3S(ipf_start, <=, ipf_end);

	epbs = zf->zf_dnode->dn_indblkshift - SPA_BLKPTRSHIFT;
	ipf_start = P2ROUNDUP(ipf_start, 1 << epbs) >> epbs;
	ipf_end = P2ROUNDUP(ipf_end, 1 << epbs) >> epbs;
	ASSERT3S(ipf_start, <=, ipf_end);
	issued = pf_end - pf_start + ipf_end - ipf_start;
	if (issued > 1) {
		/* More references on top of taken in dmu_zfetch_prepare(). */
		zfs_refcount_add_many(&zs->zs_refs, issued - 1, NULL);
	} else if (issued == 0) {
		/* Some other thread has done our work, so drop the ref. */
		if (zfs_refcount_remove(&zs->zs_refs, NULL) == 0)
			dmu_zfetch_stream_fini(zs);
		return;
	}

	if (!have_lock)
		rw_enter(&zf->zf_dnode->dn_struct_rwlock, RW_READER);

	issued = 0;
	for (int64_t blk = pf_start; blk < pf_end; blk++) {
		issued += dbuf_prefetch_impl(zf->zf_dnode, 0, blk,
		    ZIO_PRIORITY_ASYNC_READ, ARC_FLAG_PREDICTIVE_PREFETCH,
		    dmu_zfetch_stream_done, zs);
	}
	for (int64_t iblk = ipf_start; iblk < ipf_end; iblk++) {
		issued += dbuf_prefetch_impl(zf->zf_dnode, 1, iblk,
		    ZIO_PRIORITY_ASYNC_READ, ARC_FLAG_PREDICTIVE_PREFETCH,
		    dmu_zfetch_stream_done, zs);
	}

	if (!have_lock)
		rw_exit(&zf->zf_dnode->dn_struct_rwlock);

	if (issued)
		ZFETCHSTAT_ADD(zfetchstat_io_issued, issued);
}

void
dmu_zfetch(zfetch_t *zf, uint64_t blkid, uint64_t nblks, boolean_t fetch_data,
    boolean_t missed, boolean_t have_lock)
{
	zstream_t *zs;

	zs = dmu_zfetch_prepare(zf, blkid, nblks, fetch_data, have_lock);
	if (zs)
		dmu_zfetch_run(zs, missed, have_lock);
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
