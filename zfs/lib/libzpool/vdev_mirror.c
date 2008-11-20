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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"@(#)vdev_mirror.c	1.9	07/11/27 SMI"

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/vdev_impl.h>
#include <sys/zio.h>
#include <sys/fs/zfs.h>

/*
 * Virtual device vector for mirroring.
 */

typedef struct mirror_child {
	vdev_t		*mc_vd;
	uint64_t	mc_offset;
	int		mc_error;
	short		mc_tried;
	short		mc_skipped;
} mirror_child_t;

typedef struct mirror_map {
	int		mm_children;
	int		mm_replacing;
	int		mm_preferred;
	int		mm_root;
	mirror_child_t	mm_child[1];
} mirror_map_t;

int vdev_mirror_shift = 21;

static mirror_map_t *
vdev_mirror_map_alloc(zio_t *zio)
{
	mirror_map_t *mm = NULL;
	mirror_child_t *mc;
	vdev_t *vd = zio->io_vd;
	int c, d;

	if (vd == NULL) {
		dva_t *dva = zio->io_bp->blk_dva;
		spa_t *spa = zio->io_spa;

		c = BP_GET_NDVAS(zio->io_bp);

		mm = kmem_zalloc(offsetof(mirror_map_t, mm_child[c]), KM_SLEEP);
		mm->mm_children = c;
		mm->mm_replacing = B_FALSE;
		mm->mm_preferred = spa_get_random(c);
		mm->mm_root = B_TRUE;

		/*
		 * Check the other, lower-index DVAs to see if they're on
		 * the same vdev as the child we picked.  If they are, use
		 * them since they are likely to have been allocated from
		 * the primary metaslab in use at the time, and hence are
		 * more likely to have locality with single-copy data.
		 */
		for (c = mm->mm_preferred, d = c - 1; d >= 0; d--) {
			if (DVA_GET_VDEV(&dva[d]) == DVA_GET_VDEV(&dva[c]))
				mm->mm_preferred = d;
		}

		for (c = 0; c < mm->mm_children; c++) {
			mc = &mm->mm_child[c];

			mc->mc_vd = vdev_lookup_top(spa, DVA_GET_VDEV(&dva[c]));
			mc->mc_offset = DVA_GET_OFFSET(&dva[c]);
		}
	} else {
		c = vd->vdev_children;

		mm = kmem_zalloc(offsetof(mirror_map_t, mm_child[c]), KM_SLEEP);
		mm->mm_children = c;
		mm->mm_replacing = (vd->vdev_ops == &vdev_replacing_ops ||
		    vd->vdev_ops == &vdev_spare_ops);
		mm->mm_preferred = mm->mm_replacing ? 0 :
		    (zio->io_offset >> vdev_mirror_shift) % c;
		mm->mm_root = B_FALSE;

		for (c = 0; c < mm->mm_children; c++) {
			mc = &mm->mm_child[c];
			mc->mc_vd = vd->vdev_child[c];
			mc->mc_offset = zio->io_offset;
		}
	}

	zio->io_vsd = mm;
	return (mm);
}

static void
vdev_mirror_map_free(zio_t *zio)
{
	mirror_map_t *mm = zio->io_vsd;

	kmem_free(mm, offsetof(mirror_map_t, mm_child[mm->mm_children]));
	zio->io_vsd = NULL;
}

static int
vdev_mirror_open(vdev_t *vd, uint64_t *asize, uint64_t *ashift)
{
	vdev_t *cvd;
	uint64_t c;
	int numerrors = 0;
	int ret, lasterror = 0;

	if (vd->vdev_children == 0) {
		vd->vdev_stat.vs_aux = VDEV_AUX_BAD_LABEL;
		return (EINVAL);
	}

	for (c = 0; c < vd->vdev_children; c++) {
		cvd = vd->vdev_child[c];

		if ((ret = vdev_open(cvd)) != 0) {
			lasterror = ret;
			numerrors++;
			continue;
		}

		*asize = MIN(*asize - 1, cvd->vdev_asize - 1) + 1;
		*ashift = MAX(*ashift, cvd->vdev_ashift);
	}

	if (numerrors == vd->vdev_children) {
		vd->vdev_stat.vs_aux = VDEV_AUX_NO_REPLICAS;
		return (lasterror);
	}

	return (0);
}

static void
vdev_mirror_close(vdev_t *vd)
{
	uint64_t c;

	for (c = 0; c < vd->vdev_children; c++)
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
		zio_t *pio = zio->io_parent;
		mutex_enter(&pio->io_lock);
		ASSERT3U(zio->io_size, >=, pio->io_size);
		bcopy(zio->io_data, pio->io_data, pio->io_size);
		mutex_exit(&pio->io_lock);
	}

	zio_buf_free(zio->io_data, zio->io_size);

	mc->mc_error = zio->io_error;
	mc->mc_tried = 1;
	mc->mc_skipped = 0;
}

static void
vdev_mirror_repair_done(zio_t *zio)
{
	ASSERT(zio->io_private == zio->io_parent);
	vdev_mirror_map_free(zio->io_private);
}

/*
 * Try to find a child whose DTL doesn't contain the block we want to read.
 * If we can't, try the read on any vdev we haven't already tried.
 */
static int
vdev_mirror_child_select(zio_t *zio)
{
	mirror_map_t *mm = zio->io_vsd;
	mirror_child_t *mc;
	uint64_t txg = zio->io_txg;
	int i, c;

	ASSERT(zio->io_bp == NULL || zio->io_bp->blk_birth == txg);

	/*
	 * Try to find a child whose DTL doesn't contain the block to read.
	 * If a child is known to be completely inaccessible (indicated by
	 * vdev_readable() returning B_FALSE), don't even try.
	 */
	for (i = 0, c = mm->mm_preferred; i < mm->mm_children; i++, c++) {
		if (c >= mm->mm_children)
			c = 0;
		mc = &mm->mm_child[c];
		if (mc->mc_tried || mc->mc_skipped)
			continue;
		if (vdev_is_dead(mc->mc_vd) && !vdev_readable(mc->mc_vd)) {
			mc->mc_error = ENXIO;
			mc->mc_tried = 1;	/* don't even try */
			mc->mc_skipped = 1;
			continue;
		}
		if (!vdev_dtl_contains(&mc->mc_vd->vdev_dtl_map, txg, 1))
			return (c);
		mc->mc_error = ESTALE;
		mc->mc_skipped = 1;
	}

	/*
	 * Every device is either missing or has this txg in its DTL.
	 * Look for any child we haven't already tried before giving up.
	 */
	for (c = 0; c < mm->mm_children; c++)
		if (!mm->mm_child[c].mc_tried)
			return (c);

	/*
	 * Every child failed.  There's no place left to look.
	 */
	return (-1);
}

static int
vdev_mirror_io_start(zio_t *zio)
{
	mirror_map_t *mm;
	mirror_child_t *mc;
	int c, children;

	mm = vdev_mirror_map_alloc(zio);

	if (zio->io_type == ZIO_TYPE_READ) {
		if ((zio->io_flags & ZIO_FLAG_SCRUB) && !mm->mm_replacing) {
			/*
			 * For scrubbing reads we need to allocate a read
			 * buffer for each child and issue reads to all
			 * children.  If any child succeeds, it will copy its
			 * data into zio->io_data in vdev_mirror_scrub_done.
			 */
			for (c = 0; c < mm->mm_children; c++) {
				mc = &mm->mm_child[c];
				zio_nowait(zio_vdev_child_io(zio, zio->io_bp,
				    mc->mc_vd, mc->mc_offset,
				    zio_buf_alloc(zio->io_size), zio->io_size,
				    zio->io_type, zio->io_priority,
				    ZIO_FLAG_CANFAIL,
				    vdev_mirror_scrub_done, mc));
			}
			return (zio_wait_for_children_done(zio));
		}
		/*
		 * For normal reads just pick one child.
		 */
		c = vdev_mirror_child_select(zio);
		children = (c >= 0);
	} else {
		ASSERT(zio->io_type == ZIO_TYPE_WRITE);

		/*
		 * If this is a resilvering I/O to a replacing vdev,
		 * only the last child should be written -- unless the
		 * first child happens to have a DTL entry here as well.
		 * All other writes go to all children.
		 */
		if ((zio->io_flags & ZIO_FLAG_RESILVER) && mm->mm_replacing &&
		    !vdev_dtl_contains(&mm->mm_child[0].mc_vd->vdev_dtl_map,
		    zio->io_txg, 1)) {
			c = mm->mm_children - 1;
			children = 1;
		} else {
			c = 0;
			children = mm->mm_children;
		}
	}

	while (children--) {
		mc = &mm->mm_child[c];
		zio_nowait(zio_vdev_child_io(zio, zio->io_bp,
		    mc->mc_vd, mc->mc_offset,
		    zio->io_data, zio->io_size, zio->io_type, zio->io_priority,
		    ZIO_FLAG_CANFAIL, vdev_mirror_child_done, mc));
		c++;
	}

	return (zio_wait_for_children_done(zio));
}

static int
vdev_mirror_io_done(zio_t *zio)
{
	mirror_map_t *mm = zio->io_vsd;
	mirror_child_t *mc;
	int c;
	int good_copies = 0;
	int unexpected_errors = 0;

	zio->io_error = 0;
	zio->io_numerrors = 0;

	for (c = 0; c < mm->mm_children; c++) {
		mc = &mm->mm_child[c];

		if (mc->mc_tried && mc->mc_error == 0) {
			good_copies++;
			continue;
		}

		/*
		 * We preserve any EIOs because those may be worth retrying;
		 * whereas ECKSUM and ENXIO are more likely to be persistent.
		 */
		if (mc->mc_error) {
			if (zio->io_error != EIO)
				zio->io_error = mc->mc_error;
			if (!mc->mc_skipped)
				unexpected_errors++;
			zio->io_numerrors++;
		}
	}

	if (zio->io_type == ZIO_TYPE_WRITE) {
		/*
		 * XXX -- for now, treat partial writes as success.
		 * XXX -- For a replacing vdev, we need to make sure the
		 *	  new child succeeds.
		 */
		/* XXPOLICY */
		if (good_copies != 0)
			zio->io_error = 0;
		vdev_mirror_map_free(zio);
		return (ZIO_PIPELINE_CONTINUE);
	}

	ASSERT(zio->io_type == ZIO_TYPE_READ);

	/*
	 * If we don't have a good copy yet, keep trying other children.
	 */
	/* XXPOLICY */
	if (good_copies == 0 && (c = vdev_mirror_child_select(zio)) != -1) {
		ASSERT(c >= 0 && c < mm->mm_children);
		mc = &mm->mm_child[c];
		dprintf("retrying i/o (err=%d) on child %s\n",
		    zio->io_error, vdev_description(mc->mc_vd));
		zio->io_error = 0;
		zio_vdev_io_redone(zio);
		zio_nowait(zio_vdev_child_io(zio, zio->io_bp,
		    mc->mc_vd, mc->mc_offset, zio->io_data, zio->io_size,
		    ZIO_TYPE_READ, zio->io_priority, ZIO_FLAG_CANFAIL,
		    vdev_mirror_child_done, mc));
		return (zio_wait_for_children_done(zio));
	}

	/* XXPOLICY */
	if (good_copies)
		zio->io_error = 0;
	else
		ASSERT(zio->io_error != 0);

	if (good_copies && (spa_mode & FWRITE) &&
	    (unexpected_errors ||
	    (zio->io_flags & ZIO_FLAG_RESILVER) ||
	    ((zio->io_flags & ZIO_FLAG_SCRUB) && mm->mm_replacing))) {
		zio_t *rio;

		/*
		 * Use the good data we have in hand to repair damaged children.
		 *
		 * We issue all repair I/Os as children of 'rio' to arrange
		 * that vdev_mirror_map_free(zio) will be invoked after all
		 * repairs complete, but before we advance to the next stage.
		 */
		rio = zio_null(zio, zio->io_spa,
		    vdev_mirror_repair_done, zio, ZIO_FLAG_CANFAIL);

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
				if (mc->mc_tried)
					continue;
				if (!(zio->io_flags & ZIO_FLAG_SCRUB) &&
				    !vdev_dtl_contains(&mc->mc_vd->vdev_dtl_map,
				    zio->io_txg, 1))
					continue;
				mc->mc_error = ESTALE;
			}

			dprintf("resilvered %s @ 0x%llx error %d\n",
			    vdev_description(mc->mc_vd), mc->mc_offset,
			    mc->mc_error);

			zio_nowait(zio_vdev_child_io(rio, zio->io_bp, mc->mc_vd,
			    mc->mc_offset, zio->io_data, zio->io_size,
			    ZIO_TYPE_WRITE, zio->io_priority,
			    ZIO_FLAG_IO_REPAIR | ZIO_FLAG_CANFAIL |
			    ZIO_FLAG_DONT_PROPAGATE, NULL, NULL));
		}

		zio_nowait(rio);

		return (zio_wait_for_children_done(zio));
	}

	vdev_mirror_map_free(zio);

	return (ZIO_PIPELINE_CONTINUE);
}

static void
vdev_mirror_state_change(vdev_t *vd, int faulted, int degraded)
{
	if (faulted == vd->vdev_children)
		vdev_set_state(vd, B_FALSE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_NO_REPLICAS);
	else if (degraded + faulted != 0)
		vdev_set_state(vd, B_FALSE, VDEV_STATE_DEGRADED, VDEV_AUX_NONE);
	else
		vdev_set_state(vd, B_FALSE, VDEV_STATE_HEALTHY, VDEV_AUX_NONE);
}

vdev_ops_t vdev_mirror_ops = {
	vdev_mirror_open,
	vdev_mirror_close,
	NULL,
	vdev_default_asize,
	vdev_mirror_io_start,
	vdev_mirror_io_done,
	vdev_mirror_state_change,
	VDEV_TYPE_MIRROR,	/* name of this vdev type */
	B_FALSE			/* not a leaf vdev */
};

vdev_ops_t vdev_replacing_ops = {
	vdev_mirror_open,
	vdev_mirror_close,
	NULL,
	vdev_default_asize,
	vdev_mirror_io_start,
	vdev_mirror_io_done,
	vdev_mirror_state_change,
	VDEV_TYPE_REPLACING,	/* name of this vdev type */
	B_FALSE			/* not a leaf vdev */
};

vdev_ops_t vdev_spare_ops = {
	vdev_mirror_open,
	vdev_mirror_close,
	NULL,
	vdev_default_asize,
	vdev_mirror_io_start,
	vdev_mirror_io_done,
	vdev_mirror_state_change,
	VDEV_TYPE_SPARE,	/* name of this vdev type */
	B_FALSE			/* not a leaf vdev */
};
