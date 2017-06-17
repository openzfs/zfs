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
 * Copyright (c) 2012, 2017 by Delphix. All rights reserved.
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
#include <sys/dsl_bookmark.h>
#include <sys/zfs_ioctl.h>
#include <sys/zap.h>
#include <sys/zio_checksum.h>
#include <sys/zfs_znode.h>

struct diffarg {
	struct vnode *da_vp;		/* file to which we are reporting */
	offset_t *da_offp;
	int da_err;			/* error that stopped diff search */
	boolean_t da_blockwise;
	dmu_diff_record_t da_ddr;
};

static int
write_record(struct diffarg *da)
{
	ssize_t resid; /* have to get resid to get detailed errno */

	if (da->da_ddr.ddr_type == DDR_NONE) {
		da->da_err = 0;
		return (0);
	}

	da->da_err = vn_rdwr(UIO_WRITE, da->da_vp, (caddr_t)&da->da_ddr,
	    sizeof (da->da_ddr), 0, UIO_SYSSPACE, FAPPEND,
	    RLIM64_INFINITY, CRED(), &resid);
	*da->da_offp += sizeof (da->da_ddr);
	return (da->da_err);
}

static int
report_type(struct diffarg *da, diff_type_t t, uint64_t first, uint64_t last)
{
	ASSERT3U(first, <=, last);
	if (da->da_ddr.ddr_type != t ||
	    first != da->da_ddr.ddr_last + 1) {
		if (write_record(da) != 0)
			return (da->da_err);
		da->da_ddr.ddr_type = t;
		da->da_ddr.ddr_first = first;
		da->da_ddr.ddr_last = last;
		return (0);
	}
	da->da_ddr.ddr_last = last;
	return (0);
}

static inline boolean_t
overflow_multiply(uint64_t a, uint64_t b, uint64_t *c)
{
	uint64_t temp = a * b;
	if (b != 0 && temp / b != a)
		return (B_FALSE);
	*c = temp;
	return (B_TRUE);
}

/* ARGSUSED */
static int
diff_cb(spa_t *spa, zilog_t *zilog, const blkptr_t *bp,
    const zbookmark_phys_t *zb, const dnode_phys_t *dnp, void *arg)
{
	struct diffarg *da = arg;
	int err = 0;

	if (issig(JUSTLOOKING) && issig(FORREAL))
		return (SET_ERROR(EINTR));

	if (zb->zb_object != DMU_META_DNODE_OBJECT &&
	    DMU_OBJECT_IS_SPECIAL(zb->zb_object))
		return (0);

	if (bp == NULL) {
		/* This callback represents the dnode itself. */
		if (zb->zb_object == DMU_META_DNODE_OBJECT)
			return (0);
		if (dnp->dn_type == DMU_OT_NONE) {
			err = report_type(da, DDR_OBJECT_FREE,
			    zb->zb_object, zb->zb_object);
		} else {
			err = report_type(da, DDR_OBJECT_INUSE,
			    zb->zb_object, zb->zb_object);
		}
		return (err);
	}

	if (zb->zb_level < 0)
		return (0);

	if (zb->zb_object != DMU_META_DNODE_OBJECT &&
	    DMU_OBJECT_IS_SPECIAL(zb->zb_object))
		return (0);

	uint64_t span_blkids =
	    bp_span_in_blocks(dnp->dn_indblkshift, zb->zb_level);
	uint64_t start_blkid;

	/*
	 * See comment in send_cb().
	 */
	if (!overflow_multiply(span_blkids, zb->zb_blkid, &start_blkid) ||
	    (!DMU_OT_IS_METADATA(dnp->dn_type) &&
	    span_blkids * zb->zb_blkid > dnp->dn_maxblkid)) {
		ASSERT(BP_IS_HOLE(bp));
		return (0);
	}

	if (zb->zb_object == DMU_META_DNODE_OBJECT) {
		if (BP_IS_HOLE(bp)) {
			uint64_t dnobj = start_blkid << DNODES_PER_BLOCK_SHIFT;

			err = report_type(da, DDR_OBJECT_FREE, dnobj,
			    ((start_blkid + span_blkids) <<
			    DNODES_PER_BLOCK_SHIFT) - 1);
			if (err) {
				return (err);
			}
		}
	} else {
		if (!da->da_blockwise) {
			return (TRAVERSE_VISIT_NO_CHILDREN);
		}
		if (DMU_OBJECT_IS_SPECIAL(zb->zb_object))
			return (0);
		uint64_t blksz = dnp->dn_datablkszsec * SPA_MINBLOCKSIZE;
		uint64_t start_offset, end_offset;

		if (!overflow_multiply(start_blkid, blksz, &start_offset)) {
			ASSERT(BP_IS_HOLE(bp));
			return (0);
		}
		if (!overflow_multiply(start_blkid + span_blkids, blksz,
		    &end_offset)) {
			end_offset = UINT64_MAX;
		} else {
			end_offset--;
		}

		if (BP_IS_HOLE(bp)) {
			err = report_type(da, DDR_DATA_FREE,
			    start_offset, end_offset);
			if (err)
				return (err);
		} else if (zb->zb_level == 0) {
			err = report_type(da, DDR_DATA_INUSE,
			    start_offset, end_offset);
			if (err)
				return (err);
		}
	}

	return (0);
}

/*
 * Note, "from_name" must be a snapshot or a bookmark.
 * Both tosnap_name and from_name must be full names
 * (e.g. pool/fs[@#]snap_or_book), and "from_name" must be an earlier
 * snapshot or bookmark in tosnap_name's history.
 */
int
dmu_diff(const char *tosnap_name, const char *from_name,
    boolean_t blockwise, struct vnode *vp, offset_t *offp)
{
	struct diffarg da;
	dsl_dataset_t *tosnap;
	dsl_pool_t *dp;
	int error;
	uint64_t fromtxg;

	if (strchr(tosnap_name, '@') == NULL ||
	    strpbrk(from_name, "@#") == NULL)
		return (SET_ERROR(EINVAL));

	error = dsl_pool_hold(tosnap_name, FTAG, &dp);
	if (error != 0)
		return (error);

	error = dsl_dataset_hold(dp, tosnap_name, FTAG, &tosnap);
	if (error != 0) {
		dsl_pool_rele(dp, FTAG);
		return (error);
	}

	if (strchr(from_name, '#')) {
		zfs_bookmark_phys_t bmp;
		error = dsl_bookmark_lookup(dp, from_name, tosnap, &bmp);
		if (error != 0) {
			dsl_dataset_rele(tosnap, FTAG);
			dsl_pool_rele(dp, FTAG);
			return (error);
		}
		fromtxg = bmp.zbm_creation_txg;
	} else {
		dsl_dataset_t *fromsnap;
		error = dsl_dataset_hold(dp, from_name, FTAG, &fromsnap);
		if (error != 0) {
			dsl_dataset_rele(tosnap, FTAG);
			dsl_pool_rele(dp, FTAG);
			return (error);
		}

		if (!dsl_dataset_is_before(tosnap, fromsnap, 0)) {
			dsl_dataset_rele(fromsnap, FTAG);
			dsl_dataset_rele(tosnap, FTAG);
			dsl_pool_rele(dp, FTAG);
			return (SET_ERROR(EXDEV));
		}
		fromtxg = dsl_dataset_phys(fromsnap)->ds_creation_txg;
		dsl_dataset_rele(fromsnap, FTAG);
	}


	dsl_dataset_long_hold(tosnap, FTAG);
	dsl_pool_rele(dp, FTAG);

	da.da_vp = vp;
	da.da_offp = offp;
	da.da_ddr.ddr_type = DDR_NONE;
	da.da_ddr.ddr_first = da.da_ddr.ddr_last = 0;
	da.da_err = 0;
	da.da_blockwise = blockwise;

	error = traverse_dataset(tosnap, fromtxg,
	    TRAVERSE_PRE | TRAVERSE_PREFETCH_METADATA, diff_cb, &da);

	if (error != 0) {
		da.da_err = error;
	} else {
		/* we set the da.da_err we return as side-effect */
		(void) write_record(&da);
	}

	dsl_dataset_long_rele(tosnap, FTAG);
	dsl_dataset_rele(tosnap, FTAG);

	return (da.da_err);
}
