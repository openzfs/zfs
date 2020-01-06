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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2019, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
 * Copyright (c) 2020, Datto Inc. All rights reserved.
 */

#include <sys/dmu.h>
#include <sys/dmu_impl.h>
#include <sys/dmu_tx.h>
#include <sys/dbuf.h>
#include <sys/dnode.h>
#include <sys/zfs_context.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_traverse.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_synctask.h>
#include <sys/zfs_ioctl.h>
#include <sys/zap.h>
#include <sys/zio_checksum.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_file.h>

typedef struct snap_info {
	dsl_dataset_t	*si_ds;
	objset_t	*si_os;
} snap_info_t;

typedef struct dmu_diffarg {
	zfs_file_t *da_fp;		/* file to which we are reporting */
	offset_t *da_offp;
	int da_err;			/* error that stopped diff search */
	diff_type_t da_diff_type;
	uint64_t da_firstobj;
	uint64_t da_lastobj;
	dmu_diff_record_t da_ddr;
	snap_info_t da_from;
	snap_info_t da_to;
} dmu_diffarg_t;

static int
write_frees(dmu_diffarg_t *da)
{
	dmu_diff_record_t *ddr = &da->da_ddr;
	const offset_t size = offsetof(dmu_diff_record_t, ddr_zds[1]);
	objset_t *os = da->da_from.si_os;

	ddr->ddr_type = DDR_IN_FROM;
	ddr->ddr_obj = da->da_firstobj - 1;
	while (ddr->ddr_obj < da->da_lastobj) {
		int err;

		/* find next from dnode in free range */
		err = dmu_object_next(os, &ddr->ddr_obj, B_FALSE, 0);
		if (err == ESRCH || zfs_reserved_obj(ddr->ddr_obj) ||
		    ddr->ddr_obj > da->da_lastobj)
			return (0);

		if (err == 0) {
			err = zfs_diff_stats(os, ddr->ddr_obj, &ddr->ddr_zds[0],
			    NULL, 0);

			/* ignore dnodes that are already free (delete queue) */
			if (err == ESTALE)
				continue;
		}
		ddr->ddr_err[0] = err;

		err = zfs_file_write(da->da_fp, ddr, size, NULL);
		da->da_offp += size;

		if (err != 0)
			return (err);
		else if (ddr->ddr_err[0] != 0)
			return (ddr->ddr_err[0]);
	}

	return (0);
}

static int
write_inuse(dmu_diffarg_t *da)
{
	dmu_diff_record_t *ddr = &da->da_ddr;
	const offset_t size1 = offsetof(dmu_diff_record_t, ddr_zds[1]);
	const offset_t size2 = sizeof (dmu_diff_record_t);
	int err0, err1;

	err0 = ddr->ddr_err[0] = zfs_diff_stats(da->da_to.si_os, ddr->ddr_obj,
	    &ddr->ddr_zds[0], NULL, 0);
	err1 = ddr->ddr_err[1] = zfs_diff_stats(da->da_from.si_os, ddr->ddr_obj,
	    &ddr->ddr_zds[1], NULL, 0);

	if (err1 == 0) {
		ddr->ddr_type = DDR_IN_BOTH;
		da->da_err = zfs_file_write(da->da_fp, ddr, size2, NULL);
		da->da_offp += size2;
	} else {
		ddr->ddr_type = DDR_IN_TO;
		da->da_err = zfs_file_write(da->da_fp, ddr, size1, NULL);
		da->da_offp += size1;
	}

	/* ENOENT, ENOTSUP and ESTALE are handled by client */
	if (err0 && err0 != ENOENT && err0 != ENOTSUP && err0 != ESTALE)
		return (err0);
	if (err1 && err1 != ENOENT && err1 != ENOTSUP && err1 != ESTALE)
		return (err1);
	return (da->da_err);
}

static int
write_record(dmu_diffarg_t *da)
{
	dmu_diff_record_t *ddr = &da->da_ddr;

	ddr->ddr_type = da->da_diff_type;
	if (ddr->ddr_type == DDR_NONE) {
		da->da_err = 0;
	} else if (da->da_diff_type == DDR_IN_FROM) {
		da->da_err = write_frees(da);
	} else {
		for (ddr->ddr_obj = da->da_firstobj;
		    ddr->ddr_obj <= da->da_lastobj; ddr->ddr_obj++) {
			da->da_err = write_inuse(da);
			if (da->da_err != 0)
				break;
		}
	}

	return (da->da_err);
}

static int
report_free_dnode_range(dmu_diffarg_t *da, uint64_t first, uint64_t last)
{
	ASSERT(first <= last);
	if (da->da_diff_type != DDR_IN_FROM || first != da->da_lastobj + 1) {
		if (write_record(da) != 0)
			return (da->da_err);
		da->da_diff_type = DDR_IN_FROM;
		da->da_firstobj = first;
		da->da_lastobj = last;
		return (0);
	}
	da->da_lastobj = last;
	return (0);
}

static int
report_dnode(dmu_diffarg_t *da, uint64_t object, dnode_phys_t *dnp)
{
	ASSERT(dnp != NULL);
	if (dnp->dn_type == DMU_OT_NONE)
		return (report_free_dnode_range(da, object, object));

	if (da->da_diff_type != DDR_IN_TO || object != da->da_lastobj + 1) {
		if (write_record(da) != 0)
			return (da->da_err);
		da->da_diff_type = DDR_IN_TO;
		da->da_firstobj = da->da_lastobj = object;
		return (0);
	}
	da->da_lastobj = object;
	return (0);
}

#define	DBP_SPAN(dnp, level)				  \
	(((uint64_t)dnp->dn_datablkszsec) << (SPA_MINBLOCKSHIFT + \
	(level) * (dnp->dn_indblkshift - SPA_BLKPTRSHIFT)))

/* ARGSUSED */
static int
diff_cb(spa_t *spa, zilog_t *zilog, const blkptr_t *bp,
    const zbookmark_phys_t *zb, const dnode_phys_t *dnp, void *arg)
{
	dmu_diffarg_t *da = arg;
	int err = 0;

	if (issig(JUSTLOOKING) && issig(FORREAL))
		return (SET_ERROR(EINTR));

	if (zb->zb_level == ZB_DNODE_LEVEL ||
	    zb->zb_object != DMU_META_DNODE_OBJECT)
		return (0);

	if (BP_IS_HOLE(bp)) {
		uint64_t span = DBP_SPAN(dnp, zb->zb_level);
		uint64_t dnobj = (zb->zb_blkid * span) >> DNODE_SHIFT;

		err = report_free_dnode_range(da, dnobj,
		    dnobj + (span >> DNODE_SHIFT) - 1);
		if (err)
			return (err);
	} else if (zb->zb_level == 0) {
		dnode_phys_t *blk;
		arc_buf_t *abuf;
		arc_flags_t aflags = ARC_FLAG_WAIT;
		int epb = BP_GET_LSIZE(bp) >> DNODE_SHIFT;
		int zio_flags = ZIO_FLAG_CANFAIL;
		int i;

		if (BP_IS_PROTECTED(bp))
			zio_flags |= ZIO_FLAG_RAW;

		if (arc_read(NULL, spa, bp, arc_getbuf_func, &abuf,
		    ZIO_PRIORITY_ASYNC_READ, zio_flags, &aflags, zb) != 0)
			return (SET_ERROR(EIO));

		blk = abuf->b_data;
		for (i = 0; i < epb; i += blk[i].dn_extra_slots + 1) {
			uint64_t dnobj = (zb->zb_blkid <<
			    (DNODE_BLOCK_SHIFT - DNODE_SHIFT)) + i;
			err = report_dnode(da, dnobj, blk+i);
			if (err)
				break;
		}
		arc_buf_destroy(abuf, &abuf);
		if (err)
			return (err);
		/* Don't care about the data blocks */
		return (TRAVERSE_VISIT_NO_CHILDREN);
	}
	return (0);
}

static int
si_setup(snap_info_t *si, dsl_pool_t *dp, const char *snap, void *tag)
{
	int err;

	if (strchr(snap, '@') == NULL)
		return (SET_ERROR(EINVAL));

	err = dsl_dataset_hold_flags(dp, snap, DS_HOLD_FLAG_DECRYPT, tag,
	    &si->si_ds);
	if (err != 0)
		return (err);

	return (dmu_objset_from_ds(si->si_ds, &si->si_os));
}

static void
si_teardown(snap_info_t *si, boolean_t long_hold, void *tag)
{
	if (si->si_ds) {
		if (long_hold)
			dsl_dataset_long_rele(si->si_ds, tag);
		dsl_dataset_rele_flags(si->si_ds, DS_HOLD_FLAG_DECRYPT, tag);
	}
}

int
dmu_diff(const char *tosnap_name, const char *fromsnap_name,
    zfs_file_t *fp, offset_t *offp)
{
	dmu_diffarg_t da = { 0 };
	dsl_pool_t *dp = NULL;
	int error;
	uint64_t fromtxg;

	error = dsl_pool_hold(tosnap_name, FTAG, &dp);
	if (error != 0)
		return (error);

	error = si_setup(&da.da_to, dp, tosnap_name, FTAG);
	if (error != 0)
		goto done;
	error = si_setup(&da.da_from, dp, fromsnap_name, FTAG);
	if (error != 0)
		goto done;

	if (!dsl_dataset_is_before(da.da_to.si_ds, da.da_from.si_ds, 0)) {
		error = SET_ERROR(EXDEV);
		goto done;
	}

	fromtxg = dsl_dataset_phys(da.da_from.si_ds)->ds_creation_txg;

	dsl_dataset_long_hold(da.da_to.si_ds, FTAG);
	dsl_dataset_long_hold(da.da_from.si_ds, FTAG);
	dsl_pool_rele(dp, FTAG);
	dp = NULL;

	da.da_fp = fp;
	da.da_offp = offp;
	da.da_ddr.ddr_type = DDR_NONE;

	/*
	 * Since zfs diff only looks at dnodes which are stored in plaintext
	 * (other than bonus buffers), we don't technically need to decrypt
	 * the dataset to perform this operation. However, the command line
	 * utility will still fail if the keys are not loaded because the
	 * dataset isn't mounted and because it will fail when it attempts to
	 * call the ZFS_IOC_OBJ_TO_STATS ioctl.
	 */
	error = traverse_dataset(da.da_to.si_ds, fromtxg,
	    TRAVERSE_PRE | TRAVERSE_PREFETCH_METADATA | TRAVERSE_NO_DECRYPT,
	    diff_cb, &da);

	if (error == 0) {
		/* we set the da.da_err we return as side-effect */
		(void) write_record(&da);
		error = da.da_err;
	}

done:
	if (dp)
		dsl_pool_rele(dp, FTAG);
	si_teardown(&da.da_from, dp == NULL, FTAG);
	si_teardown(&da.da_to, dp == NULL, FTAG);

	return (error);
}
