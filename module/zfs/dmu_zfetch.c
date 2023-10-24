/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
#include <sys/arc_impl.h>
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

static int zfs_prefetch_disable = B_FALSE;

/* max # of streams per zfetch */
static unsigned int	zfetch_max_streams = 8;
/* min time before stream reclaim */
static unsigned int	zfetch_min_sec_reap = 1;
/* max time before stream delete */
static unsigned int	zfetch_max_sec_reap = 2;
#ifdef _ILP32
/* min bytes to prefetch per stream (default 2MB) */
static unsigned int	zfetch_min_distance = 2 * 1024 * 1024;
/* max bytes to prefetch per stream (default 8MB) */
unsigned int	zfetch_max_distance = 8 * 1024 * 1024;
#else
/* min bytes to prefetch per stream (default 4MB) */
static unsigned int	zfetch_min_distance = 4 * 1024 * 1024;
/* max bytes to prefetch per stream (default 64MB) */
unsigned int	zfetch_max_distance = 64 * 1024 * 1024;
#endif
/* max bytes to prefetch indirects for per stream (default 64MB) */
unsigned int	zfetch_max_idistance = 64 * 1024 * 1024;
/* max request reorder distance within a stream (default 16MB) */
unsigned int	zfetch_max_reorder = 16 * 1024 * 1024;
/* Max log2 fraction of holes in a stream */
unsigned int	zfetch_hole_shift = 2;

typedef struct zfetch_stats {
	kstat_named_t zfetchstat_hits;
	kstat_named_t zfetchstat_future;
	kstat_named_t zfetchstat_stride;
	kstat_named_t zfetchstat_past;
	kstat_named_t zfetchstat_misses;
	kstat_named_t zfetchstat_max_streams;
	kstat_named_t zfetchstat_io_issued;
	kstat_named_t zfetchstat_io_active;
} zfetch_stats_t;

static zfetch_stats_t zfetch_stats = {
	{ "hits",			KSTAT_DATA_UINT64 },
	{ "future",			KSTAT_DATA_UINT64 },
	{ "stride",			KSTAT_DATA_UINT64 },
	{ "past",			KSTAT_DATA_UINT64 },
	{ "misses",			KSTAT_DATA_UINT64 },
	{ "max_streams",		KSTAT_DATA_UINT64 },
	{ "io_issued",			KSTAT_DATA_UINT64 },
	{ "io_active",			KSTAT_DATA_UINT64 },
};

struct {
	wmsum_t zfetchstat_hits;
	wmsum_t zfetchstat_future;
	wmsum_t zfetchstat_stride;
	wmsum_t zfetchstat_past;
	wmsum_t zfetchstat_misses;
	wmsum_t zfetchstat_max_streams;
	wmsum_t zfetchstat_io_issued;
	aggsum_t zfetchstat_io_active;
} zfetch_sums;

#define	ZFETCHSTAT_BUMP(stat)					\
	wmsum_add(&zfetch_sums.stat, 1)
#define	ZFETCHSTAT_ADD(stat, val)				\
	wmsum_add(&zfetch_sums.stat, val)


static kstat_t		*zfetch_ksp;

static int
zfetch_kstats_update(kstat_t *ksp, int rw)
{
	zfetch_stats_t *zs = ksp->ks_data;

	if (rw == KSTAT_WRITE)
		return (EACCES);
	zs->zfetchstat_hits.value.ui64 =
	    wmsum_value(&zfetch_sums.zfetchstat_hits);
	zs->zfetchstat_future.value.ui64 =
	    wmsum_value(&zfetch_sums.zfetchstat_future);
	zs->zfetchstat_stride.value.ui64 =
	    wmsum_value(&zfetch_sums.zfetchstat_stride);
	zs->zfetchstat_past.value.ui64 =
	    wmsum_value(&zfetch_sums.zfetchstat_past);
	zs->zfetchstat_misses.value.ui64 =
	    wmsum_value(&zfetch_sums.zfetchstat_misses);
	zs->zfetchstat_max_streams.value.ui64 =
	    wmsum_value(&zfetch_sums.zfetchstat_max_streams);
	zs->zfetchstat_io_issued.value.ui64 =
	    wmsum_value(&zfetch_sums.zfetchstat_io_issued);
	zs->zfetchstat_io_active.value.ui64 =
	    aggsum_value(&zfetch_sums.zfetchstat_io_active);
	return (0);
}

void
zfetch_init(void)
{
	wmsum_init(&zfetch_sums.zfetchstat_hits, 0);
	wmsum_init(&zfetch_sums.zfetchstat_future, 0);
	wmsum_init(&zfetch_sums.zfetchstat_stride, 0);
	wmsum_init(&zfetch_sums.zfetchstat_past, 0);
	wmsum_init(&zfetch_sums.zfetchstat_misses, 0);
	wmsum_init(&zfetch_sums.zfetchstat_max_streams, 0);
	wmsum_init(&zfetch_sums.zfetchstat_io_issued, 0);
	aggsum_init(&zfetch_sums.zfetchstat_io_active, 0);

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
	wmsum_fini(&zfetch_sums.zfetchstat_future);
	wmsum_fini(&zfetch_sums.zfetchstat_stride);
	wmsum_fini(&zfetch_sums.zfetchstat_past);
	wmsum_fini(&zfetch_sums.zfetchstat_misses);
	wmsum_fini(&zfetch_sums.zfetchstat_max_streams);
	wmsum_fini(&zfetch_sums.zfetchstat_io_issued);
	ASSERT0(aggsum_value(&zfetch_sums.zfetchstat_io_active));
	aggsum_fini(&zfetch_sums.zfetchstat_io_active);
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
 * If there aren't too many active streams already, create one more.
 * In process delete/reuse all streams without hits for zfetch_max_sec_reap.
 * If needed, reuse oldest stream without hits for zfetch_min_sec_reap or ever.
 * The "blkid" argument is the next block that we expect this stream to access.
 */
static void
dmu_zfetch_stream_create(zfetch_t *zf, uint64_t blkid)
{
	zstream_t *zs, *zs_next, *zs_old = NULL;
	uint_t now = gethrestime_sec(), t;

	ASSERT(MUTEX_HELD(&zf->zf_lock));

	/*
	 * Delete too old streams, reusing the first found one.
	 */
	t = now - zfetch_max_sec_reap;
	for (zs = list_head(&zf->zf_stream); zs != NULL; zs = zs_next) {
		zs_next = list_next(&zf->zf_stream, zs);
		/*
		 * Skip if still active.  1 -- zf_stream reference.
		 */
		if ((int)(zs->zs_atime - t) >= 0)
			continue;
		if (zfs_refcount_count(&zs->zs_refs) != 1)
			continue;
		if (zs_old)
			dmu_zfetch_stream_remove(zf, zs);
		else
			zs_old = zs;
	}
	if (zs_old) {
		zs = zs_old;
		list_remove(&zf->zf_stream, zs);
		goto reuse;
	}

	/*
	 * The maximum number of streams is normally zfetch_max_streams,
	 * but for small files we lower it such that it's at least possible
	 * for all the streams to be non-overlapping.
	 */
	uint32_t max_streams = MAX(1, MIN(zfetch_max_streams,
	    (zf->zf_dnode->dn_maxblkid << zf->zf_dnode->dn_datablkshift) /
	    zfetch_max_distance));
	if (zf->zf_numstreams >= max_streams) {
		t = now - zfetch_min_sec_reap;
		for (zs = list_head(&zf->zf_stream); zs != NULL;
		    zs = list_next(&zf->zf_stream, zs)) {
			if ((int)(zs->zs_atime - t) >= 0)
				continue;
			if (zfs_refcount_count(&zs->zs_refs) != 1)
				continue;
			if (zs_old == NULL ||
			    (int)(zs_old->zs_atime - zs->zs_atime) >= 0)
				zs_old = zs;
		}
		if (zs_old) {
			zs = zs_old;
			list_remove(&zf->zf_stream, zs);
			goto reuse;
		}
		ZFETCHSTAT_BUMP(zfetchstat_max_streams);
		return;
	}

	zs = kmem_zalloc(sizeof (*zs), KM_SLEEP);
	zfs_refcount_create(&zs->zs_callers);
	zfs_refcount_create(&zs->zs_refs);
	/* One reference for zf_stream. */
	zfs_refcount_add(&zs->zs_refs, NULL);
	zf->zf_numstreams++;

reuse:
	list_insert_head(&zf->zf_stream, zs);
	zs->zs_blkid = blkid;
	/* Allow immediate stream reuse until first hit. */
	zs->zs_atime = now - zfetch_min_sec_reap;
	memset(zs->zs_ranges, 0, sizeof (zs->zs_ranges));
	zs->zs_pf_dist = 0;
	zs->zs_ipf_dist = 0;
	zs->zs_pf_start = blkid;
	zs->zs_pf_end = blkid;
	zs->zs_ipf_start = blkid;
	zs->zs_ipf_end = blkid;
	zs->zs_missed = B_FALSE;
	zs->zs_more = B_FALSE;
}

static void
dmu_zfetch_done(void *arg, uint64_t level, uint64_t blkid, boolean_t io_issued)
{
	zstream_t *zs = arg;

	if (io_issued && level == 0 && blkid < zs->zs_blkid)
		zs->zs_more = B_TRUE;
	if (zfs_refcount_remove(&zs->zs_refs, NULL) == 0)
		dmu_zfetch_stream_fini(zs);
	aggsum_add(&zfetch_sums.zfetchstat_io_active, -1);
}

/*
 * Process stream hit access for nblks blocks starting at zs_blkid.  Return
 * number of blocks to proceed for after aggregation with future ranges.
 */
static uint64_t
dmu_zfetch_hit(zstream_t *zs, uint64_t nblks)
{
	uint_t i, j;

	/* Optimize sequential accesses (no future ranges). */
	if (zs->zs_ranges[0].start == 0)
		goto done;

	/* Look for intersections with further ranges. */
	for (i = 0; i < ZFETCH_RANGES; i++) {
		zsrange_t *r = &zs->zs_ranges[i];
		if (r->start == 0 || r->start > nblks)
			break;
		if (r->end >= nblks) {
			nblks = r->end;
			i++;
			break;
		}
	}

	/* Delete all found intersecting ranges, updates remaining. */
	for (j = 0; i < ZFETCH_RANGES; i++, j++) {
		if (zs->zs_ranges[i].start == 0)
			break;
		ASSERT3U(zs->zs_ranges[i].start, >, nblks);
		ASSERT3U(zs->zs_ranges[i].end, >, nblks);
		zs->zs_ranges[j].start = zs->zs_ranges[i].start - nblks;
		zs->zs_ranges[j].end = zs->zs_ranges[i].end - nblks;
	}
	if (j < ZFETCH_RANGES) {
		zs->zs_ranges[j].start = 0;
		zs->zs_ranges[j].end = 0;
	}

done:
	zs->zs_blkid += nblks;
	return (nblks);
}

/*
 * Process future stream access for nblks blocks starting at blkid.  Return
 * number of blocks to proceed for if future ranges reach fill threshold.
 */
static uint64_t
dmu_zfetch_future(zstream_t *zs, uint64_t blkid, uint64_t nblks)
{
	ASSERT3U(blkid, >, zs->zs_blkid);
	blkid -= zs->zs_blkid;
	ASSERT3U(blkid + nblks, <=, UINT16_MAX);

	/* Search for first and last intersection or insert point. */
	uint_t f = ZFETCH_RANGES, l = 0, i;
	for (i = 0; i < ZFETCH_RANGES; i++) {
		zsrange_t *r = &zs->zs_ranges[i];
		if (r->start == 0 || r->start > blkid + nblks)
			break;
		if (r->end < blkid)
			continue;
		if (f > i)
			f = i;
		if (l < i)
			l = i;
	}
	if (f <= l) {
		/* Got some intersecting range, expand it if needed. */
		if (zs->zs_ranges[f].start > blkid)
			zs->zs_ranges[f].start = blkid;
		zs->zs_ranges[f].end = MAX(zs->zs_ranges[l].end, blkid + nblks);
		if (f < l) {
			/* Got more than one intersection, remove others. */
			for (f++, l++; l < ZFETCH_RANGES; f++, l++) {
				zs->zs_ranges[f].start = zs->zs_ranges[l].start;
				zs->zs_ranges[f].end = zs->zs_ranges[l].end;
			}
			zs->zs_ranges[f].start = 0;
			zs->zs_ranges[f].end = 0;
		}
	} else if (i < ZFETCH_RANGES) {
		/* Got no intersecting ranges, insert new one. */
		for (l = ZFETCH_RANGES - 1; l > i; l--) {
			zs->zs_ranges[l].start = zs->zs_ranges[l - 1].start;
			zs->zs_ranges[l].end = zs->zs_ranges[l - 1].end;
		}
		zs->zs_ranges[i].start = blkid;
		zs->zs_ranges[i].end = blkid + nblks;
	} else {
		/* No space left to insert.  Drop the range. */
		return (0);
	}

	/* Check if with the new access addition we reached fill threshold. */
	if (zfetch_hole_shift >= 16)
		return (0);
	uint_t hole = 0;
	for (i = f = l = 0; i < ZFETCH_RANGES; i++) {
		zsrange_t *r = &zs->zs_ranges[i];
		if (r->start == 0)
			break;
		hole += r->start - f;
		f = r->end;
		if (hole <= r->end >> zfetch_hole_shift)
			l = r->end;
	}
	if (l > 0)
		return (dmu_zfetch_hit(zs, l));

	return (0);
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
	spa_t *spa = zf->zf_dnode->dn_objset->os_spa;
	zfs_prefetch_type_t os_prefetch = zf->zf_dnode->dn_objset->os_prefetch;

	if (zfs_prefetch_disable || os_prefetch == ZFS_PREFETCH_NONE)
		return (NULL);

	if (os_prefetch == ZFS_PREFETCH_METADATA)
		fetch_data = B_FALSE;

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
	uint64_t maxblkid = zf->zf_dnode->dn_maxblkid;
	if (maxblkid < 2) {
		if (!have_lock)
			rw_exit(&zf->zf_dnode->dn_struct_rwlock);
		return (NULL);
	}
	mutex_enter(&zf->zf_lock);

	/*
	 * Find perfect prefetch stream.  Depending on whether the accesses
	 * are block-aligned, first block of the new access may either follow
	 * the last block of the previous access, or be equal to it.
	 */
	unsigned int dbs = zf->zf_dnode->dn_datablkshift;
	uint64_t end_blkid = blkid + nblks;
	for (zs = list_head(&zf->zf_stream); zs != NULL;
	    zs = list_next(&zf->zf_stream, zs)) {
		if (blkid == zs->zs_blkid) {
			goto hit;
		} else if (blkid + 1 == zs->zs_blkid) {
			blkid++;
			nblks--;
			goto hit;
		}
	}

	/*
	 * Find close enough prefetch stream.  Access crossing stream position
	 * is a hit in its new part.  Access ahead of stream position considered
	 * a hit for metadata prefetch, since we do not care about fill percent,
	 * or stored for future otherwise.  Access behind stream position is
	 * silently ignored, since we already skipped it reaching fill percent.
	 */
	uint_t max_reorder = MIN((zfetch_max_reorder >> dbs) + 1, UINT16_MAX);
	uint_t t = gethrestime_sec() - zfetch_max_sec_reap;
	for (zs = list_head(&zf->zf_stream); zs != NULL;
	    zs = list_next(&zf->zf_stream, zs)) {
		if (blkid > zs->zs_blkid) {
			if (end_blkid <= zs->zs_blkid + max_reorder) {
				if (!fetch_data) {
					nblks = dmu_zfetch_hit(zs,
					    end_blkid - zs->zs_blkid);
					ZFETCHSTAT_BUMP(zfetchstat_stride);
					goto future;
				}
				nblks = dmu_zfetch_future(zs, blkid, nblks);
				if (nblks > 0)
					ZFETCHSTAT_BUMP(zfetchstat_stride);
				else
					ZFETCHSTAT_BUMP(zfetchstat_future);
				goto future;
			}
		} else if (end_blkid >= zs->zs_blkid) {
			nblks -= zs->zs_blkid - blkid;
			blkid += zs->zs_blkid - blkid;
			goto hit;
		} else if (end_blkid + max_reorder > zs->zs_blkid &&
		    (int)(zs->zs_atime - t) >= 0) {
			ZFETCHSTAT_BUMP(zfetchstat_past);
			zs->zs_atime = gethrestime_sec();
			goto out;
		}
	}

	/*
	 * This access is not part of any existing stream.  Create a new
	 * stream for it unless we are at the end of file.
	 */
	if (end_blkid < maxblkid)
		dmu_zfetch_stream_create(zf, end_blkid);
	mutex_exit(&zf->zf_lock);
	if (!have_lock)
		rw_exit(&zf->zf_dnode->dn_struct_rwlock);
	ZFETCHSTAT_BUMP(zfetchstat_misses);
	return (NULL);

hit:
	nblks = dmu_zfetch_hit(zs, nblks);
	ZFETCHSTAT_BUMP(zfetchstat_hits);

future:
	zs->zs_atime = gethrestime_sec();

	/* Exit if we already prefetched for this position before. */
	if (nblks == 0)
		goto out;

	/* If the file is ending, remove the stream. */
	end_blkid = zs->zs_blkid;
	if (end_blkid >= maxblkid) {
		dmu_zfetch_stream_remove(zf, zs);
out:
		mutex_exit(&zf->zf_lock);
		if (!have_lock)
			rw_exit(&zf->zf_dnode->dn_struct_rwlock);
		return (NULL);
	}

	/*
	 * This access was to a block that we issued a prefetch for on
	 * behalf of this stream.  Calculate further prefetch distances.
	 *
	 * Start prefetch from the demand access size (nblks).  Double the
	 * distance every access up to zfetch_min_distance.  After that only
	 * if needed increase the distance by 1/8 up to zfetch_max_distance.
	 *
	 * Don't double the distance beyond single block if we have more
	 * than ~6% of ARC held by active prefetches.  It should help with
	 * getting out of RAM on some badly mispredicted read patterns.
	 */
	unsigned int nbytes = nblks << dbs;
	unsigned int pf_nblks;
	if (fetch_data) {
		if (unlikely(zs->zs_pf_dist < nbytes))
			zs->zs_pf_dist = nbytes;
		else if (zs->zs_pf_dist < zfetch_min_distance &&
		    (zs->zs_pf_dist < (1 << dbs) ||
		    aggsum_compare(&zfetch_sums.zfetchstat_io_active,
		    arc_c_max >> (4 + dbs)) < 0))
			zs->zs_pf_dist *= 2;
		else if (zs->zs_more)
			zs->zs_pf_dist += zs->zs_pf_dist / 8;
		zs->zs_more = B_FALSE;
		if (zs->zs_pf_dist > zfetch_max_distance)
			zs->zs_pf_dist = zfetch_max_distance;
		pf_nblks = zs->zs_pf_dist >> dbs;
	} else {
		pf_nblks = 0;
	}
	if (zs->zs_pf_start < end_blkid)
		zs->zs_pf_start = end_blkid;
	if (zs->zs_pf_end < end_blkid + pf_nblks)
		zs->zs_pf_end = end_blkid + pf_nblks;

	/*
	 * Do the same for indirects, starting where we will stop reading
	 * data blocks (and the indirects that point to them).
	 */
	if (unlikely(zs->zs_ipf_dist < nbytes))
		zs->zs_ipf_dist = nbytes;
	else
		zs->zs_ipf_dist *= 2;
	if (zs->zs_ipf_dist > zfetch_max_idistance)
		zs->zs_ipf_dist = zfetch_max_idistance;
	pf_nblks = zs->zs_ipf_dist >> dbs;
	if (zs->zs_ipf_start < zs->zs_pf_end)
		zs->zs_ipf_start = zs->zs_pf_end;
	if (zs->zs_ipf_end < zs->zs_pf_end + pf_nblks)
		zs->zs_ipf_end = zs->zs_pf_end + pf_nblks;

	zfs_refcount_add(&zs->zs_refs, NULL);
	/* Count concurrent callers. */
	zfs_refcount_add(&zs->zs_callers, NULL);
	mutex_exit(&zf->zf_lock);

	if (!have_lock)
		rw_exit(&zf->zf_dnode->dn_struct_rwlock);
	return (zs);
}

void
dmu_zfetch_run(zfetch_t *zf, zstream_t *zs, boolean_t missed,
    boolean_t have_lock)
{
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
		pf_start = zs->zs_pf_start;
		pf_end = zs->zs_pf_start = zs->zs_pf_end;
	} else {
		pf_start = pf_end = 0;
	}
	ipf_start = zs->zs_ipf_start;
	ipf_end = zs->zs_ipf_start = zs->zs_ipf_end;
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
		zfs_refcount_add_few(&zs->zs_refs, issued - 1, NULL);
	} else if (issued == 0) {
		/* Some other thread has done our work, so drop the ref. */
		if (zfs_refcount_remove(&zs->zs_refs, NULL) == 0)
			dmu_zfetch_stream_fini(zs);
		return;
	}
	aggsum_add(&zfetch_sums.zfetchstat_io_active, issued);

	if (!have_lock)
		rw_enter(&zf->zf_dnode->dn_struct_rwlock, RW_READER);

	issued = 0;
	for (int64_t blk = pf_start; blk < pf_end; blk++) {
		issued += dbuf_prefetch_impl(zf->zf_dnode, 0, blk,
		    ZIO_PRIORITY_ASYNC_READ, 0, dmu_zfetch_done, zs);
	}
	for (int64_t iblk = ipf_start; iblk < ipf_end; iblk++) {
		issued += dbuf_prefetch_impl(zf->zf_dnode, 1, iblk,
		    ZIO_PRIORITY_ASYNC_READ, 0, dmu_zfetch_done, zs);
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
		dmu_zfetch_run(zf, zs, missed, have_lock);
}

ZFS_MODULE_PARAM(zfs_prefetch, zfs_prefetch_, disable, INT, ZMOD_RW,
	"Disable all ZFS prefetching");

ZFS_MODULE_PARAM(zfs_prefetch, zfetch_, max_streams, UINT, ZMOD_RW,
	"Max number of streams per zfetch");

ZFS_MODULE_PARAM(zfs_prefetch, zfetch_, min_sec_reap, UINT, ZMOD_RW,
	"Min time before stream reclaim");

ZFS_MODULE_PARAM(zfs_prefetch, zfetch_, max_sec_reap, UINT, ZMOD_RW,
	"Max time before stream delete");

ZFS_MODULE_PARAM(zfs_prefetch, zfetch_, min_distance, UINT, ZMOD_RW,
	"Min bytes to prefetch per stream");

ZFS_MODULE_PARAM(zfs_prefetch, zfetch_, max_distance, UINT, ZMOD_RW,
	"Max bytes to prefetch per stream");

ZFS_MODULE_PARAM(zfs_prefetch, zfetch_, max_idistance, UINT, ZMOD_RW,
	"Max bytes to prefetch indirects for per stream");

ZFS_MODULE_PARAM(zfs_prefetch, zfetch_, max_reorder, UINT, ZMOD_RW,
	"Max request reorder distance within a stream");

ZFS_MODULE_PARAM(zfs_prefetch, zfetch_, hole_shift, UINT, ZMOD_RW,
	"Max log2 fraction of holes in a stream");
