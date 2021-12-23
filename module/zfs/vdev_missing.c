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
 * Copyright (c) 2012, 2016 by Delphix. All rights reserved.
 */

/*
 * The 'missing' vdev is a special vdev type used only during import.  It
 * signifies a placeholder in the root vdev for some vdev that we know is
 * missing.  We pass it down to the kernel to allow the rest of the
 * configuration to parsed and an attempt made to open all available devices.
 * Because its GUID is always 0, we know that the guid sum will mismatch and we
 * won't be able to open the pool anyway.
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/vdev_impl.h>
#include <sys/fs/zfs.h>
#include <sys/zio.h>

static int
vdev_missing_open(vdev_t *vd, uint64_t *psize, uint64_t *max_psize,
    uint64_t *ashift, uint64_t *pshift)
{
	/*
	 * Really this should just fail.  But then the root vdev will be in the
	 * faulted state with VDEV_AUX_NO_REPLICAS, when what we really want is
	 * VDEV_AUX_BAD_GUID_SUM.  So we pretend to succeed, knowing that we
	 * will fail the GUID sum check before ever trying to open the pool.
	 */
	(void) vd;
	*psize = 0;
	*max_psize = 0;
	*ashift = 0;
	*pshift = 0;
	return (0);
}

static void
vdev_missing_close(vdev_t *vd)
{
	(void) vd;
}

static void
vdev_missing_io_start(zio_t *zio)
{
	zio->io_error = SET_ERROR(ENOTSUP);
	zio_execute(zio);
}

static void
vdev_missing_io_done(zio_t *zio)
{
	(void) zio;
}

vdev_ops_t vdev_missing_ops = {
	.vdev_op_init = NULL,
	.vdev_op_fini = NULL,
	.vdev_op_open = vdev_missing_open,
	.vdev_op_close = vdev_missing_close,
	.vdev_op_asize = vdev_default_asize,
	.vdev_op_min_asize = vdev_default_min_asize,
	.vdev_op_min_alloc = NULL,
	.vdev_op_io_start = vdev_missing_io_start,
	.vdev_op_io_done = vdev_missing_io_done,
	.vdev_op_state_change = NULL,
	.vdev_op_need_resilver = NULL,
	.vdev_op_hold = NULL,
	.vdev_op_rele = NULL,
	.vdev_op_remap = NULL,
	.vdev_op_xlate = NULL,
	.vdev_op_rebuild_asize = NULL,
	.vdev_op_metaslab_init = NULL,
	.vdev_op_config_generate = NULL,
	.vdev_op_nparity = NULL,
	.vdev_op_ndisks = NULL,
	.vdev_op_type = VDEV_TYPE_MISSING,	/* name of this vdev type */
	.vdev_op_leaf = B_TRUE			/* leaf vdev */
};

vdev_ops_t vdev_hole_ops = {
	.vdev_op_init = NULL,
	.vdev_op_fini = NULL,
	.vdev_op_open = vdev_missing_open,
	.vdev_op_close = vdev_missing_close,
	.vdev_op_asize = vdev_default_asize,
	.vdev_op_min_asize = vdev_default_min_asize,
	.vdev_op_min_alloc = NULL,
	.vdev_op_io_start = vdev_missing_io_start,
	.vdev_op_io_done = vdev_missing_io_done,
	.vdev_op_state_change = NULL,
	.vdev_op_need_resilver = NULL,
	.vdev_op_hold = NULL,
	.vdev_op_rele = NULL,
	.vdev_op_remap = NULL,
	.vdev_op_xlate = NULL,
	.vdev_op_rebuild_asize = NULL,
	.vdev_op_metaslab_init = NULL,
	.vdev_op_config_generate = NULL,
	.vdev_op_nparity = NULL,
	.vdev_op_ndisks = NULL,
	.vdev_op_type = VDEV_TYPE_HOLE,		/* name of this vdev type */
	.vdev_op_leaf = B_TRUE			/* leaf vdev */
};
