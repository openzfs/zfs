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
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright (c) 2012, 2015 by Delphix. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_scan.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_draid.h>
#include <sys/zio.h>
#include <sys/abd.h>
#include <sys/fs/zfs.h>

/*
 * Vdev mirror kstats
 */
static kstat_t *mirror_ksp = NULL;

typedef struct mirror_stats {
	kstat_named_t vdev_mirror_stat_rotating_linear;
	kstat_named_t vdev_mirror_stat_rotating_offset;
	kstat_named_t vdev_mirror_stat_rotating_seek;
	kstat_named_t vdev_mirror_stat_non_rotating_linear;
	kstat_named_t vdev_mirror_stat_non_rotating_seek;

	kstat_named_t vdev_mirror_stat_preferred_found;
	kstat_named_t vdev_mirror_stat_preferred_not_found;
} mirror_stats_t;

static mirror_stats_t mirror_stats = {
	/* New I/O follows directly the last I/O */
	{ "rotating_linear",			KSTAT_DATA_UINT64 },
	/* New I/O is within zfs_vdev_mirror_rotating_seek_offset of the last */
	{ "rotating_offset",			KSTAT_DATA_UINT64 },
	/* New I/O requires random seek */
	{ "rotating_seek",			KSTAT_DATA_UINT64 },
	/* New I/O follows directly the last I/O  (nonrot) */
	{ "non_rotating_linear",		KSTAT_DATA_UINT64 },
	/* New I/O requires random seek (nonrot) */
	{ "non_rotating_seek",			KSTAT_DATA_UINT64 },
	/* Preferred child vdev found */
	{ "preferred_found",			KSTAT_DATA_UINT64 },
	/* Preferred child vdev not found or equal load  */
	{ "preferred_not_found",		KSTAT_DATA_UINT64 },

};

#define	MIRROR_STAT(stat)		(mirror_stats.stat.value.ui64)
#define	MIRROR_INCR(stat, val) 		atomic_add_64(&MIRROR_STAT(stat), val)
#define	MIRROR_BUMP(stat)		MIRROR_INCR(stat, 1)

void
vdev_mirror_stat_init(void)
{
	mirror_ksp = kstat_create("zfs", 0, "vdev_mirror_stats",
	    "misc", KSTAT_TYPE_NAMED,
	    sizeof (mirror_stats) / sizeof (kstat_named_t), KSTAT_FLAG_VIRTUAL);
	if (mirror_ksp != NULL) {
		mirror_ksp->ks_data = &mirror_stats;
		kstat_install(mirror_ksp);
	}
}

void
vdev_mirror_stat_fini(void)
{
	if (mirror_ksp != NULL) {
		kstat_delete(mirror_ksp);
		mirror_ksp = NULL;
	}
}

/*
 * Virtual device vector for mirroring.
 */
typedef struct mirror_child {
	vdev_t		*mc_vd;
	uint64_t	mc_offset;
	int		mc_error;
	int		mc_load;
	uint8_t		mc_tried;
	uint8_t		mc_skipped;
	uint8_t		mc_speculative;
	uint8_t		mc_rebuilding;
} mirror_child_t;

typedef struct mirror_map {
	int		*mm_preferred;
	int		mm_preferred_cnt;
	int		mm_children;
	boolean_t	mm_resilvering;
	boolean_t	mm_rebuilding;
	boolean_t	mm_root;
	mirror_child_t	mm_child[];
} mirror_map_t;

static int vdev_mirror_shift = 21;

/*
 * The load configuration settings below are tuned by default for
 * the case where all devices are of the same rotational type.
 *
 * If there is a mixture of rotating and non-rotating media, setting
 * zfs_vdev_mirror_non_rotating_seek_inc to 0 may well provide better results
 * as it will direct more reads to the non-rotating vdevs which are more likely
 * to have a higher performance.
 */

/* Rotating media load calculation configuration. */
static int zfs_vdev_mirror_rotating_inc = 0;
static int zfs_vdev_mirror_rotating_seek_inc = 5;
static int zfs_vdev_mirror_rotating_seek_offset = 1 * 1024 * 1024;

/* Non-rotating media load calculation configuration. */
static int zfs_vdev_mirror_non_rotating_inc = 0;
static int zfs_vdev_mirror_non_rotating_seek_inc = 1;

static inline size_t
vdev_mirror_map_size(int children)
{
	return (offsetof(mirror_map_t, mm_child[children]) +
	    sizeof (int) * children);
}

static inline mirror_map_t *
vdev_mirror_map_alloc(int children, boolean_t resilvering, boolean_t root)
{
	mirror_map_t *mm;

	mm = kmem_zalloc(vdev_mirror_map_size(children), KM_SLEEP);
	mm->mm_children = children;
	mm->mm_resilvering = resilvering;
	mm->mm_root = root;
	mm->mm_preferred = (int *)((uintptr_t)mm +
	    offsetof(mirror_map_t, mm_child[children]));

	return (mm);
}

static void
vdev_mirror_map_free(zio_t *zio)
{
	mirror_map_t *mm = zio->io_vsd;

	kmem_free(mm, vdev_mirror_map_size(mm->mm_children));
}

static const zio_vsd_ops_t vdev_mirror_vsd_ops = {
	.vsd_free = vdev_mirror_map_free,
};

static int
vdev_mirror_load(mirror_map_t *mm, vdev_t *vd, uint64_t zio_offset)
{
	uint64_t last_offset;
	int64_t offset_diff;
	int load;

	/* All DVAs have equal weight at the root. */
	if (mm->mm_root)
		return (INT_MAX);

	/*
	 * We don't return INT_MAX if the device is resilvering i.e.
	 * vdev_resilver_txg != 0 as when tested performance was slightly
	 * worse overall when resilvering with compared to without.
	 */

	/* Fix zio_offset for leaf vdevs */
	if (vd->vdev_ops->vdev_op_leaf)
		zio_offset += VDEV_LABEL_START_SIZE;

	/* Standard load based on pending queue length. */
	load = vdev_queue_length(vd);
	last_offset = vdev_queue_last_offset(vd);

	if (vd->vdev_nonrot) {
		/* Non-rotating media. */
		if (last_offset == zio_offset) {
			MIRROR_BUMP(vdev_mirror_stat_non_rotating_linear);
			return (load + zfs_vdev_mirror_non_rotating_inc);
		}

		/*
		 * Apply a seek penalty even for non-rotating devices as
		 * sequential I/O's can be aggregated into fewer operations on
		 * the device, thus avoiding unnecessary per-command overhead
		 * and boosting performance.
		 */
		MIRROR_BUMP(vdev_mirror_stat_non_rotating_seek);
		return (load + zfs_vdev_mirror_non_rotating_seek_inc);
	}

	/* Rotating media I/O's which directly follow the last I/O. */
	if (last_offset == zio_offset) {
		MIRROR_BUMP(vdev_mirror_stat_rotating_linear);
		return (load + zfs_vdev_mirror_rotating_inc);
	}

	/*
	 * Apply half the seek increment to I/O's within seek offset
	 * of the last I/O issued to this vdev as they should incur less
	 * of a seek increment.
	 */
	offset_diff = (int64_t)(last_offset - zio_offset);
	if (ABS(offset_diff) < zfs_vdev_mirror_rotating_seek_offset) {
		MIRROR_BUMP(vdev_mirror_stat_rotating_offset);
		return (load + (zfs_vdev_mirror_rotating_seek_inc / 2));
	}

	/* Apply the full seek increment to all other I/O's. */
	MIRROR_BUMP(vdev_mirror_stat_rotating_seek);
	return (load + zfs_vdev_mirror_rotating_seek_inc);
}

static boolean_t
vdev_mirror_rebuilding(vdev_t *vd)
{
	if (vd->vdev_ops->vdev_op_leaf && vd->vdev_rebuild_txg)
		return (B_TRUE);

	for (int i = 0; i < vd->vdev_children; i++) {
		if (vdev_mirror_rebuilding(vd->vdev_child[i])) {
			return (B_TRUE);
		}
	}

	return (B_FALSE);
}

/*
 * Avoid inlining the function to keep vdev_mirror_io_start(), which
 * is this functions only caller, as small as possible on the stack.
 */
noinline static mirror_map_t *
vdev_mirror_map_init(zio_t *zio)
{
	mirror_map_t *mm = NULL;
	mirror_child_t *mc;
	vdev_t *vd = zio->io_vd;
	int c;

	if (vd == NULL) {
		dva_t *dva = zio->io_bp->blk_dva;
		spa_t *spa = zio->io_spa;
		dsl_scan_t *scn = spa->spa_dsl_pool->dp_scan;
		dva_t dva_copy[SPA_DVAS_PER_BP];

		/*
		 * The sequential scrub code sorts and issues all DVAs
		 * of a bp separately. Each of these IOs includes all
		 * original DVA copies so that repairs can be performed
		 * in the event of an error, but we only actually want
		 * to check the first DVA since the others will be
		 * checked by their respective sorted IOs. Only if we
		 * hit an error will we try all DVAs upon retrying.
		 *
		 * Note: This check is safe even if the user switches
		 * from a legacy scrub to a sequential one in the middle
		 * of processing, since scn_is_sorted isn't updated until
		 * all outstanding IOs from the previous scrub pass
		 * complete.
		 */
		if ((zio->io_flags & ZIO_FLAG_SCRUB) &&
		    !(zio->io_flags & ZIO_FLAG_IO_RETRY) &&
		    dsl_scan_scrubbing(spa->spa_dsl_pool) &&
		    scn->scn_is_sorted) {
			c = 1;
		} else {
			c = BP_GET_NDVAS(zio->io_bp);
		}

		/*
		 * If the pool cannot be written to, then infer that some
		 * DVAs might be invalid or point to vdevs that do not exist.
		 * We skip them.
		 */
		if (!spa_writeable(spa)) {
			ASSERT3U(zio->io_type, ==, ZIO_TYPE_READ);
			int j = 0;
			for (int i = 0; i < c; i++) {
				if (zfs_dva_valid(spa, &dva[i], zio->io_bp))
					dva_copy[j++] = dva[i];
			}
			if (j == 0) {
				zio->io_vsd = NULL;
				zio->io_error = ENXIO;
				return (NULL);
			}
			if (j < c) {
				dva = dva_copy;
				c = j;
			}
		}

		mm = vdev_mirror_map_alloc(c, B_FALSE, B_TRUE);
		for (c = 0; c < mm->mm_children; c++) {
			mc = &mm->mm_child[c];

			mc->mc_vd = vdev_lookup_top(spa, DVA_GET_VDEV(&dva[c]));
			mc->mc_offset = DVA_GET_OFFSET(&dva[c]);
			if (mc->mc_vd == NULL) {
				kmem_free(mm, vdev_mirror_map_size(
				    mm->mm_children));
				zio->io_vsd = NULL;
				zio->io_error = ENXIO;
				return (NULL);
			}
		}
	} else {
		/*
		 * If we are resilvering, then we should handle scrub reads
		 * differently; we shouldn't issue them to the resilvering
		 * device because it might not have those blocks.
		 *
		 * We are resilvering iff:
		 * 1) We are a replacing vdev (ie our name is "replacing-1" or
		 *    "spare-1" or something like that), and
		 * 2) The pool is currently being resilvered.
		 *
		 * We cannot simply check vd->vdev_resilver_txg, because it's
		 * not set in this path.
		 *
		 * Nor can we just check our vdev_ops; there are cases (such as
		 * when a user types "zpool replace pool odev spare_dev" and
		 * spare_dev is in the spare list, or when a spare device is
		 * automatically used to replace a DEGRADED device) when
		 * resilvering is complete but both the original vdev and the
		 * spare vdev remain in the pool.  That behavior is intentional.
		 * It helps implement the policy that a spare should be
		 * automatically removed from the pool after the user replaces
		 * the device that originally failed.
		 *
		 * If a spa load is in progress, then spa_dsl_pool may be
		 * uninitialized.  But we shouldn't be resilvering during a spa
		 * load anyway.
		 */
		boolean_t replacing = (vd->vdev_ops == &vdev_replacing_ops ||
		    vd->vdev_ops == &vdev_spare_ops) &&
		    spa_load_state(vd->vdev_spa) == SPA_LOAD_NONE &&
		    dsl_scan_resilvering(vd->vdev_spa->spa_dsl_pool);
		mm = vdev_mirror_map_alloc(vd->vdev_children, replacing,
		    B_FALSE);
		for (c = 0; c < mm->mm_children; c++) {
			mc = &mm->mm_child[c];
			mc->mc_vd = vd->vdev_child[c];
			mc->mc_offset = zio->io_offset;

			if (vdev_mirror_rebuilding(mc->mc_vd))
				mm->mm_rebuilding = mc->mc_rebuilding = B_TRUE;
		}
	}

	return (mm);
}

static int
vdev_mirror_open(vdev_t *vd, uint64_t *asize, uint64_t *max_asize,
    uint64_t *logical_ashift, uint64_t *physical_ashift)
{
	int numerrors = 0;
	int lasterror = 0;

	if (vd->vdev_children == 0) {
		vd->vdev_stat.vs_aux = VDEV_AUX_BAD_LABEL;
		return (SET_ERROR(EINVAL));
	}

	vdev_open_children(vd);

	for (int c = 0; c < vd->vdev_children; c++) {
		vdev_t *cvd = vd->vdev_child[c];

		if (cvd->vdev_open_error) {
			lasterror = cvd->vdev_open_error;
			numerrors++;
			continue;
		}

		*asize = MIN(*asize - 1, cvd->vdev_asize - 1) + 1;
		*max_asize = MIN(*max_asize - 1, cvd->vdev_max_asize - 1) + 1;
		*logical_ashift = MAX(*logical_ashift, cvd->vdev_ashift);
		*physical_ashift = MAX(*physical_ashift,
		    cvd->vdev_physical_ashift);
	}

	if (numerrors == vd->vdev_children) {
		if (vdev_children_are_offline(vd))
			vd->vdev_stat.vs_aux = VDEV_AUX_CHILDREN_OFFLINE;
		else
			vd->vdev_stat.vs_aux = VDEV_AUX_NO_REPLICAS;
		return (lasterror);
	}

	return (0);
}

static void
vdev_mirror_close(vdev_t *vd)
{
	for (int c = 0; c < vd->vdev_children; c++)
		vdev_close(vd->vdev_child[c]);
}

static void
vdev_mirror_child_done(zio_t *zio)
{
	mirror_child_t *mc = zio->io_private;

	mc->mc_error = zio->io_error;
	mc->mc_tried = 1;
	mc->mc_skipped = 0;
}

static void
vdev_mirror_scrub_done(zio_t *zio)
{
	mirror_child_t *mc = zio->io_private;

	if (zio->io_error == 0) {
		zio_t *pio;
		zio_link_t *zl = NULL;

		mutex_enter(&zio->io_lock);
		while ((pio = zio_walk_parents(zio, &zl)) != NULL) {
			mutex_enter(&pio->io_lock);
			ASSERT3U(zio->io_size, >=, pio->io_size);
			abd_copy(pio->io_abd, zio->io_abd, pio->io_size);
			mutex_exit(&pio->io_lock);
		}
		mutex_exit(&zio->io_lock);
	}

	abd_free(zio->io_abd);

	mc->mc_error = zio->io_error;
	mc->mc_tried = 1;
	mc->mc_skipped = 0;
}

/*
 * Check the other, lower-index DVAs to see if they're on the same
 * vdev as the child we picked.  If they are, use them since they
 * are likely to have been allocated from the primary metaslab in
 * use at the time, and hence are more likely to have locality with
 * single-copy data.
 */
static int
vdev_mirror_dva_select(zio_t *zio, int p)
{
	dva_t *dva = zio->io_bp->blk_dva;
	mirror_map_t *mm = zio->io_vsd;
	int preferred;
	int c;

	preferred = mm->mm_preferred[p];
	for (p--; p >= 0; p--) {
		c = mm->mm_preferred[p];
		if (DVA_GET_VDEV(&dva[c]) == DVA_GET_VDEV(&dva[preferred]))
			preferred = c;
	}
	return (preferred);
}

static int
vdev_mirror_preferred_child_randomize(zio_t *zio)
{
	mirror_map_t *mm = zio->io_vsd;
	int p;

	if (mm->mm_root) {
		p = random_in_range(mm->mm_preferred_cnt);
		return (vdev_mirror_dva_select(zio, p));
	}

	/*
	 * To ensure we don't always favour the first matching vdev,
	 * which could lead to wear leveling issues on SSD's, we
	 * use the I/O offset as a pseudo random seed into the vdevs
	 * which have the lowest load.
	 */
	p = (zio->io_offset >> vdev_mirror_shift) % mm->mm_preferred_cnt;
	return (mm->mm_preferred[p]);
}

static boolean_t
vdev_mirror_child_readable(mirror_child_t *mc)
{
	vdev_t *vd = mc->mc_vd;

	if (vd->vdev_top != NULL && vd->vdev_top->vdev_ops == &vdev_draid_ops)
		return (vdev_draid_readable(vd, mc->mc_offset));
	else
		return (vdev_readable(vd));
}

static boolean_t
vdev_mirror_child_missing(mirror_child_t *mc, uint64_t txg, uint64_t size)
{
	vdev_t *vd = mc->mc_vd;

	if (vd->vdev_top != NULL && vd->vdev_top->vdev_ops == &vdev_draid_ops)
		return (vdev_draid_missing(vd, mc->mc_offset, txg, size));
	else
		return (vdev_dtl_contains(vd, DTL_MISSING, txg, size));
}

/*
 * Try to find a vdev whose DTL doesn't contain the block we want to read
 * preferring vdevs based on determined load. If we can't, try the read on
 * any vdev we haven't already tried.
 *
 * Distributed spares are an exception to the above load rule. They are
 * always preferred in order to detect gaps in the distributed spare which
 * are created when another disk in the dRAID fails. In order to restore
 * redundancy those gaps must be read to trigger the required repair IO.
 */
static int
vdev_mirror_child_select(zio_t *zio)
{
	mirror_map_t *mm = zio->io_vsd;
	uint64_t txg = zio->io_txg;
	int c, lowest_load;

	ASSERT(zio->io_bp == NULL || BP_PHYSICAL_BIRTH(zio->io_bp) == txg);

	lowest_load = INT_MAX;
	mm->mm_preferred_cnt = 0;
	for (c = 0; c < mm->mm_children; c++) {
		mirror_child_t *mc;

		mc = &mm->mm_child[c];
		if (mc->mc_tried || mc->mc_skipped)
			continue;

		if (mc->mc_vd == NULL ||
		    !vdev_mirror_child_readable(mc)) {
			mc->mc_error = SET_ERROR(ENXIO);
			mc->mc_tried = 1;	/* don't even try */
			mc->mc_skipped = 1;
			continue;
		}

		if (vdev_mirror_child_missing(mc, txg, 1)) {
			mc->mc_error = SET_ERROR(ESTALE);
			mc->mc_skipped = 1;
			mc->mc_speculative = 1;
			continue;
		}

		if (mc->mc_vd->vdev_ops == &vdev_draid_spare_ops) {
			mm->mm_preferred[0] = c;
			mm->mm_preferred_cnt = 1;
			break;
		}

		mc->mc_load = vdev_mirror_load(mm, mc->mc_vd, mc->mc_offset);
		if (mc->mc_load > lowest_load)
			continue;

		if (mc->mc_load < lowest_load) {
			lowest_load = mc->mc_load;
			mm->mm_preferred_cnt = 0;
		}
		mm->mm_preferred[mm->mm_preferred_cnt] = c;
		mm->mm_preferred_cnt++;
	}

	if (mm->mm_preferred_cnt == 1) {
		MIRROR_BUMP(vdev_mirror_stat_preferred_found);
		return (mm->mm_preferred[0]);
	}

	if (mm->mm_preferred_cnt > 1) {
		MIRROR_BUMP(vdev_mirror_stat_preferred_not_found);
		return (vdev_mirror_preferred_child_randomize(zio));
	}

	/*
	 * Every device is either missing or has this txg in its DTL.
	 * Look for any child we haven't already tried before giving up.
	 */
	for (c = 0; c < mm->mm_children; c++) {
		if (!mm->mm_child[c].mc_tried)
			return (c);
	}

	/*
	 * Every child failed.  There's no place left to look.
	 */
	return (-1);
}

static void
vdev_mirror_io_start(zio_t *zio)
{
	mirror_map_t *mm;
	mirror_child_t *mc;
	int c, children;

	mm = vdev_mirror_map_init(zio);
	zio->io_vsd = mm;
	zio->io_vsd_ops = &vdev_mirror_vsd_ops;

	if (mm == NULL) {
		ASSERT(!spa_trust_config(zio->io_spa));
		ASSERT(zio->io_type == ZIO_TYPE_READ);
		zio_execute(zio);
		return;
	}

	if (zio->io_type == ZIO_TYPE_READ) {
		if (zio->io_bp != NULL &&
		    (zio->io_flags & ZIO_FLAG_SCRUB) && !mm->mm_resilvering) {
			/*
			 * For scrubbing reads (if we can verify the
			 * checksum here, as indicated by io_bp being
			 * non-NULL) we need to allocate a read buffer for
			 * each child and issue reads to all children.  If
			 * any child succeeds, it will copy its data into
			 * zio->io_data in vdev_mirror_scrub_done.
			 */
			for (c = 0; c < mm->mm_children; c++) {
				mc = &mm->mm_child[c];

				/* Don't issue ZIOs to offline children */
				if (!vdev_mirror_child_readable(mc)) {
					mc->mc_error = SET_ERROR(ENXIO);
					mc->mc_tried = 1;
					mc->mc_skipped = 1;
					continue;
				}

				zio_nowait(zio_vdev_child_io(zio, zio->io_bp,
				    mc->mc_vd, mc->mc_offset,
				    abd_alloc_sametype(zio->io_abd,
				    zio->io_size), zio->io_size,
				    zio->io_type, zio->io_priority, 0,
				    vdev_mirror_scrub_done, mc));
			}
			zio_execute(zio);
			return;
		}
		/*
		 * For normal reads just pick one child.
		 */
		c = vdev_mirror_child_select(zio);
		children = (c >= 0);
	} else {
		ASSERT(zio->io_type == ZIO_TYPE_WRITE);

		/*
		 * Writes go to all children.
		 */
		c = 0;
		children = mm->mm_children;
	}

	while (children--) {
		mc = &mm->mm_child[c];
		c++;

		/*
		 * When sequentially resilvering only issue write repair
		 * IOs to the vdev which is being rebuilt since performance
		 * is limited by the slowest child.  This is an issue for
		 * faster replacement devices such as distributed spares.
		 */
		if ((zio->io_priority == ZIO_PRIORITY_REBUILD) &&
		    (zio->io_flags & ZIO_FLAG_IO_REPAIR) &&
		    !(zio->io_flags & ZIO_FLAG_SCRUB) &&
		    mm->mm_rebuilding && !mc->mc_rebuilding) {
			continue;
		}

		zio_nowait(zio_vdev_child_io(zio, zio->io_bp,
		    mc->mc_vd, mc->mc_offset, zio->io_abd, zio->io_size,
		    zio->io_type, zio->io_priority, 0,
		    vdev_mirror_child_done, mc));
	}

	zio_execute(zio);
}

static int
vdev_mirror_worst_error(mirror_map_t *mm)
{
	int error[2] = { 0, 0 };

	for (int c = 0; c < mm->mm_children; c++) {
		mirror_child_t *mc = &mm->mm_child[c];
		int s = mc->mc_speculative;
		error[s] = zio_worst_error(error[s], mc->mc_error);
	}

	return (error[0] ? error[0] : error[1]);
}

static void
vdev_mirror_io_done(zio_t *zio)
{
	mirror_map_t *mm = zio->io_vsd;
	mirror_child_t *mc;
	int c;
	int good_copies = 0;
	int unexpected_errors = 0;

	if (mm == NULL)
		return;

	for (c = 0; c < mm->mm_children; c++) {
		mc = &mm->mm_child[c];

		if (mc->mc_error) {
			if (!mc->mc_skipped)
				unexpected_errors++;
		} else if (mc->mc_tried) {
			good_copies++;
		}
	}

	if (zio->io_type == ZIO_TYPE_WRITE) {
		/*
		 * XXX -- for now, treat partial writes as success.
		 *
		 * Now that we support write reallocation, it would be better
		 * to treat partial failure as real failure unless there are
		 * no non-degraded top-level vdevs left, and not update DTLs
		 * if we intend to reallocate.
		 */
		/* XXPOLICY */
		if (good_copies != mm->mm_children) {
			/*
			 * Always require at least one good copy.
			 *
			 * For ditto blocks (io_vd == NULL), require
			 * all copies to be good.
			 *
			 * XXX -- for replacing vdevs, there's no great answer.
			 * If the old device is really dead, we may not even
			 * be able to access it -- so we only want to
			 * require good writes to the new device.  But if
			 * the new device turns out to be flaky, we want
			 * to be able to detach it -- which requires all
			 * writes to the old device to have succeeded.
			 */
			if (good_copies == 0 || zio->io_vd == NULL)
				zio->io_error = vdev_mirror_worst_error(mm);
		}
		return;
	}

	ASSERT(zio->io_type == ZIO_TYPE_READ);

	/*
	 * If we don't have a good copy yet, keep trying other children.
	 */
	/* XXPOLICY */
	if (good_copies == 0 && (c = vdev_mirror_child_select(zio)) != -1) {
		ASSERT(c >= 0 && c < mm->mm_children);
		mc = &mm->mm_child[c];
		zio_vdev_io_redone(zio);
		zio_nowait(zio_vdev_child_io(zio, zio->io_bp,
		    mc->mc_vd, mc->mc_offset, zio->io_abd, zio->io_size,
		    ZIO_TYPE_READ, zio->io_priority, 0,
		    vdev_mirror_child_done, mc));
		return;
	}

	/* XXPOLICY */
	if (good_copies == 0) {
		zio->io_error = vdev_mirror_worst_error(mm);
		ASSERT(zio->io_error != 0);
	}

	if (good_copies && spa_writeable(zio->io_spa) &&
	    (unexpected_errors ||
	    (zio->io_flags & ZIO_FLAG_RESILVER) ||
	    ((zio->io_flags & ZIO_FLAG_SCRUB) && mm->mm_resilvering))) {
		/*
		 * Use the good data we have in hand to repair damaged children.
		 */
		for (c = 0; c < mm->mm_children; c++) {
			/*
			 * Don't rewrite known good children.
			 * Not only is it unnecessary, it could
			 * actually be harmful: if the system lost
			 * power while rewriting the only good copy,
			 * there would be no good copies left!
			 */
			mc = &mm->mm_child[c];

			if (mc->mc_error == 0) {
				vdev_ops_t *ops = mc->mc_vd->vdev_ops;

				if (mc->mc_tried)
					continue;
				/*
				 * We didn't try this child.  We need to
				 * repair it if:
				 * 1. it's a scrub (in which case we have
				 * tried everything that was healthy)
				 *  - or -
				 * 2. it's an indirect or distributed spare
				 * vdev (in which case it could point to any
				 * other vdev, which might have a bad DTL)
				 *  - or -
				 * 3. the DTL indicates that this data is
				 * missing from this vdev
				 */
				if (!(zio->io_flags & ZIO_FLAG_SCRUB) &&
				    ops != &vdev_indirect_ops &&
				    ops != &vdev_draid_spare_ops &&
				    !vdev_dtl_contains(mc->mc_vd, DTL_PARTIAL,
				    zio->io_txg, 1))
					continue;
				mc->mc_error = SET_ERROR(ESTALE);
			}

			zio_nowait(zio_vdev_child_io(zio, zio->io_bp,
			    mc->mc_vd, mc->mc_offset,
			    zio->io_abd, zio->io_size, ZIO_TYPE_WRITE,
			    zio->io_priority == ZIO_PRIORITY_REBUILD ?
			    ZIO_PRIORITY_REBUILD : ZIO_PRIORITY_ASYNC_WRITE,
			    ZIO_FLAG_IO_REPAIR | (unexpected_errors ?
			    ZIO_FLAG_SELF_HEAL : 0), NULL, NULL));
		}
	}
}

static void
vdev_mirror_state_change(vdev_t *vd, int faulted, int degraded)
{
	if (faulted == vd->vdev_children) {
		if (vdev_children_are_offline(vd)) {
			vdev_set_state(vd, B_FALSE, VDEV_STATE_OFFLINE,
			    VDEV_AUX_CHILDREN_OFFLINE);
		} else {
			vdev_set_state(vd, B_FALSE, VDEV_STATE_CANT_OPEN,
			    VDEV_AUX_NO_REPLICAS);
		}
	} else if (degraded + faulted != 0) {
		vdev_set_state(vd, B_FALSE, VDEV_STATE_DEGRADED, VDEV_AUX_NONE);
	} else {
		vdev_set_state(vd, B_FALSE, VDEV_STATE_HEALTHY, VDEV_AUX_NONE);
	}
}

/*
 * Return the maximum asize for a rebuild zio in the provided range.
 */
static uint64_t
vdev_mirror_rebuild_asize(vdev_t *vd, uint64_t start, uint64_t asize,
    uint64_t max_segment)
{
	(void) start;

	uint64_t psize = MIN(P2ROUNDUP(max_segment, 1 << vd->vdev_ashift),
	    SPA_MAXBLOCKSIZE);

	return (MIN(asize, vdev_psize_to_asize(vd, psize)));
}

vdev_ops_t vdev_mirror_ops = {
	.vdev_op_init = NULL,
	.vdev_op_fini = NULL,
	.vdev_op_open = vdev_mirror_open,
	.vdev_op_close = vdev_mirror_close,
	.vdev_op_asize = vdev_default_asize,
	.vdev_op_min_asize = vdev_default_min_asize,
	.vdev_op_min_alloc = NULL,
	.vdev_op_io_start = vdev_mirror_io_start,
	.vdev_op_io_done = vdev_mirror_io_done,
	.vdev_op_state_change = vdev_mirror_state_change,
	.vdev_op_need_resilver = vdev_default_need_resilver,
	.vdev_op_hold = NULL,
	.vdev_op_rele = NULL,
	.vdev_op_remap = NULL,
	.vdev_op_xlate = vdev_default_xlate,
	.vdev_op_rebuild_asize = vdev_mirror_rebuild_asize,
	.vdev_op_metaslab_init = NULL,
	.vdev_op_config_generate = NULL,
	.vdev_op_nparity = NULL,
	.vdev_op_ndisks = NULL,
	.vdev_op_type = VDEV_TYPE_MIRROR,	/* name of this vdev type */
	.vdev_op_leaf = B_FALSE			/* not a leaf vdev */
};

vdev_ops_t vdev_replacing_ops = {
	.vdev_op_init = NULL,
	.vdev_op_fini = NULL,
	.vdev_op_open = vdev_mirror_open,
	.vdev_op_close = vdev_mirror_close,
	.vdev_op_asize = vdev_default_asize,
	.vdev_op_min_asize = vdev_default_min_asize,
	.vdev_op_min_alloc = NULL,
	.vdev_op_io_start = vdev_mirror_io_start,
	.vdev_op_io_done = vdev_mirror_io_done,
	.vdev_op_state_change = vdev_mirror_state_change,
	.vdev_op_need_resilver = vdev_default_need_resilver,
	.vdev_op_hold = NULL,
	.vdev_op_rele = NULL,
	.vdev_op_remap = NULL,
	.vdev_op_xlate = vdev_default_xlate,
	.vdev_op_rebuild_asize = vdev_mirror_rebuild_asize,
	.vdev_op_metaslab_init = NULL,
	.vdev_op_config_generate = NULL,
	.vdev_op_nparity = NULL,
	.vdev_op_ndisks = NULL,
	.vdev_op_type = VDEV_TYPE_REPLACING,	/* name of this vdev type */
	.vdev_op_leaf = B_FALSE			/* not a leaf vdev */
};

vdev_ops_t vdev_spare_ops = {
	.vdev_op_init = NULL,
	.vdev_op_fini = NULL,
	.vdev_op_open = vdev_mirror_open,
	.vdev_op_close = vdev_mirror_close,
	.vdev_op_asize = vdev_default_asize,
	.vdev_op_min_asize = vdev_default_min_asize,
	.vdev_op_min_alloc = NULL,
	.vdev_op_io_start = vdev_mirror_io_start,
	.vdev_op_io_done = vdev_mirror_io_done,
	.vdev_op_state_change = vdev_mirror_state_change,
	.vdev_op_need_resilver = vdev_default_need_resilver,
	.vdev_op_hold = NULL,
	.vdev_op_rele = NULL,
	.vdev_op_remap = NULL,
	.vdev_op_xlate = vdev_default_xlate,
	.vdev_op_rebuild_asize = vdev_mirror_rebuild_asize,
	.vdev_op_metaslab_init = NULL,
	.vdev_op_config_generate = NULL,
	.vdev_op_nparity = NULL,
	.vdev_op_ndisks = NULL,
	.vdev_op_type = VDEV_TYPE_SPARE,	/* name of this vdev type */
	.vdev_op_leaf = B_FALSE			/* not a leaf vdev */
};

/* BEGIN CSTYLED */
ZFS_MODULE_PARAM(zfs_vdev_mirror, zfs_vdev_mirror_, rotating_inc, INT, ZMOD_RW,
	"Rotating media load increment for non-seeking I/O's");

ZFS_MODULE_PARAM(zfs_vdev_mirror, zfs_vdev_mirror_, rotating_seek_inc, INT, ZMOD_RW,
	"Rotating media load increment for seeking I/O's");

ZFS_MODULE_PARAM(zfs_vdev_mirror, zfs_vdev_mirror_, rotating_seek_offset, INT, ZMOD_RW,
	"Offset in bytes from the last I/O which triggers "
	"a reduced rotating media seek increment");

ZFS_MODULE_PARAM(zfs_vdev_mirror, zfs_vdev_mirror_, non_rotating_inc, INT, ZMOD_RW,
	"Non-rotating media load increment for non-seeking I/O's");

ZFS_MODULE_PARAM(zfs_vdev_mirror, zfs_vdev_mirror_, non_rotating_seek_inc, INT, ZMOD_RW,
	"Non-rotating media load increment for seeking I/O's");
/* END CSTYLED */
