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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2014 by Delphix. All rights reserved.
 * Copyright (c) 2016 Gvozden Nešković. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/vdev_impl.h>
#include <sys/zio.h>
#include <sys/zio_checksum.h>
#include <sys/abd.h>
#include <sys/fs/zfs.h>
#include <sys/fm/fs/zfs.h>
#include <sys/vdev_raidz.h>
#include <sys/vdev_raidz_impl.h>

/*
 * Virtual device vector for RAID-Z.
 *
 * This vdev supports single, double, and triple parity. For single parity,
 * we use a simple XOR of all the data columns. For double or triple parity,
 * we use a special case of Reed-Solomon coding. This extends the
 * technique described in "The mathematics of RAID-6" by H. Peter Anvin by
 * drawing on the system described in "A Tutorial on Reed-Solomon Coding for
 * Fault-Tolerance in RAID-like Systems" by James S. Plank on which the
 * former is also based. The latter is designed to provide higher performance
 * for writes.
 *
 * Note that the Plank paper claimed to support arbitrary N+M, but was then
 * amended six years later identifying a critical flaw that invalidates its
 * claims. Nevertheless, the technique can be adapted to work for up to
 * triple parity. For additional parity, the amendment "Note: Correction to
 * the 1997 Tutorial on Reed-Solomon Coding" by James S. Plank and Ying Ding
 * is viable, but the additional complexity means that write performance will
 * suffer.
 *
 * All of the methods above operate on a Galois field, defined over the
 * integers mod 2^N. In our case we choose N=8 for GF(8) so that all elements
 * can be expressed with a single byte. Briefly, the operations on the
 * field are defined as follows:
 *
 *   o addition (+) is represented by a bitwise XOR
 *   o subtraction (-) is therefore identical to addition: A + B = A - B
 *   o multiplication of A by 2 is defined by the following bitwise expression:
 *
 *	(A * 2)_7 = A_6
 *	(A * 2)_6 = A_5
 *	(A * 2)_5 = A_4
 *	(A * 2)_4 = A_3 + A_7
 *	(A * 2)_3 = A_2 + A_7
 *	(A * 2)_2 = A_1 + A_7
 *	(A * 2)_1 = A_0
 *	(A * 2)_0 = A_7
 *
 * In C, multiplying by 2 is therefore ((a << 1) ^ ((a & 0x80) ? 0x1d : 0)).
 * As an aside, this multiplication is derived from the error correcting
 * primitive polynomial x^8 + x^4 + x^3 + x^2 + 1.
 *
 * Observe that any number in the field (except for 0) can be expressed as a
 * power of 2 -- a generator for the field. We store a table of the powers of
 * 2 and logs base 2 for quick look ups, and exploit the fact that A * B can
 * be rewritten as 2^(log_2(A) + log_2(B)) (where '+' is normal addition rather
 * than field addition). The inverse of a field element A (A^-1) is therefore
 * A ^ (255 - 1) = A^254.
 *
 * The up-to-three parity columns, P, Q, R over several data columns,
 * D_0, ... D_n-1, can be expressed by field operations:
 *
 *	P = D_0 + D_1 + ... + D_n-2 + D_n-1
 *	Q = 2^n-1 * D_0 + 2^n-2 * D_1 + ... + 2^1 * D_n-2 + 2^0 * D_n-1
 *	  = ((...((D_0) * 2 + D_1) * 2 + ...) * 2 + D_n-2) * 2 + D_n-1
 *	R = 4^n-1 * D_0 + 4^n-2 * D_1 + ... + 4^1 * D_n-2 + 4^0 * D_n-1
 *	  = ((...((D_0) * 4 + D_1) * 4 + ...) * 4 + D_n-2) * 4 + D_n-1
 *
 * We chose 1, 2, and 4 as our generators because 1 corresponds to the trival
 * XOR operation, and 2 and 4 can be computed quickly and generate linearly-
 * independent coefficients. (There are no additional coefficients that have
 * this property which is why the uncorrected Plank method breaks down.)
 *
 * See the reconstruction code below for how P, Q and R can used individually
 * or in concert to recover missing data columns.
 */

#define	VDEV_RAIDZ_P		0
#define	VDEV_RAIDZ_Q		1
#define	VDEV_RAIDZ_R		2

void
vdev_raidz_map_free(raidz_map_t *rm)
{
	int c;

	for (c = 0; c < rm->rm_firstdatacol; c++) {
		abd_free(rm->rm_col[c].rc_abd);

		if (rm->rm_col[c].rc_gdata != NULL)
			abd_free(rm->rm_col[c].rc_gdata);
	}

	for (c = rm->rm_firstdatacol; c < rm->rm_cols; c++) {
		abd_put(rm->rm_col[c].rc_abd);
	}

	if (rm->rm_abd_copy != NULL)
		abd_free(rm->rm_abd_copy);

	kmem_free(rm, offsetof(raidz_map_t, rm_col[rm->rm_scols]));
}

static void
vdev_raidz_map_free_vsd(zio_t *zio)
{
	raidz_map_t *rm = zio->io_vsd;

	ASSERT0(rm->rm_freed);
	rm->rm_freed = 1;

	if (rm->rm_reports == 0)
		vdev_raidz_map_free(rm);
}

/*ARGSUSED*/
static void
vdev_raidz_cksum_free(void *arg, size_t ignored)
{
	raidz_map_t *rm = arg;

	ASSERT3U(rm->rm_reports, >, 0);

	if (--rm->rm_reports == 0 && rm->rm_freed != 0)
		vdev_raidz_map_free(rm);
}

static void
vdev_raidz_cksum_finish(zio_cksum_report_t *zcr, const abd_t *good_data)
{
	raidz_map_t *rm = zcr->zcr_cbdata;
	const size_t c = zcr->zcr_cbinfo;
	size_t x, offset;

	const abd_t *good = NULL;
	const abd_t *bad = rm->rm_col[c].rc_abd;

	if (good_data == NULL) {
		zfs_ereport_finish_checksum(zcr, NULL, NULL, B_FALSE);
		return;
	}

	if (c < rm->rm_firstdatacol) {
		/*
		 * The first time through, calculate the parity blocks for
		 * the good data (this relies on the fact that the good
		 * data never changes for a given logical ZIO)
		 */
		if (rm->rm_col[0].rc_gdata == NULL) {
			abd_t *bad_parity[VDEV_RAIDZ_MAXPARITY];

			/*
			 * Set up the rm_col[]s to generate the parity for
			 * good_data, first saving the parity bufs and
			 * replacing them with buffers to hold the result.
			 */
			for (x = 0; x < rm->rm_firstdatacol; x++) {
				bad_parity[x] = rm->rm_col[x].rc_abd;
				rm->rm_col[x].rc_abd =
				    rm->rm_col[x].rc_gdata =
				    abd_alloc_for_io(rm->rm_col[x].rc_size,
				    B_FALSE);
			}

			/* fill in the data columns from good_data */
			offset = 0;
			for (; x < rm->rm_cols; x++) {
				abd_put(rm->rm_col[x].rc_abd);

				rm->rm_col[x].rc_abd =
				    abd_get_offset_size((abd_t *)good_data,
				    offset, rm->rm_col[x].rc_size);
				offset += rm->rm_col[x].rc_size;
			}

			/*
			 * Construct the parity from the good data.
			 */
			vdev_raidz_generate_parity(rm);

			/* restore everything back to its original state */
			for (x = 0; x < rm->rm_firstdatacol; x++)
				rm->rm_col[x].rc_abd = bad_parity[x];

			offset = 0;
			for (x = rm->rm_firstdatacol; x < rm->rm_cols; x++) {
				abd_put(rm->rm_col[x].rc_abd);
				rm->rm_col[x].rc_abd = abd_get_offset_size(
				    rm->rm_abd_copy, offset,
				    rm->rm_col[x].rc_size);
				offset += rm->rm_col[x].rc_size;
			}
		}

		ASSERT3P(rm->rm_col[c].rc_gdata, !=, NULL);
		good = abd_get_offset_size(rm->rm_col[c].rc_gdata, 0, rm->rm_col[c].rc_size);
	} else {
		/* adjust good_data to point at the start of our column */
		offset = 0;
		for (x = rm->rm_firstdatacol; x < c; x++)
			offset += rm->rm_col[x].rc_size;

		good = abd_get_offset_size((abd_t *)good_data, offset,
		    rm->rm_col[c].rc_size);
	}

	/* we drop the ereport if it ends up that the data was good */
	zfs_ereport_finish_checksum(zcr, good, bad, B_TRUE);
	abd_put((abd_t *)good);
}

/*
 * Invoked indirectly by zfs_ereport_start_checksum(), called
 * below when our read operation fails completely.  The main point
 * is to keep a copy of everything we read from disk, so that at
 * vdev_raidz_cksum_finish() time we can compare it with the good data.
 */
static void
vdev_raidz_cksum_report(zio_t *zio, zio_cksum_report_t *zcr, void *arg)
{
	size_t c = (size_t)(uintptr_t)arg;
	size_t offset;

	raidz_map_t *rm = zio->io_vsd;
	size_t size;

	/* set up the report and bump the refcount  */
	zcr->zcr_cbdata = rm;
	zcr->zcr_cbinfo = c;
	zcr->zcr_finish = vdev_raidz_cksum_finish;
	zcr->zcr_free = vdev_raidz_cksum_free;

	rm->rm_reports++;
	ASSERT3U(rm->rm_reports, >, 0);

	if (rm->rm_abd_copy != NULL)
		return;

	/*
	 * It's the first time we're called for this raidz_map_t, so we need
	 * to copy the data aside; there's no guarantee that our zio's buffer
	 * won't be re-used for something else.
	 *
	 * Our parity data is already in separate buffers, so there's no need
	 * to copy them.
	 */

	size = 0;
	for (c = rm->rm_firstdatacol; c < rm->rm_cols; c++)
		size += rm->rm_col[c].rc_size;

	rm->rm_abd_copy = abd_alloc_for_io(size, B_FALSE);

	for (offset = 0, c = rm->rm_firstdatacol; c < rm->rm_cols; c++) {
		raidz_col_t *col = &rm->rm_col[c];
		abd_t *tmp = abd_get_offset_size(rm->rm_abd_copy, offset,
		    col->rc_size);

		abd_copy(tmp, col->rc_abd, col->rc_size);

		abd_put(col->rc_abd);
		col->rc_abd = tmp;

		offset += col->rc_size;
	}
	ASSERT3U(offset, ==, size);
}

static const zio_vsd_ops_t vdev_raidz_vsd_ops = {
	vdev_raidz_map_free_vsd,
	vdev_raidz_cksum_report
};

/*
 * Divides the IO evenly across all child vdevs; usually, dcols is
 * the number of children in the target vdev.
 *
 * Avoid inlining the function to keep vdev_raidz_io_start(), which
 * is this functions only caller, as small as possible on the stack.
 */
noinline raidz_map_t *
vdev_raidz_map_alloc(zio_t *zio, uint64_t unit_shift, uint64_t dcols,
    uint64_t nparity)
{
	raidz_map_t *rm;
	/* The starting RAIDZ (parent) vdev sector of the block. */
	uint64_t b = zio->io_offset >> unit_shift;
	/* The zio's size in units of the vdev's minimum sector size. */
	uint64_t s = zio->io_size >> unit_shift;
	/* The first column for this stripe. */
	uint64_t f = b % dcols;
	/* The starting byte offset on each child vdev. */
	uint64_t o = (b / dcols) << unit_shift;
	uint64_t q, r, c, bc, col, acols, scols, coff, devidx, asize, tot;
	uint64_t off = 0;

	/*
	 * "Quotient": The number of data sectors for this stripe on all but
	 * the "big column" child vdevs that also contain "remainder" data.
	 */
	q = s / (dcols - nparity);

	/*
	 * "Remainder": The number of partial stripe data sectors in this I/O.
	 * This will add a sector to some, but not all, child vdevs.
	 */
	r = s - q * (dcols - nparity);

	/* The number of "big columns" - those which contain remainder data. */
	bc = (r == 0 ? 0 : r + nparity);

	/*
	 * The total number of data and parity sectors associated with
	 * this I/O.
	 */
	tot = s + nparity * (q + (r == 0 ? 0 : 1));

	/* acols: The columns that will be accessed. */
	/* scols: The columns that will be accessed or skipped. */
	if (q == 0) {
		/* Our I/O request doesn't span all child vdevs. */
		acols = bc;
		scols = MIN(dcols, roundup(bc, nparity + 1));
	} else {
		acols = dcols;
		scols = dcols;
	}

	ASSERT3U(acols, <=, scols);

	rm = kmem_alloc(offsetof(raidz_map_t, rm_col[scols]), KM_SLEEP);

	rm->rm_cols = acols;
	rm->rm_scols = scols;
	rm->rm_bigcols = bc;
	rm->rm_skipstart = bc;
	rm->rm_missingdata = 0;
	rm->rm_missingparity = 0;
	rm->rm_firstdatacol = nparity;
	rm->rm_abd_copy = NULL;
	rm->rm_reports = 0;
	rm->rm_freed = 0;
	rm->rm_ecksuminjected = 0;

	asize = 0;

	for (c = 0; c < scols; c++) {
		col = f + c;
		coff = o;
		if (col >= dcols) {
			col -= dcols;
			coff += 1ULL << unit_shift;
		}
		rm->rm_col[c].rc_devidx = col;
		rm->rm_col[c].rc_offset = coff;
		rm->rm_col[c].rc_abd = NULL;
		rm->rm_col[c].rc_gdata = NULL;
		rm->rm_col[c].rc_error = 0;
		rm->rm_col[c].rc_tried = 0;
		rm->rm_col[c].rc_skipped = 0;

		if (c >= acols)
			rm->rm_col[c].rc_size = 0;
		else if (c < bc)
			rm->rm_col[c].rc_size = (q + 1) << unit_shift;
		else
			rm->rm_col[c].rc_size = q << unit_shift;

		asize += rm->rm_col[c].rc_size;
	}

	ASSERT3U(asize, ==, tot << unit_shift);
	rm->rm_asize = roundup(asize, (nparity + 1) << unit_shift);
	rm->rm_nskip = roundup(tot, nparity + 1) - tot;
	ASSERT3U(rm->rm_asize - asize, ==, rm->rm_nskip << unit_shift);
	ASSERT3U(rm->rm_nskip, <=, nparity);

	for (c = 0; c < rm->rm_firstdatacol; c++)
		rm->rm_col[c].rc_abd =
		    abd_alloc_for_io(rm->rm_col[c].rc_size, B_FALSE);

	rm->rm_col[c].rc_abd = abd_get_offset_size(zio->io_abd, 0,
	    rm->rm_col[c].rc_size);
	off = rm->rm_col[c].rc_size;

	for (c = c + 1; c < acols; c++) {
		rm->rm_col[c].rc_abd = abd_get_offset_size(zio->io_abd, off,
		    rm->rm_col[c].rc_size);
		off += rm->rm_col[c].rc_size;
	}

	/*
	 * If all data stored spans all columns, there's a danger that parity
	 * will always be on the same device and, since parity isn't read
	 * during normal operation, that that device's I/O bandwidth won't be
	 * used effectively. We therefore switch the parity every 1MB.
	 *
	 * ... at least that was, ostensibly, the theory. As a practical
	 * matter unless we juggle the parity between all devices evenly, we
	 * won't see any benefit. Further, occasional writes that aren't a
	 * multiple of the LCM of the number of children and the minimum
	 * stripe width are sufficient to avoid pessimal behavior.
	 * Unfortunately, this decision created an implicit on-disk format
	 * requirement that we need to support for all eternity, but only
	 * for single-parity RAID-Z.
	 *
	 * If we intend to skip a sector in the zeroth column for padding
	 * we must make sure to note this swap. We will never intend to
	 * skip the first column since at least one data and one parity
	 * column must appear in each row.
	 */
	ASSERT(rm->rm_cols >= 2);
	ASSERT(rm->rm_col[0].rc_size == rm->rm_col[1].rc_size);

	if (rm->rm_firstdatacol == 1 && (zio->io_offset & (1ULL << 20))) {
		devidx = rm->rm_col[0].rc_devidx;
		o = rm->rm_col[0].rc_offset;
		rm->rm_col[0].rc_devidx = rm->rm_col[1].rc_devidx;
		rm->rm_col[0].rc_offset = rm->rm_col[1].rc_offset;
		rm->rm_col[1].rc_devidx = devidx;
		rm->rm_col[1].rc_offset = o;

		if (rm->rm_skipstart == 0)
			rm->rm_skipstart = 1;
	}

	zio->io_vsd = rm;
	zio->io_vsd_ops = &vdev_raidz_vsd_ops;

	/* init RAIDZ parity ops */
	rm->rm_ops = vdev_raidz_math_get_ops();

	return (rm);
}

/*
 * Generate RAID parity in the first virtual columns according to the number of
 * parity columns available.
 */
void
vdev_raidz_generate_parity(raidz_map_t *rm)
{
	vdev_raidz_math_generate(rm);
}

int
vdev_raidz_reconstruct(raidz_map_t *rm, const int *t, int nt)
{
	int tgts[VDEV_RAIDZ_MAXPARITY], *dt;
	int ntgts;
	int i, c;
	int nbadparity, nbaddata;
	int parity_valid[VDEV_RAIDZ_MAXPARITY];

	/*
	 * The tgts list must already be sorted.
	 */
	for (i = 1; i < nt; i++) {
		ASSERT(t[i] > t[i - 1]);
	}

	nbadparity = rm->rm_firstdatacol;
	nbaddata = rm->rm_cols - nbadparity;
	ntgts = 0;
	for (i = 0, c = 0; c < rm->rm_cols; c++) {
		if (c < rm->rm_firstdatacol)
			parity_valid[c] = B_FALSE;

		if (i < nt && c == t[i]) {
			tgts[ntgts++] = c;
			i++;
		} else if (rm->rm_col[c].rc_error != 0) {
			tgts[ntgts++] = c;
		} else if (c >= rm->rm_firstdatacol) {
			nbaddata--;
		} else {
			parity_valid[c] = B_TRUE;
			nbadparity--;
		}
	}

	ASSERT(ntgts >= nt);
	ASSERT(nbaddata >= 0);
	ASSERT(nbaddata + nbadparity == ntgts);

	dt = &tgts[nbadparity];

	/* Reconstruct using the new math implementation */
	return (vdev_raidz_math_reconstruct(rm, parity_valid, dt, nbaddata));
}

static int
vdev_raidz_open(vdev_t *vd, uint64_t *asize, uint64_t *max_asize,
    uint64_t *ashift)
{
	vdev_t *cvd;
	uint64_t nparity = vd->vdev_nparity;
	int c;
	int lasterror = 0;
	int numerrors = 0;

	ASSERT(nparity > 0);

	if (nparity > VDEV_RAIDZ_MAXPARITY ||
	    vd->vdev_children < nparity + 1) {
		vd->vdev_stat.vs_aux = VDEV_AUX_BAD_LABEL;
		return (SET_ERROR(EINVAL));
	}

	vdev_open_children(vd);

	for (c = 0; c < vd->vdev_children; c++) {
		cvd = vd->vdev_child[c];

		if (cvd->vdev_open_error != 0) {
			lasterror = cvd->vdev_open_error;
			numerrors++;
			continue;
		}

		*asize = MIN(*asize - 1, cvd->vdev_asize - 1) + 1;
		*max_asize = MIN(*max_asize - 1, cvd->vdev_max_asize - 1) + 1;
		*ashift = MAX(*ashift, cvd->vdev_ashift);
	}

	*asize *= vd->vdev_children;
	*max_asize *= vd->vdev_children;

	if (numerrors > nparity) {
		vd->vdev_stat.vs_aux = VDEV_AUX_NO_REPLICAS;
		return (lasterror);
	}

	return (0);
}

static void
vdev_raidz_close(vdev_t *vd)
{
	int c;

	for (c = 0; c < vd->vdev_children; c++)
		vdev_close(vd->vdev_child[c]);
}

static uint64_t
vdev_raidz_asize(vdev_t *vd, uint64_t psize)
{
	uint64_t asize;
	uint64_t ashift = vd->vdev_top->vdev_ashift;
	uint64_t cols = vd->vdev_children;
	uint64_t nparity = vd->vdev_nparity;

	asize = ((psize - 1) >> ashift) + 1;
	asize += nparity * ((asize + cols - nparity - 1) / (cols - nparity));
	asize = roundup(asize, nparity + 1) << ashift;

	return (asize);
}

static void
vdev_raidz_child_done(zio_t *zio)
{
	raidz_col_t *rc = zio->io_private;

	rc->rc_error = zio->io_error;
	rc->rc_tried = 1;
	rc->rc_skipped = 0;
}

/*
 * Start an IO operation on a RAIDZ VDev
 *
 * Outline:
 * - For write operations:
 *   1. Generate the parity data
 *   2. Create child zio write operations to each column's vdev, for both
 *      data and parity.
 *   3. If the column skips any sectors for padding, create optional dummy
 *      write zio children for those areas to improve aggregation continuity.
 * - For read operations:
 *   1. Create child zio read operations to each data column's vdev to read
 *      the range of data required for zio.
 *   2. If this is a scrub or resilver operation, or if any of the data
 *      vdevs have had errors, then create zio read operations to the parity
 *      columns' VDevs as well.
 */
static void
vdev_raidz_io_start(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;
	vdev_t *tvd = vd->vdev_top;
	vdev_t *cvd;
	raidz_map_t *rm;
	raidz_col_t *rc;
	int c, i;

	rm = vdev_raidz_map_alloc(zio, tvd->vdev_ashift, vd->vdev_children,
	    vd->vdev_nparity);

	ASSERT3U(rm->rm_asize, ==, vdev_psize_to_asize(vd, zio->io_size));

	if (zio->io_type == ZIO_TYPE_WRITE) {
		vdev_raidz_generate_parity(rm);

		for (c = 0; c < rm->rm_cols; c++) {
			rc = &rm->rm_col[c];
			cvd = vd->vdev_child[rc->rc_devidx];
			zio_nowait(zio_vdev_child_io(zio, NULL, cvd,
			    rc->rc_offset, rc->rc_abd, rc->rc_size,
			    zio->io_type, zio->io_priority, 0,
			    vdev_raidz_child_done, rc));
		}

		/*
		 * Generate optional I/Os for any skipped sectors to improve
		 * aggregation contiguity.
		 */
		for (c = rm->rm_skipstart, i = 0; i < rm->rm_nskip; c++, i++) {
			ASSERT(c <= rm->rm_scols);
			if (c == rm->rm_scols)
				c = 0;
			rc = &rm->rm_col[c];
			cvd = vd->vdev_child[rc->rc_devidx];
			zio_nowait(zio_vdev_child_io(zio, NULL, cvd,
			    rc->rc_offset + rc->rc_size, NULL,
			    1 << tvd->vdev_ashift,
			    zio->io_type, zio->io_priority,
			    ZIO_FLAG_NODATA | ZIO_FLAG_OPTIONAL, NULL, NULL));
		}

		zio_execute(zio);
		return;
	}

	ASSERT(zio->io_type == ZIO_TYPE_READ);

	/*
	 * Iterate over the columns in reverse order so that we hit the parity
	 * last -- any errors along the way will force us to read the parity.
	 */
	for (c = rm->rm_cols - 1; c >= 0; c--) {
		rc = &rm->rm_col[c];
		cvd = vd->vdev_child[rc->rc_devidx];
		if (!vdev_readable(cvd)) {
			if (c >= rm->rm_firstdatacol)
				rm->rm_missingdata++;
			else
				rm->rm_missingparity++;
			rc->rc_error = SET_ERROR(ENXIO);
			rc->rc_tried = 1;	/* don't even try */
			rc->rc_skipped = 1;
			continue;
		}
		if (vdev_dtl_contains(cvd, DTL_MISSING, zio->io_txg, 1)) {
			if (c >= rm->rm_firstdatacol)
				rm->rm_missingdata++;
			else
				rm->rm_missingparity++;
			rc->rc_error = SET_ERROR(ESTALE);
			rc->rc_skipped = 1;
			continue;
		}
		if (c >= rm->rm_firstdatacol || rm->rm_missingdata > 0 ||
		    (zio->io_flags & (ZIO_FLAG_SCRUB | ZIO_FLAG_RESILVER))) {
			zio_nowait(zio_vdev_child_io(zio, NULL, cvd,
			    rc->rc_offset, rc->rc_abd, rc->rc_size,
			    zio->io_type, zio->io_priority, 0,
			    vdev_raidz_child_done, rc));
		}
	}

	zio_execute(zio);
}


/*
 * Report a checksum error for a child of a RAID-Z device.
 */
static void
raidz_checksum_error(zio_t *zio, raidz_col_t *rc, abd_t *bad_data)
{
	vdev_t *vd = zio->io_vd->vdev_child[rc->rc_devidx];

	if (!(zio->io_flags & ZIO_FLAG_SPECULATIVE)) {
		zio_bad_cksum_t zbc;
		raidz_map_t *rm = zio->io_vsd;

		mutex_enter(&vd->vdev_stat_lock);
		vd->vdev_stat.vs_checksum_errors++;
		mutex_exit(&vd->vdev_stat_lock);

		zbc.zbc_has_cksum = 0;
		zbc.zbc_injected = rm->rm_ecksuminjected;

		zfs_ereport_post_checksum(zio->io_spa, vd, zio,
		    rc->rc_offset, rc->rc_size, rc->rc_abd, bad_data,
		    &zbc);
	}
}

/*
 * We keep track of whether or not there were any injected errors, so that
 * any ereports we generate can note it.
 */
static int
raidz_checksum_verify(zio_t *zio)
{
	zio_bad_cksum_t zbc;
	raidz_map_t *rm = zio->io_vsd;
	int ret;

	bzero(&zbc, sizeof (zio_bad_cksum_t));

	ret = zio_checksum_error(zio, &zbc);
	if (ret != 0 && zbc.zbc_injected != 0)
		rm->rm_ecksuminjected = 1;

	return (ret);
}

/*
 * Generate the parity from the data columns. If we tried and were able to
 * read the parity without error, verify that the generated parity matches the
 * data we read. If it doesn't, we fire off a checksum error. Return the
 * number such failures.
 */
static int
raidz_parity_verify(zio_t *zio, raidz_map_t *rm)
{
	abd_t *orig[VDEV_RAIDZ_MAXPARITY];
	int c, ret = 0;
	raidz_col_t *rc;

	for (c = 0; c < rm->rm_firstdatacol; c++) {
		rc = &rm->rm_col[c];
		if (!rc->rc_tried || rc->rc_error != 0)
			continue;

		orig[c] = abd_alloc_sametype(rc->rc_abd, rc->rc_size);
		abd_copy(orig[c], rc->rc_abd, rc->rc_size);
	}

	vdev_raidz_generate_parity(rm);

	for (c = 0; c < rm->rm_firstdatacol; c++) {
		rc = &rm->rm_col[c];
		if (!rc->rc_tried || rc->rc_error != 0)
			continue;
		if (abd_cmp(orig[c], rc->rc_abd) != 0) {
			raidz_checksum_error(zio, rc, orig[c]);
			rc->rc_error = SET_ERROR(ECKSUM);
			ret++;
		}
		abd_free(orig[c]);
	}

	return (ret);
}

static int
vdev_raidz_worst_error(raidz_map_t *rm)
{
	int c, error = 0;

	for (c = 0; c < rm->rm_cols; c++)
		error = zio_worst_error(error, rm->rm_col[c].rc_error);

	return (error);
}

/*
 * Iterate over all combinations of bad data and attempt a reconstruction.
 * Note that the algorithm below is non-optimal because it doesn't take into
 * account how reconstruction is actually performed. For example, with
 * triple-parity RAID-Z the reconstruction procedure is the same if column 4
 * is targeted as invalid as if columns 1 and 4 are targeted since in both
 * cases we'd only use parity information in column 0.
 */
static int
vdev_raidz_combrec(zio_t *zio, int total_errors, int data_errors)
{
	raidz_map_t *rm = zio->io_vsd;
	raidz_col_t *rc;
	abd_t *orig[VDEV_RAIDZ_MAXPARITY] = { NULL };
	int tstore[VDEV_RAIDZ_MAXPARITY + 2];
	int *tgts = &tstore[1];
	int curr, next, i, c, n;
	int code, ret = 0;

	ASSERT(total_errors < rm->rm_firstdatacol);

	/*
	 * This simplifies one edge condition.
	 */
	tgts[-1] = -1;

	for (n = 1; n <= rm->rm_firstdatacol - total_errors; n++) {
		/*
		 * Initialize the targets array by finding the first n columns
		 * that contain no error.
		 *
		 * If there were no data errors, we need to ensure that we're
		 * always explicitly attempting to reconstruct at least one
		 * data column. To do this, we simply push the highest target
		 * up into the data columns.
		 */
		for (c = 0, i = 0; i < n; i++) {
			if (i == n - 1 && data_errors == 0 &&
			    c < rm->rm_firstdatacol) {
				c = rm->rm_firstdatacol;
			}

			while (rm->rm_col[c].rc_error != 0) {
				c++;
				ASSERT3S(c, <, rm->rm_cols);
			}

			tgts[i] = c++;
		}

		/*
		 * Setting tgts[n] simplifies the other edge condition.
		 */
		tgts[n] = rm->rm_cols;

		/*
		 * These buffers were allocated in previous iterations.
		 */
		for (i = 0; i < n - 1; i++) {
			ASSERT(orig[i] != NULL);
		}

		orig[n - 1] = abd_alloc_for_io(rm->rm_col[0].rc_size, B_FALSE);

		curr = 0;
		next = tgts[curr];

		while (curr != n) {
			tgts[curr] = next;
			curr = 0;

			/*
			 * Save off the original data that we're going to
			 * attempt to reconstruct.
			 */
			for (i = 0; i < n; i++) {
				ASSERT(orig[i] != NULL);
				c = tgts[i];
				ASSERT3S(c, >=, 0);
				ASSERT3S(c, <, rm->rm_cols);
				rc = &rm->rm_col[c];
				abd_copy(orig[i], rc->rc_abd, rc->rc_size);
			}

			/*
			 * Attempt a reconstruction and exit the outer loop on
			 * success.
			 */
			code = vdev_raidz_reconstruct(rm, tgts, n);
			if (raidz_checksum_verify(zio) == 0) {

				for (i = 0; i < n; i++) {
					c = tgts[i];
					rc = &rm->rm_col[c];
					ASSERT(rc->rc_error == 0);
					if (rc->rc_tried)
						raidz_checksum_error(zio, rc,
						    orig[i]);
					rc->rc_error = SET_ERROR(ECKSUM);
				}

				ret = code;
				goto done;
			}

			/*
			 * Restore the original data.
			 */
			for (i = 0; i < n; i++) {
				c = tgts[i];
				rc = &rm->rm_col[c];
				abd_copy(rc->rc_abd, orig[i], rc->rc_size);
			}

			do {
				/*
				 * Find the next valid column after the curr
				 * position..
				 */
				for (next = tgts[curr] + 1;
				    next < rm->rm_cols &&
				    rm->rm_col[next].rc_error != 0; next++)
					continue;

				ASSERT(next <= tgts[curr + 1]);

				/*
				 * If that spot is available, we're done here.
				 */
				if (next != tgts[curr + 1])
					break;

				/*
				 * Otherwise, find the next valid column after
				 * the previous position.
				 */
				for (c = tgts[curr - 1] + 1;
				    rm->rm_col[c].rc_error != 0; c++)
					continue;

				tgts[curr] = c;
				curr++;

			} while (curr != n);
		}
	}
	n--;
done:
	for (i = 0; i < n; i++)
		abd_free(orig[i]);

	return (ret);
}

/*
 * Complete an IO operation on a RAIDZ VDev
 *
 * Outline:
 * - For write operations:
 *   1. Check for errors on the child IOs.
 *   2. Return, setting an error code if too few child VDevs were written
 *      to reconstruct the data later.  Note that partial writes are
 *      considered successful if they can be reconstructed at all.
 * - For read operations:
 *   1. Check for errors on the child IOs.
 *   2. If data errors occurred:
 *      a. Try to reassemble the data from the parity available.
 *      b. If we haven't yet read the parity drives, read them now.
 *      c. If all parity drives have been read but the data still doesn't
 *         reassemble with a correct checksum, then try combinatorial
 *         reconstruction.
 *      d. If that doesn't work, return an error.
 *   3. If there were unexpected errors or this is a resilver operation,
 *      rewrite the vdevs that had errors.
 */
static void
vdev_raidz_io_done(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;
	vdev_t *cvd;
	raidz_map_t *rm = zio->io_vsd;
	raidz_col_t *rc = NULL;
	int unexpected_errors = 0;
	int parity_errors = 0;
	int parity_untried = 0;
	int data_errors = 0;
	int total_errors = 0;
	int n, c;
	int tgts[VDEV_RAIDZ_MAXPARITY];
	int code;

	ASSERT(zio->io_bp != NULL);  /* XXX need to add code to enforce this */

	ASSERT(rm->rm_missingparity <= rm->rm_firstdatacol);
	ASSERT(rm->rm_missingdata <= rm->rm_cols - rm->rm_firstdatacol);

	for (c = 0; c < rm->rm_cols; c++) {
		rc = &rm->rm_col[c];

		if (rc->rc_error) {
			ASSERT(rc->rc_error != ECKSUM);	/* child has no bp */

			if (c < rm->rm_firstdatacol)
				parity_errors++;
			else
				data_errors++;

			if (!rc->rc_skipped)
				unexpected_errors++;

			total_errors++;
		} else if (c < rm->rm_firstdatacol && !rc->rc_tried) {
			parity_untried++;
		}
	}

	if (zio->io_type == ZIO_TYPE_WRITE) {
		/*
		 * XXX -- for now, treat partial writes as a success.
		 * (If we couldn't write enough columns to reconstruct
		 * the data, the I/O failed.  Otherwise, good enough.)
		 *
		 * Now that we support write reallocation, it would be better
		 * to treat partial failure as real failure unless there are
		 * no non-degraded top-level vdevs left, and not update DTLs
		 * if we intend to reallocate.
		 */
		/* XXPOLICY */
		if (total_errors > rm->rm_firstdatacol)
			zio->io_error = vdev_raidz_worst_error(rm);

		return;
	}

	ASSERT(zio->io_type == ZIO_TYPE_READ);
	/*
	 * There are three potential phases for a read:
	 *	1. produce valid data from the columns read
	 *	2. read all disks and try again
	 *	3. perform combinatorial reconstruction
	 *
	 * Each phase is progressively both more expensive and less likely to
	 * occur. If we encounter more errors than we can repair or all phases
	 * fail, we have no choice but to return an error.
	 */

	/*
	 * If the number of errors we saw was correctable -- less than or equal
	 * to the number of parity disks read -- attempt to produce data that
	 * has a valid checksum. Naturally, this case applies in the absence of
	 * any errors.
	 */
	if (total_errors <= rm->rm_firstdatacol - parity_untried) {
		if (data_errors == 0) {
			if (raidz_checksum_verify(zio) == 0) {
				/*
				 * If we read parity information (unnecessarily
				 * as it happens since no reconstruction was
				 * needed) regenerate and verify the parity.
				 * We also regenerate parity when resilvering
				 * so we can write it out to the failed device
				 * later.
				 */
				if (parity_errors + parity_untried <
				    rm->rm_firstdatacol ||
				    (zio->io_flags & ZIO_FLAG_RESILVER)) {
					n = raidz_parity_verify(zio, rm);
					unexpected_errors += n;
					ASSERT(parity_errors + n <=
					    rm->rm_firstdatacol);
				}
				goto done;
			}
		} else {
			/*
			 * We either attempt to read all the parity columns or
			 * none of them. If we didn't try to read parity, we
			 * wouldn't be here in the correctable case. There must
			 * also have been fewer parity errors than parity
			 * columns or, again, we wouldn't be in this code path.
			 */
			ASSERT(parity_untried == 0);
			ASSERT(parity_errors < rm->rm_firstdatacol);

			/*
			 * Identify the data columns that reported an error.
			 */
			n = 0;
			for (c = rm->rm_firstdatacol; c < rm->rm_cols; c++) {
				rc = &rm->rm_col[c];
				if (rc->rc_error != 0) {
					ASSERT(n < VDEV_RAIDZ_MAXPARITY);
					tgts[n++] = c;
				}
			}

			ASSERT(rm->rm_firstdatacol >= n);

			code = vdev_raidz_reconstruct(rm, tgts, n);

			if (raidz_checksum_verify(zio) == 0) {
				/*
				 * If we read more parity disks than were used
				 * for reconstruction, confirm that the other
				 * parity disks produced correct data. This
				 * routine is suboptimal in that it regenerates
				 * the parity that we already used in addition
				 * to the parity that we're attempting to
				 * verify, but this should be a relatively
				 * uncommon case, and can be optimized if it
				 * becomes a problem. Note that we regenerate
				 * parity when resilvering so we can write it
				 * out to failed devices later.
				 */
				if (parity_errors < rm->rm_firstdatacol - n ||
				    (zio->io_flags & ZIO_FLAG_RESILVER)) {
					n = raidz_parity_verify(zio, rm);
					unexpected_errors += n;
					ASSERT(parity_errors + n <=
					    rm->rm_firstdatacol);
				}

				goto done;
			}
		}
	}

	/*
	 * This isn't a typical situation -- either we got a read error or
	 * a child silently returned bad data. Read every block so we can
	 * try again with as much data and parity as we can track down. If
	 * we've already been through once before, all children will be marked
	 * as tried so we'll proceed to combinatorial reconstruction.
	 */
	unexpected_errors = 1;
	rm->rm_missingdata = 0;
	rm->rm_missingparity = 0;

	for (c = 0; c < rm->rm_cols; c++) {
		if (rm->rm_col[c].rc_tried)
			continue;

		zio_vdev_io_redone(zio);
		do {
			rc = &rm->rm_col[c];
			if (rc->rc_tried)
				continue;
			zio_nowait(zio_vdev_child_io(zio, NULL,
			    vd->vdev_child[rc->rc_devidx],
			    rc->rc_offset, rc->rc_abd, rc->rc_size,
			    zio->io_type, zio->io_priority, 0,
			    vdev_raidz_child_done, rc));
		} while (++c < rm->rm_cols);

		return;
	}

	/*
	 * At this point we've attempted to reconstruct the data given the
	 * errors we detected, and we've attempted to read all columns. There
	 * must, therefore, be one or more additional problems -- silent errors
	 * resulting in invalid data rather than explicit I/O errors resulting
	 * in absent data. We check if there is enough additional data to
	 * possibly reconstruct the data and then perform combinatorial
	 * reconstruction over all possible combinations. If that fails,
	 * we're cooked.
	 */
	if (total_errors > rm->rm_firstdatacol) {
		zio->io_error = vdev_raidz_worst_error(rm);

	} else if (total_errors < rm->rm_firstdatacol &&
	    (code = vdev_raidz_combrec(zio, total_errors, data_errors)) != 0) {
		/*
		 * If we didn't use all the available parity for the
		 * combinatorial reconstruction, verify that the remaining
		 * parity is correct.
		 */
		if (code != (1 << rm->rm_firstdatacol) - 1)
			(void) raidz_parity_verify(zio, rm);
	} else {
		/*
		 * We're here because either:
		 *
		 *	total_errors == rm_first_datacol, or
		 *	vdev_raidz_combrec() failed
		 *
		 * In either case, there is enough bad data to prevent
		 * reconstruction.
		 *
		 * Start checksum ereports for all children which haven't
		 * failed, and the IO wasn't speculative.
		 */
		zio->io_error = SET_ERROR(ECKSUM);

		if (!(zio->io_flags & ZIO_FLAG_SPECULATIVE)) {
			for (c = 0; c < rm->rm_cols; c++) {
				rc = &rm->rm_col[c];
				if (rc->rc_error == 0) {
					zio_bad_cksum_t zbc;
					zbc.zbc_has_cksum = 0;
					zbc.zbc_injected =
					    rm->rm_ecksuminjected;

					zfs_ereport_start_checksum(
					    zio->io_spa,
					    vd->vdev_child[rc->rc_devidx],
					    zio, rc->rc_offset, rc->rc_size,
					    (void *)(uintptr_t)c, &zbc);
				}
			}
		}
	}

done:
	zio_checksum_verified(zio);

	if (zio->io_error == 0 && spa_writeable(zio->io_spa) &&
	    (unexpected_errors || (zio->io_flags & ZIO_FLAG_RESILVER))) {
		/*
		 * Use the good data we have in hand to repair damaged children.
		 */
		for (c = 0; c < rm->rm_cols; c++) {
			rc = &rm->rm_col[c];
			cvd = vd->vdev_child[rc->rc_devidx];

			if (rc->rc_error == 0)
				continue;

			zio_nowait(zio_vdev_child_io(zio, NULL, cvd,
			    rc->rc_offset, rc->rc_abd, rc->rc_size,
			    ZIO_TYPE_WRITE, ZIO_PRIORITY_ASYNC_WRITE,
			    ZIO_FLAG_IO_REPAIR | (unexpected_errors ?
			    ZIO_FLAG_SELF_HEAL : 0), NULL, NULL));
		}
	}
}

static void
vdev_raidz_state_change(vdev_t *vd, int faulted, int degraded)
{
	if (faulted > vd->vdev_nparity)
		vdev_set_state(vd, B_FALSE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_NO_REPLICAS);
	else if (degraded + faulted != 0)
		vdev_set_state(vd, B_FALSE, VDEV_STATE_DEGRADED, VDEV_AUX_NONE);
	else
		vdev_set_state(vd, B_FALSE, VDEV_STATE_HEALTHY, VDEV_AUX_NONE);
}

vdev_ops_t vdev_raidz_ops = {
	vdev_raidz_open,
	vdev_raidz_close,
	vdev_raidz_asize,
	vdev_raidz_io_start,
	vdev_raidz_io_done,
	vdev_raidz_state_change,
	NULL,
	NULL,
	VDEV_TYPE_RAIDZ,	/* name of this vdev type */
	B_FALSE			/* not a leaf vdev */
};
