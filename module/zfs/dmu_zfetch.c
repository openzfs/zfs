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

bool zfs_prefetch_disable = B_FALSE;

/* max # of streams per zfetch */
unsigned int	zfetch_max_streams = 8;
/* min time before stream reclaim */
unsigned int	zfetch_min_sec_reap = 2;
/* max bytes to prefetch per stream (default 8MB) */
uint32_t	zfetch_max_distance = 8 * 1024 * 1024;
/* max number of bytes in an array_read in which we allow prefetching (1MB) */
unsigned long	zfetch_array_rd_sz = 1024 * 1024;

typedef struct zfetch_stats {
	kstat_named_t zfetchstat_hits;
	kstat_named_t zfetchstat_misses;
	kstat_named_t zfetchstat_max_streams;
} zfetch_stats_t;

static zfetch_stats_t zfetch_stats = {
	{ "hits",			KSTAT_DATA_UINT64 },
	{ "misses",			KSTAT_DATA_UINT64 },
	{ "max_streams",		KSTAT_DATA_UINT64 },
};

#define	ZFETCHSTAT_BUMP(stat) \
	atomic_inc_64(&zfetch_stats.stat.value.ui64);

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

	list_create(&zf->zf_stream, sizeof (zstream_t),
	    offsetof(zstream_t, zs_node));

	rw_init(&zf->zf_rwlock, NULL, RW_DEFAULT, NULL);
}

static void
dmu_zfetch_stream_remove(zfetch_t *zf, zstream_t *zs)
{
	ASSERT(RW_WRITE_HELD(&zf->zf_rwlock));
	list_remove(&zf->zf_stream, zs);
	mutex_destroy(&zs->zs_lock);
	kmem_free(zs, sizeof (*zs));
}

/*
 * Clean-up state associated with a zfetch structure (e.g. destroy the
 * streams).  This doesn't free the zfetch_t itself, that's left to the caller.
 */
void
dmu_zfetch_fini(zfetch_t *zf)
{
	zstream_t *zs;

	ASSERT(!RW_LOCK_HELD(&zf->zf_rwlock));

	rw_enter(&zf->zf_rwlock, RW_WRITER);
	while ((zs = list_head(&zf->zf_stream)) != NULL)
		dmu_zfetch_stream_remove(zf, zs);
	rw_exit(&zf->zf_rwlock);
	list_destroy(&zf->zf_stream);
	rw_destroy(&zf->zf_rwlock);

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
	zstream_t *zs;
	zstream_t *zs_next;
	int numstreams = 0;
	uint32_t max_streams;

	ASSERT(RW_WRITE_HELD(&zf->zf_rwlock));

	/*
	 * Clean up old streams.
	 */
	for (zs = list_head(&zf->zf_stream);
	    zs != NULL; zs = zs_next) {
		zs_next = list_next(&zf->zf_stream, zs);
		if (((gethrtime() - zs->zs_atime) / NANOSEC) >
		    zfetch_min_sec_reap)
			dmu_zfetch_stream_remove(zf, zs);
		else
			numstreams++;
	}

	/*
	 * The maximum number of streams is normally zfetch_max_streams,
	 * but for small files we lower it such that it's at least possible
	 * for all the streams to be non-overlapping.
	 *
	 * If we are already at the maximum number of streams for this file,
	 * even after removing old streams, then don't create this stream.
	 */
	max_streams = MAX(1, MIN(zfetch_max_streams,
	    zf->zf_dnode->dn_maxblkid * zf->zf_dnode->dn_datablksz /
	    zfetch_max_distance));
	if (numstreams >= max_streams) {
		ZFETCHSTAT_BUMP(zfetchstat_max_streams);
		return;
	}

	zs = kmem_zalloc(sizeof (*zs), KM_SLEEP);
	zs->zs_blkid = blkid;
	zs->zs_pf_blkid = blkid;
	zs->zs_atime = gethrtime();
	mutex_init(&zs->zs_lock, NULL, MUTEX_DEFAULT, NULL);

	list_insert_head(&zf->zf_stream, zs);
}

/*
 * This is the prefetch entry point.  It calls all of the other dmu_zfetch
 * routines to create, delete, find, or operate upon prefetch streams.
 */
void
dmu_zfetch(zfetch_t *zf, uint64_t blkid, uint64_t nblks)
{
	zstream_t *zs;
	int64_t pf_start;
	int pf_nblks;
	int i;

	if (zfs_prefetch_disable)
		return;

	/*
	 * As a fast path for small (single-block) files, ignore access
	 * to the first block.
	 */
	if (blkid == 0)
		return;

	rw_enter(&zf->zf_rwlock, RW_READER);

	for (zs = list_head(&zf->zf_stream); zs != NULL;
	    zs = list_next(&zf->zf_stream, zs)) {
		if (blkid == zs->zs_blkid) {
			mutex_enter(&zs->zs_lock);
			/*
			 * zs_blkid could have changed before we
			 * acquired zs_lock; re-check them here.
			 */
			if (blkid != zs->zs_blkid) {
				mutex_exit(&zs->zs_lock);
				continue;
			}
			break;
		}
	}

	if (zs == NULL) {
		/*
		 * This access is not part of any existing stream.  Create
		 * a new stream for it.
		 */
		ZFETCHSTAT_BUMP(zfetchstat_misses);
		if (rw_tryupgrade(&zf->zf_rwlock))
			dmu_zfetch_stream_create(zf, blkid + nblks);
		rw_exit(&zf->zf_rwlock);
		return;
	}

	/*
	 * This access was to a block that we issued a prefetch for on
	 * behalf of this stream. Issue further prefetches for this stream.
	 *
	 * Normally, we start prefetching where we stopped
	 * prefetching last (zs_pf_blkid).  But when we get our first
	 * hit on this stream, zs_pf_blkid == zs_blkid, we don't
	 * want to prefetch to block we just accessed.  In this case,
	 * start just after the block we just accessed.
	 */
	pf_start = MAX(zs->zs_pf_blkid, blkid + nblks);

	/*
	 * Double our amount of prefetched data, but don't let the
	 * prefetch get further ahead than zfetch_max_distance.
	 */
	pf_nblks =
	    MIN((int64_t)zs->zs_pf_blkid - zs->zs_blkid + nblks,
	    zs->zs_blkid + nblks +
	    (zfetch_max_distance >> zf->zf_dnode->dn_datablkshift) - pf_start);

	zs->zs_pf_blkid = pf_start + pf_nblks;
	zs->zs_atime = gethrtime();
	zs->zs_blkid = blkid + nblks;

	/*
	 * dbuf_prefetch() issues the prefetch i/o
	 * asynchronously, but it may need to wait for an
	 * indirect block to be read from disk.  Therefore
	 * we do not want to hold any locks while we call it.
	 */
	mutex_exit(&zs->zs_lock);
	rw_exit(&zf->zf_rwlock);
	for (i = 0; i < pf_nblks; i++) {
		dbuf_prefetch(zf->zf_dnode, 0, pf_start + i,
		    ZIO_PRIORITY_ASYNC_READ, ARC_FLAG_PREDICTIVE_PREFETCH);
	}
	ZFETCHSTAT_BUMP(zfetchstat_hits);
}

#if defined(_KERNEL) && defined(HAVE_SPL)
module_param(zfs_prefetch_disable, bool, 0644);
MODULE_PARM_DESC(zfs_prefetch_disable, "Disable all ZFS prefetching");

module_param(zfetch_max_streams, uint, 0644);
MODULE_PARM_DESC(zfetch_max_streams, "Max number of streams per zfetch");

module_param(zfetch_min_sec_reap, uint, 0644);
MODULE_PARM_DESC(zfetch_min_sec_reap, "Min time before stream reclaim");

module_param(zfetch_max_distance, uint, 0644);
MODULE_PARM_DESC(zfetch_max_distance,
	"Max bytes to prefetch per stream (default 8MB)");

module_param(zfetch_array_rd_sz, ulong, 0644);
MODULE_PARM_DESC(zfetch_array_rd_sz, "Number of bytes in a array_read");
#endif
