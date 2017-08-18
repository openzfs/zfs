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
 * Copyright (c) 2013, 2015 by Delphix. All rights reserved.
 * Copyright (C) 2017 Gvozden Nešković. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/dnode.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_zfetch.h>
#include <sys/dmu.h>
#include <sys/dbuf.h>
#include <sys/kstat.h>
#include <sys/range_tree.h>

/*
 * This tunable disables predictive prefetch.  Note that it leaves "prescient"
 * prefetch (e.g. prefetch for zfs send) intact.  Unlike predictive prefetch,
 * prescient prefetch never issues i/os that end up not being needed,
 * so it can't hurt performance.
 */

int zfs_prefetch_disable = B_FALSE;

/* min time before stream reclaim */
unsigned int	zfetch_min_sec_reap = 60;
/* max bytes to prefetch per stream (default 8MB) */
unsigned int	zfetch_max_distance = 8 * 1024 * 1024;
/* max bytes to prefetch indirects for per stream (default 64MB) */
unsigned int	zfetch_max_idistance = 64 * 1024 * 1024;

/* Number of requests to look for stream access (default 4) */
unsigned int	zfetch_stream_req = 4;
/* Number of blocks to skip when stream prefetching (default 1) */
unsigned int	zfetch_stream_skip_blk = 1;


#define	ZFETCHSTAT_ORDER_SHIFT	9
#define	ZFETCHSTAT_MAX_ORDER	(32 - ZFETCHSTAT_ORDER_SHIFT)

typedef struct zfetch_stats {
	kstat_named_t zfetchstat_hits;
	kstat_named_t zfetchstat_misses;
	kstat_named_t zfetchstat_hits_data;
	kstat_named_t zfetchstat_hits_meta;
	kstat_named_t zfetchstat_hits_fwd;
	kstat_named_t zfetchstat_hits_bwd;
	kstat_named_t zfetchstat_reaps;
	kstat_named_t zfetchstat_lock;
	kstat_named_t zfetchstat_sameblk;
	kstat_named_t zfetchstat_stream_fwd;
	kstat_named_t zfetchstat_stream_bwd;
	kstat_named_t zfetchstat_prefetch_bytes[ZFETCHSTAT_MAX_ORDER];
	kstat_named_t zfetchstat_prefetch_ahead[ZFETCHSTAT_MAX_ORDER];
} zfetch_stats_t;

static zfetch_stats_t zfetch_stats = {
	{ "hits",			KSTAT_DATA_UINT64 },
	{ "misses",			KSTAT_DATA_UINT64 },
	{ "hits_data",			KSTAT_DATA_UINT64 },
	{ "hits_meta",			KSTAT_DATA_UINT64 },
	{ "hits_forward",		KSTAT_DATA_UINT64 },
	{ "hits_backward",		KSTAT_DATA_UINT64 },
	{ "reaps",			KSTAT_DATA_UINT64 },
	{ "locked",			KSTAT_DATA_UINT64 },
	{ "same_blk_access",		KSTAT_DATA_UINT64 },
	{ "stream_forward",		KSTAT_DATA_UINT64 },
	{ "stream_backward",		KSTAT_DATA_UINT64 },
	{ { "prefetch_bytes_N",		KSTAT_DATA_UINT64 } },
	{ { "prefetch_ahead_N",		KSTAT_DATA_UINT64 } },
};

#define	ZFETCHSTAT_BUMP(stat)    atomic_inc_64(&zfetch_stats.stat.value.ui64)
#define	ZFETCHSTAT_BUMP_ORDER(stat, size)				\
do {									\
	unsigned _oidx = highbit64(size) - ZFETCHSTAT_ORDER_SHIFT - 1;	\
	if (_oidx < ZFETCHSTAT_MAX_ORDER)				\
		atomic_inc_64(&zfetch_stats.stat[_oidx].value.ui64);	\
} while (0)

kstat_t		*zfetch_ksp;

void
zfetch_init(void)
{
	int i;
	zfetch_ksp = kstat_create("zfs", 0, "zfetchstats", "misc",
	    KSTAT_TYPE_NAMED, sizeof (zfetch_stats) / sizeof (kstat_named_t),
	    KSTAT_FLAG_VIRTUAL);

	if (zfetch_ksp != NULL) {
		zfetch_ksp->ks_data = &zfetch_stats;
		kstat_install(zfetch_ksp);

		for (i = 0; i < ZFETCHSTAT_MAX_ORDER; i++) {
			snprintf(zfetch_stats.zfetchstat_prefetch_bytes[i].name,
			    KSTAT_STRLEN, "prefetch_bytes_%lu",
			    1UL << (i + ZFETCHSTAT_ORDER_SHIFT));
			zfetch_stats.zfetchstat_prefetch_bytes[i].data_type =
			    KSTAT_DATA_UINT64;

			snprintf(zfetch_stats.zfetchstat_prefetch_ahead[i].name,
			    KSTAT_STRLEN, "prefetch_ahead_%lu",
			    1UL << (i + ZFETCHSTAT_ORDER_SHIFT));
			zfetch_stats.zfetchstat_prefetch_ahead[i].data_type =
			    KSTAT_DATA_UINT64;
		}
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

	ASSERT3P(dno, !=, NULL);

	zf->zf_dnode = dno;
	mutex_init(&zf->zf_lock, NULL, MUTEX_DEFAULT, NULL);

	range_tree_initialize(&zf->zf_pf_tree, NULL, NULL, &zf->zf_lock);
	range_tree_initialize(&zf->zf_demand_tree, NULL, NULL, &zf->zf_lock);
	zf->zf_atime = gethrtime();
}

/*
 * Clean-up state associated with a zfetch structure (e.g. destroy the
 * streams).  This doesn't free the zfetch_t itself, that's left to the caller.
 */
void
dmu_zfetch_fini(zfetch_t *zf)
{
	if (zf->zf_dnode == NULL)
		return;

	mutex_enter(&zf->zf_lock);
	range_tree_vacate(&zf->zf_pf_tree, NULL, NULL);
	range_tree_vacate(&zf->zf_demand_tree, NULL, NULL);
	mutex_exit(&zf->zf_lock);

	range_tree_deinitialize(&zf->zf_pf_tree);
	range_tree_deinitialize(&zf->zf_demand_tree);
	mutex_destroy(&zf->zf_lock);

	zf->zf_dnode = NULL;
	zf->zf_atime = 0;
}

// [a, b] overlaps with [x, y] iff b > x and a < y
#define	L_OVERLAP(s)		((end > (s)) && (start < zai->zai_req_start))
#define	L_OVERLAP_SEG(e)	(MIN(end, zai->zai_req_start) - MAX(e, start))
#define	R_OVERLAP(b)		((end > zai->zai_req_end) && (start < (b)))
#define	R_OVERLAP_SEG(b)	(MIN(end, b) - MAX(zai->zai_req_end, start))

static void
zfetch_demand_walk_cb(void *arg, uint64_t start, uint64_t size)
{
	zfetch_access_info_t *zai = (zfetch_access_info_t *)arg;
	const uint64_t end = start + size;

	if (L_OVERLAP(zai->zai_st_min_blk)) {
		zai->zai_left_st_seg++;
		zai->zai_left_st_space += L_OVERLAP_SEG(zai->zai_st_min_blk);
	}

	if (R_OVERLAP(zai->zai_st_max_blk)) {
		zai->zai_right_st_seg++;
		zai->zai_right_st_space += R_OVERLAP_SEG(zai->zai_st_max_blk);
	}
}

static void
zfetch_prefetch_walk_cb(void *arg, uint64_t start, uint64_t size)
{
	zfetch_access_info_t *zai = (zfetch_access_info_t *)arg;
	const uint64_t end = start + size;

	if (L_OVERLAP(zai->zai_pf_min_blk)) {
		zai->zai_left_pf_seg++;
		zai->zai_left_pf_space += L_OVERLAP_SEG(zai->zai_pf_min_blk);
	}

	if (R_OVERLAP(zai->zai_pf_max_blk)) {
		zai->zai_right_pf_seg++;
		zai->zai_right_pf_space += R_OVERLAP_SEG(zai->zai_pf_max_blk);
	}
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
dmu_zfetch(zfetch_t *zf, uint64_t blkid, uint64_t nblks, boolean_t fetch_data)
{
	int i, stride;
	size_t pf_fwd = 0, pf_fwd_skip = 0, pf_fwd_cnt = 0;
	size_t pf_bwd = 0, pf_bwd_skip = 0, pf_bwd_cnt = 0;
	uint64_t pf_blk_max, st_blk_max;

	const uint64_t blkid_end = blkid + nblks;

	const int dshift = zf->zf_dnode->dn_datablkshift;

	if (blkid == 0 || blkid == DMU_BONUS_BLKID ||
	    blkid == DMU_SPILL_BLKID || dshift == 0)
		return;

	if (zfs_prefetch_disable)
		return;

	if (atomic_swap_64(&zf->zf_lastblkid, blkid) == blkid) {
		ZFETCHSTAT_BUMP(zfetchstat_sameblk);
		return;
	}

	if (!mutex_tryenter(&zf->zf_lock)) {
		ZFETCHSTAT_BUMP(zfetchstat_lock);
		return;
	}

	if (NSEC2SEC(gethrtime() - zf->zf_atime) > zfetch_min_sec_reap) {
		ZFETCHSTAT_BUMP(zfetchstat_reaps);
		range_tree_vacate(&zf->zf_pf_tree, NULL, NULL);
		range_tree_vacate(&zf->zf_demand_tree, NULL, NULL);
		zf->zf_lastblkid = 0;
	}

	zf->zf_atime = gethrtime();

	if (range_tree_overlaps(&zf->zf_demand_tree, blkid, nblks)) {
		mutex_exit(&zf->zf_lock);
		return;
	}

	/* ADD the request into prefetch tree */
	range_tree_set(&zf->zf_demand_tree, blkid, nblks);
	range_tree_set(&zf->zf_pf_tree, blkid, nblks);

	if (fetch_data) {
		zfetch_access_info_t *zai = &zf->zf_zai;
		st_blk_max = MAX(1, zfetch_stream_req * nblks);
		pf_blk_max = MAX(st_blk_max + 1, zfetch_max_distance >> dshift);

		bzero(zai, sizeof (zfetch_access_info_t));

		zai->zai_req_start = blkid;
		zai->zai_req_end = blkid_end;
		zai->zai_pf_min_blk = MAX(0, (int64_t)(blkid - pf_blk_max));
		zai->zai_st_min_blk = MAX(0, (int64_t)(blkid - st_blk_max));
		zai->zai_st_max_blk = blkid_end + st_blk_max;
		zai->zai_pf_max_blk = blkid_end + pf_blk_max;

		ASSERT3U(zai->zai_pf_min_blk, <=, zai->zai_st_min_blk);
		ASSERT3U(zai->zai_st_min_blk, <=, blkid);
		ASSERT3U(zai->zai_pf_max_blk, >, zai->zai_st_max_blk);
		ASSERT3U(zai->zai_st_max_blk, >, blkid);

		range_tree_range_walk(&zf->zf_demand_tree,
		    zai->zai_st_min_blk, zai->zai_st_max_blk,
		    zfetch_demand_walk_cb, zai);

		range_tree_range_walk(&zf->zf_pf_tree,
		    zai->zai_pf_min_blk, zai->zai_pf_max_blk,
		    zfetch_prefetch_walk_cb, zai);

		/* FORWARD stream */
		if (zai->zai_left_st_space == st_blk_max &&
		    zai->zai_left_st_space > zai->zai_right_st_space) {
			ZFETCHSTAT_BUMP(zfetchstat_stream_fwd);
			pf_fwd_skip = zfetch_stream_skip_blk;
			pf_fwd = zai->zai_right_pf_space * 7 / 8 +
			    pf_blk_max / 8;
			pf_fwd = MAX(pf_fwd, pf_fwd_skip + 2);

			mutex_exit(&zf->zf_lock);
			goto do_prefetch;
		}

		/* BACKWARD stream */
		if (zai->zai_right_st_space == st_blk_max &&
		    zai->zai_right_st_space > zai->zai_left_st_space) {
			ZFETCHSTAT_BUMP(zfetchstat_stream_bwd);
			pf_bwd_skip = zfetch_stream_skip_blk;
			pf_bwd = zai->zai_left_pf_space * 3 / 4 +
			    pf_blk_max / 4;
			pf_bwd = MAX(pf_bwd, pf_bwd_skip + 2);

			mutex_exit(&zf->zf_lock);
			goto do_prefetch;
		}

		// cmn_err(CE_NOTE, "stream_info: %lu (%lu) <|=|> %lu (%lu)",
		// 	zai->zai_left_space, zai->zai_left_segments,
		// 	zai->zai_right_space, zai->zai_right_segments);
		mutex_exit(&zf->zf_lock);
		goto miss;
	} else {
		pf_fwd = 0;
		mutex_exit(&zf->zf_lock);
		goto do_prefetch;
	}

miss:
	ZFETCHSTAT_BUMP(zfetchstat_misses);
	return;

do_prefetch:
	ZFETCHSTAT_BUMP(zfetchstat_hits);
	if (fetch_data)
		ZFETCHSTAT_BUMP(zfetchstat_hits_data);
	else
		ZFETCHSTAT_BUMP(zfetchstat_hits_meta);


	stride = MAX(1, pf_fwd / 4);
	for (i = pf_fwd_skip; i < pf_fwd; i += stride) {
		boolean_t p = B_FALSE;
		uint64_t pfblkid = blkid_end + i;

		if (pfblkid > zf->zf_dnode->dn_maxblkid)
			break;

		if (!mutex_tryenter(&zf->zf_lock)) {
			ZFETCHSTAT_BUMP(zfetchstat_lock);
			continue;
		}

		p = range_tree_overlaps(&zf->zf_pf_tree, pfblkid, stride);
		if (!p)
			range_tree_add(&zf->zf_pf_tree, pfblkid, stride);
		mutex_exit(&zf->zf_lock);

		if (!p) {
			int ps;
			for (ps = 0; ps < stride; ps++, pfblkid++) {
				if (pfblkid > zf->zf_dnode->dn_maxblkid)
					break;

				ZFETCHSTAT_BUMP(zfetchstat_hits_fwd);
				pf_fwd_cnt++;
				dbuf_prefetch(zf->zf_dnode, 0, pfblkid,
				    ZIO_PRIORITY_ASYNC_READ,
				    ARC_FLAG_PREDICTIVE_PREFETCH);
			}
		}
	}

	stride = MAX(1, pf_bwd / 4);
	for (i = pf_bwd_skip; i < pf_bwd; i += stride) {
		boolean_t p = B_FALSE;
		uint64_t pfblkid = blkid - i - stride;

		if ((int64_t)(pfblkid) < 0)
			break;

		if (!mutex_tryenter(&zf->zf_lock)) {
			ZFETCHSTAT_BUMP(zfetchstat_lock);
			continue;
		}

		p = range_tree_overlaps(&zf->zf_pf_tree, pfblkid, stride);
		if (!p)
			range_tree_add(&zf->zf_pf_tree, pfblkid, stride);
		mutex_exit(&zf->zf_lock);

		if (!p) {
			int ps;
			for (ps = 0; ps < stride; ps++, pfblkid++) {
				ZFETCHSTAT_BUMP(zfetchstat_hits_bwd);
				pf_bwd_cnt++;
				dbuf_prefetch(zf->zf_dnode, 0, pfblkid,
				    ZIO_PRIORITY_ASYNC_READ,
				    ARC_FLAG_PREDICTIVE_PREFETCH);
			}
		}
	}

	ZFETCHSTAT_BUMP_ORDER(zfetchstat_prefetch_bytes,
	    (pf_fwd_cnt + pf_bwd_cnt) <<dshift);


	ZFETCHSTAT_BUMP_ORDER(zfetchstat_prefetch_ahead,
	    MAX(pf_fwd, pf_bwd) << dshift);

	// TODO: metadata prefetching
#if 0

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


	zs->zs_blkid = end_of_access_blkid;
	mutex_exit(&zs->zs_lock);
	rw_exit(&zf->zf_rwlock);

	/*
	 * dbuf_prefetch() is asynchronous (even when it needs to read
	 * indirect blocks), but we still prefer to drop our locks before
	 * calling it to reduce the time we hold them.
	 */

	for (int i = 0; i < pf_nblks; i++) {
		dbuf_prefetch(zf->zf_dnode, 0, pf_start + i,
		    ZIO_PRIORITY_ASYNC_READ, ARC_FLAG_PREDICTIVE_PREFETCH);
	}
	for (int64_t iblk = ipf_istart; iblk < ipf_iend; iblk++) {
		dbuf_prefetch(zf->zf_dnode, 1, iblk,
		    ZIO_PRIORITY_ASYNC_READ, ARC_FLAG_PREDICTIVE_PREFETCH);
	}
	ZFETCHSTAT_BUMP(zfetchstat_hits);
#endif
}

#if defined(_KERNEL) && defined(HAVE_SPL)
/* BEGIN CSTYLED */
module_param(zfs_prefetch_disable, int, 0644);
MODULE_PARM_DESC(zfs_prefetch_disable, "Disable all ZFS prefetching");

module_param(zfetch_min_sec_reap, uint, 0644);
MODULE_PARM_DESC(zfetch_min_sec_reap, "Min time before stream reclaim");

module_param(zfetch_max_distance, uint, 0644);
MODULE_PARM_DESC(zfetch_max_distance,
	"Max bytes to prefetch per stream (default 8MB)");

module_param(zfetch_stream_req, uint, 0644);
MODULE_PARM_DESC(zfetch_stream_req,
	"Number of blocks to look for stream access (default 4)");

module_param(zfetch_stream_skip_blk, uint, 0644);
MODULE_PARM_DESC(zfetch_stream_skip_blk,
	"Number of blocks to skip when stream prefetching (default 1)");

/* END CSTYLED */
#endif
