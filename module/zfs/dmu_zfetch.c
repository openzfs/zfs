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

/* max # of streams per zfetch */
unsigned int	zfetch_max_streams = 8;
/* min time before stream reclaim */
unsigned int	zfetch_min_sec_reap = 60;
/* max bytes to prefetch per stream (default 8MB) */
unsigned int	zfetch_max_distance = 8 * 1024 * 1024;
/* max bytes to prefetch indirects for per stream (default 64MB) */
unsigned int	zfetch_max_idistance = 64 * 1024 * 1024;
/* max number of bytes in an array_read in which we allow prefetching (1MB) */
unsigned long	zfetch_array_rd_sz = 1024 * 1024;

#define	PREFETCH_MAX_ORDER	32

typedef struct zfetch_stats {
	kstat_named_t zfetchstat_hits;
	kstat_named_t zfetchstat_misses;
	kstat_named_t zfetchstat_hits_data;
	kstat_named_t zfetchstat_hits_meta;
	kstat_named_t zfetchstat_hits_fwd;
	kstat_named_t zfetchstat_hits_bwd;
	kstat_named_t zfetchstat_reaps;
	kstat_named_t zfetchstat_prefetch_order[PREFETCH_MAX_ORDER];
} zfetch_stats_t;

static zfetch_stats_t zfetch_stats = {
	{ "hits",			KSTAT_DATA_UINT64 },
	{ "misses",			KSTAT_DATA_UINT64 },
	{ "hits_data",			KSTAT_DATA_UINT64 },
	{ "hits_meta",			KSTAT_DATA_UINT64 },
	{ "hits_forward",		KSTAT_DATA_UINT64 },
	{ "hits_backward",		KSTAT_DATA_UINT64 },
	{ "reaps",			KSTAT_DATA_UINT64 },
	{ { "prefetch_size_N",		KSTAT_DATA_UINT64 } },
};

#define	ZFETCHSTAT_BUMP(stat)    atomic_inc_64(&zfetch_stats.stat.value.ui64)
#define	ZFETCHSTAT_BUMP_ORDER(stat, size)				\
do {									\
	unsigned _oidx = highbit64(size) - 1;				\
	if (_oidx < PREFETCH_MAX_ORDER)					\
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

		for (i = 0; i < PREFETCH_MAX_ORDER; i++) {
			snprintf(zfetch_stats.zfetchstat_prefetch_order[i].name,
			    KSTAT_STRLEN, "prefetch_bytes_%lu", 1UL << i);
			zfetch_stats.zfetchstat_prefetch_order[i].data_type =
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

	zf->zf_dnode = dno;
	mutex_init(&zf->zf_lock, NULL, MUTEX_DEFAULT, NULL);

	zf->zf_pftree = range_tree_create(NULL, NULL, &zf->zf_lock);
	zf->zf_atime = gethrtime();

}

/*
 * Clean-up state associated with a zfetch structure (e.g. destroy the
 * streams).  This doesn't free the zfetch_t itself, that's left to the caller.
 */
void
dmu_zfetch_fini(zfetch_t *zf)
{
	mutex_enter(&zf->zf_lock);
	range_tree_vacate(zf->zf_pftree, NULL, NULL);
	mutex_exit(&zf->zf_lock);

	range_tree_destroy(zf->zf_pftree);
	mutex_destroy(&zf->zf_lock);

	zf->zf_dnode = NULL;
	zf->zf_pftree = NULL;
	zf->zf_atime = 0;
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
	int i;
	uint64_t end_of_access_blkid;
	end_of_access_blkid = blkid + nblks;

	uint64_t pf_space, pf_range, pf_start, pf_end;
	uint64_t pf_fwd = 0, pf_fwd_cnt = 0;
	uint64_t pf_bwd = 0, pf_bwd_cnt = 0;
	uint64_t pf_blk_max;

	if (zfs_prefetch_disable)
		return;

	// ?
	if (zf->zf_dnode->dn_datablkshift == 0)
		return;

	mutex_enter(&zf->zf_lock);

	if (NSEC2SEC(gethrtime() - zf->zf_atime) > zfetch_min_sec_reap) {
		ZFETCHSTAT_BUMP(zfetchstat_reaps);
		range_tree_vacate(zf->zf_pftree, NULL, NULL);
	}


	zf->zf_atime = gethrtime();

	/* ADD the request into prefetch tree */
	range_tree_clear(zf->zf_pftree, blkid, nblks);
	range_tree_add(zf->zf_pftree, blkid, nblks);

	if (fetch_data) {

		/* Check how much to prefetch */
		pf_space = range_tree_space(zf->zf_pftree);
		pf_start = range_tree_space_start(zf->zf_pftree);
		pf_end = range_tree_space_end(zf->zf_pftree);
		pf_range = pf_end - pf_start;

		ASSERT3U(pf_end, >=, pf_start);

		mutex_exit(&zf->zf_lock);

		pf_blk_max = MAX(1, zfetch_max_distance >>
		    zf->zf_dnode->dn_datablkshift);

		/* #1 - first, small access */
		if (pf_space == nblks)
			goto miss;

		/* #2 - pf space too fragmented -> random access: do nothing */
		if (pf_range > (4 * pf_space))
			goto miss;

		/* #3 - pf space somewhat fragmented -> prefetch only 1 block */
		if (pf_range > (2 * pf_space)) {
			pf_fwd = pf_bwd = 1;
			goto do_prefetch;
		}

		/* #4 - access is towards the end -> prefetch forward */
		if (ABS((int64_t)(pf_end - end_of_access_blkid)) <
		    (pf_range / 4)) {
			pf_fwd = pf_blk_max;
			goto do_prefetch;
		}

		/* #5 - access is towards the beginning -> prefetch backward */
		if (ABS((int64_t)(pf_start - blkid)) < (pf_range / 4)) {
			pf_bwd = pf_blk_max;
			goto do_prefetch;
		}

		/* #6 - pf space fragmented -> prefetch +-2 block */
		if (pf_range > pf_space) {
			pf_fwd = pf_bwd = 2;
			goto do_prefetch;
		}

		goto miss;
	} else {
		pf_fwd = 1;
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

	for (i = 0; i < pf_fwd; i++) {
		boolean_t p = B_FALSE;

		mutex_enter(&zf->zf_lock);
		p = range_tree_contains(zf->zf_pftree,
		    end_of_access_blkid + i + 1, 1);
		if (!p)
			range_tree_add(zf->zf_pftree,
			    end_of_access_blkid + i + 1, 1);
		mutex_exit(&zf->zf_lock);

		if (!p) {
			ZFETCHSTAT_BUMP(zfetchstat_hits_fwd);
			pf_fwd_cnt++;
			dbuf_prefetch(zf->zf_dnode, 0,
			    end_of_access_blkid + i + 1,
			    ZIO_PRIORITY_ASYNC_READ,
			    ARC_FLAG_PREDICTIVE_PREFETCH);
		}
	}

	for (i = pf_bwd; i >= 0; i--) {
		boolean_t p = B_FALSE;

		if ((int64_t)(blkid - i - 1) < 0)
			continue;

		mutex_enter(&zf->zf_lock);
		p = range_tree_contains(zf->zf_pftree, blkid - i - 1, 1);
		if (!p)
			range_tree_add(zf->zf_pftree, blkid - i - 1, 1);
		mutex_exit(&zf->zf_lock);

		if (!p) {
			ZFETCHSTAT_BUMP(zfetchstat_hits_bwd);
			pf_bwd_cnt++;
			dbuf_prefetch(zf->zf_dnode, 0, blkid - i - 1,
			    ZIO_PRIORITY_ASYNC_READ,
			    ARC_FLAG_PREDICTIVE_PREFETCH);
		}
	}

	ZFETCHSTAT_BUMP_ORDER(zfetchstat_prefetch_order,
	    (pf_fwd_cnt + pf_bwd_cnt) << zf->zf_dnode->dn_datablkshift);

	// TODO:
#if 0
	/*
	 * Double our amount of prefetched data, but don't let the
	 * prefetch get further ahead than zfetch_max_distance.
	 */
	if (fetch_data) {

		/* Make sure zfetch_max_distance is sane */
		uint64_t zfetch_max_distance_loc = MAX(zfetch_max_distance,
		    16ULL << zf->zf_dnode->dn_datablkshift);

		zfetch_max_distance_loc = MAX(zfetch_max_distance_loc,
		    16ULL * (nblks << zf->zf_dnode->dn_datablkshift));

		max_dist_blks =
		    zfetch_max_distance_loc >> zf->zf_dnode->dn_datablkshift;
		/*
		 * Previously, we were (zs_pf_blkid - blkid) ahead.  We
		 * want to now be double that, so read that amount again,
		 * plus the amount we are catching up by (i.e. the amount
		 * read just now).
		 */
		pf_ahead_blks = zs->zs_pf_blkid - blkid + nblks;
		max_blks = max_dist_blks - (pf_start - end_of_access_blkid);
		pf_nblks = MIN(pf_ahead_blks, max_blks);
		ZFETCHSTAT_BUMP(zfetchstat_hits_data);
		ZFETCHSTAT_BUMP_ORDER(zfetchstat_prefetch_order, pf_nblks <<
		    zf->zf_dnode->dn_datablkshift);
	} else {
		pf_nblks = 0;
		ZFETCHSTAT_BUMP(zfetchstat_hits_meta);
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


	zs->zs_blkid = end_of_access_blkid;
	mutex_exit(&zs->zs_lock);
	rw_exit(&zf->zf_rwlock);

	/*
	 * dbuf_prefetch() is asynchronous (even when it needs to read
	 * indirect blocks), but we still prefer to drop our locks before
	 * calling it to reduce the time we hold them.
	 */

	for (i = 0; i < pf_nblks; i++) {
		dbuf_prefetch(zf->zf_dnode, 0, pf_start + i,
		    ZIO_PRIORITY_ASYNC_READ, ARC_FLAG_PREDICTIVE_PREFETCH);
	}
	for (iblk = ipf_istart; iblk < ipf_iend; iblk++) {
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
/* END CSTYLED */
#endif
