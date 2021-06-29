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
 * Copyright (c) 2012, 2020 by Delphix. All rights reserved.
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
#include <sys/vdev_draid.h>

#ifdef ZFS_DEBUG
#include <sys/vdev.h>	/* For vdev_xlate() in vdev_raidz_io_verify() */
#endif

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
 * We chose 1, 2, and 4 as our generators because 1 corresponds to the trivial
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

#define	VDEV_RAIDZ_MUL_2(x)	(((x) << 1) ^ (((x) & 0x80) ? 0x1d : 0))
#define	VDEV_RAIDZ_MUL_4(x)	(VDEV_RAIDZ_MUL_2(VDEV_RAIDZ_MUL_2(x)))

/*
 * We provide a mechanism to perform the field multiplication operation on a
 * 64-bit value all at once rather than a byte at a time. This works by
 * creating a mask from the top bit in each byte and using that to
 * conditionally apply the XOR of 0x1d.
 */
#define	VDEV_RAIDZ_64MUL_2(x, mask) \
{ \
	(mask) = (x) & 0x8080808080808080ULL; \
	(mask) = ((mask) << 1) - ((mask) >> 7); \
	(x) = (((x) << 1) & 0xfefefefefefefefeULL) ^ \
	    ((mask) & 0x1d1d1d1d1d1d1d1dULL); \
}

#define	VDEV_RAIDZ_64MUL_4(x, mask) \
{ \
	VDEV_RAIDZ_64MUL_2((x), mask); \
	VDEV_RAIDZ_64MUL_2((x), mask); \
}

static void
vdev_raidz_row_free(raidz_row_t *rr)
{
	for (int c = 0; c < rr->rr_cols; c++) {
		raidz_col_t *rc = &rr->rr_col[c];

		if (rc->rc_size != 0)
			abd_free(rc->rc_abd);
		if (rc->rc_orig_data != NULL)
			abd_free(rc->rc_orig_data);
	}

	if (rr->rr_abd_empty != NULL)
		abd_free(rr->rr_abd_empty);

	kmem_free(rr, offsetof(raidz_row_t, rr_col[rr->rr_scols]));
}

void
vdev_raidz_map_free(raidz_map_t *rm)
{
	for (int i = 0; i < rm->rm_nrows; i++)
		vdev_raidz_row_free(rm->rm_row[i]);

	kmem_free(rm, offsetof(raidz_map_t, rm_row[rm->rm_nrows]));
}

static void
vdev_raidz_map_free_vsd(zio_t *zio)
{
	raidz_map_t *rm = zio->io_vsd;

	vdev_raidz_map_free(rm);
}

const zio_vsd_ops_t vdev_raidz_vsd_ops = {
	.vsd_free = vdev_raidz_map_free_vsd,
};

static void
vdev_raidz_map_alloc_write(zio_t *zio, raidz_map_t *rm, uint64_t ashift)
{
	int c;
	int nwrapped = 0;
	uint64_t off = 0;
	raidz_row_t *rr = rm->rm_row[0];

	ASSERT3U(zio->io_type, ==, ZIO_TYPE_WRITE);
	ASSERT3U(rm->rm_nrows, ==, 1);

	/*
	 * Pad any parity columns with additional space to account for skip
	 * sectors.
	 */
	if (rm->rm_skipstart < rr->rr_firstdatacol) {
		ASSERT0(rm->rm_skipstart);
		nwrapped = rm->rm_nskip;
	} else if (rr->rr_scols < (rm->rm_skipstart + rm->rm_nskip)) {
		nwrapped =
		    (rm->rm_skipstart + rm->rm_nskip) % rr->rr_scols;
	}

	/*
	 * Optional single skip sectors (rc_size == 0) will be handled in
	 * vdev_raidz_io_start_write().
	 */
	int skipped = rr->rr_scols - rr->rr_cols;

	/* Allocate buffers for the parity columns */
	for (c = 0; c < rr->rr_firstdatacol; c++) {
		raidz_col_t *rc = &rr->rr_col[c];

		/*
		 * Parity columns will pad out a linear ABD to account for
		 * the skip sector. A linear ABD is used here because
		 * parity calculations use the ABD buffer directly to calculate
		 * parity. This avoids doing a memcpy back to the ABD after the
		 * parity has been calculated. By issuing the parity column
		 * with the skip sector we can reduce contention on the child
		 * VDEV queue locks (vq_lock).
		 */
		if (c < nwrapped) {
			rc->rc_abd = abd_alloc_linear(
			    rc->rc_size + (1ULL << ashift), B_FALSE);
			abd_zero_off(rc->rc_abd, rc->rc_size, 1ULL << ashift);
			skipped++;
		} else {
			rc->rc_abd = abd_alloc_linear(rc->rc_size, B_FALSE);
		}
	}

	for (off = 0; c < rr->rr_cols; c++) {
		raidz_col_t *rc = &rr->rr_col[c];
		abd_t *abd = abd_get_offset_struct(&rc->rc_abdstruct,
		    zio->io_abd, off, rc->rc_size);

		/*
		 * Generate I/O for skip sectors to improve aggregation
		 * continuity. We will use gang ABD's to reduce contention
		 * on the child VDEV queue locks (vq_lock) by issuing
		 * a single I/O that contains the data and skip sector.
		 *
		 * It is important to make sure that rc_size is not updated
		 * even though we are adding a skip sector to the ABD. When
		 * calculating the parity in vdev_raidz_generate_parity_row()
		 * the rc_size is used to iterate through the ABD's. We can
		 * not have zero'd out skip sectors used for calculating
		 * parity for raidz, because those same sectors are not used
		 * during reconstruction.
		 */
		if (c >= rm->rm_skipstart && skipped < rm->rm_nskip) {
			rc->rc_abd = abd_alloc_gang();
			abd_gang_add(rc->rc_abd, abd, B_TRUE);
			abd_gang_add(rc->rc_abd,
			    abd_get_zeros(1ULL << ashift), B_TRUE);
			skipped++;
		} else {
			rc->rc_abd = abd;
		}
		off += rc->rc_size;
	}

	ASSERT3U(off, ==, zio->io_size);
	ASSERT3S(skipped, ==, rm->rm_nskip);
}

static void
vdev_raidz_map_alloc_read(zio_t *zio, raidz_map_t *rm)
{
	int c;
	raidz_row_t *rr = rm->rm_row[0];

	ASSERT3U(rm->rm_nrows, ==, 1);

	/* Allocate buffers for the parity columns */
	for (c = 0; c < rr->rr_firstdatacol; c++)
		rr->rr_col[c].rc_abd =
		    abd_alloc_linear(rr->rr_col[c].rc_size, B_FALSE);

	for (uint64_t off = 0; c < rr->rr_cols; c++) {
		raidz_col_t *rc = &rr->rr_col[c];
		rc->rc_abd = abd_get_offset_struct(&rc->rc_abdstruct,
		    zio->io_abd, off, rc->rc_size);
		off += rc->rc_size;
	}
}

/*
 * Divides the IO evenly across all child vdevs; usually, dcols is
 * the number of children in the target vdev.
 *
 * Avoid inlining the function to keep vdev_raidz_io_start(), which
 * is this functions only caller, as small as possible on the stack.
 */
noinline raidz_map_t *
vdev_raidz_map_alloc(zio_t *zio, uint64_t ashift, uint64_t dcols,
    uint64_t nparity)
{
	raidz_row_t *rr;
	/* The starting RAIDZ (parent) vdev sector of the block. */
	uint64_t b = zio->io_offset >> ashift;
	/* The zio's size in units of the vdev's minimum sector size. */
	uint64_t s = zio->io_size >> ashift;
	/* The first column for this stripe. */
	uint64_t f = b % dcols;
	/* The starting byte offset on each child vdev. */
	uint64_t o = (b / dcols) << ashift;
	uint64_t q, r, c, bc, col, acols, scols, coff, devidx, asize, tot;

	raidz_map_t *rm =
	    kmem_zalloc(offsetof(raidz_map_t, rm_row[1]), KM_SLEEP);
	rm->rm_nrows = 1;

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

	/*
	 * acols: The columns that will be accessed.
	 * scols: The columns that will be accessed or skipped.
	 */
	if (q == 0) {
		/* Our I/O request doesn't span all child vdevs. */
		acols = bc;
		scols = MIN(dcols, roundup(bc, nparity + 1));
	} else {
		acols = dcols;
		scols = dcols;
	}

	ASSERT3U(acols, <=, scols);

	rr = kmem_alloc(offsetof(raidz_row_t, rr_col[scols]), KM_SLEEP);
	rm->rm_row[0] = rr;

	rr->rr_cols = acols;
	rr->rr_scols = scols;
	rr->rr_bigcols = bc;
	rr->rr_missingdata = 0;
	rr->rr_missingparity = 0;
	rr->rr_firstdatacol = nparity;
	rr->rr_abd_empty = NULL;
	rr->rr_nempty = 0;
#ifdef ZFS_DEBUG
	rr->rr_offset = zio->io_offset;
	rr->rr_size = zio->io_size;
#endif

	asize = 0;

	for (c = 0; c < scols; c++) {
		raidz_col_t *rc = &rr->rr_col[c];
		col = f + c;
		coff = o;
		if (col >= dcols) {
			col -= dcols;
			coff += 1ULL << ashift;
		}
		rc->rc_devidx = col;
		rc->rc_offset = coff;
		rc->rc_abd = NULL;
		rc->rc_orig_data = NULL;
		rc->rc_error = 0;
		rc->rc_tried = 0;
		rc->rc_skipped = 0;
		rc->rc_force_repair = 0;
		rc->rc_allow_repair = 1;
		rc->rc_need_orig_restore = B_FALSE;

		if (c >= acols)
			rc->rc_size = 0;
		else if (c < bc)
			rc->rc_size = (q + 1) << ashift;
		else
			rc->rc_size = q << ashift;

		asize += rc->rc_size;
	}

	ASSERT3U(asize, ==, tot << ashift);
	rm->rm_nskip = roundup(tot, nparity + 1) - tot;
	rm->rm_skipstart = bc;

	/*
	 * If all data stored spans all columns, there's a danger that parity
	 * will always be on the same device and, since parity isn't read
	 * during normal operation, that device's I/O bandwidth won't be
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
	ASSERT(rr->rr_cols >= 2);
	ASSERT(rr->rr_col[0].rc_size == rr->rr_col[1].rc_size);

	if (rr->rr_firstdatacol == 1 && (zio->io_offset & (1ULL << 20))) {
		devidx = rr->rr_col[0].rc_devidx;
		o = rr->rr_col[0].rc_offset;
		rr->rr_col[0].rc_devidx = rr->rr_col[1].rc_devidx;
		rr->rr_col[0].rc_offset = rr->rr_col[1].rc_offset;
		rr->rr_col[1].rc_devidx = devidx;
		rr->rr_col[1].rc_offset = o;

		if (rm->rm_skipstart == 0)
			rm->rm_skipstart = 1;
	}

	if (zio->io_type == ZIO_TYPE_WRITE) {
		vdev_raidz_map_alloc_write(zio, rm, ashift);
	} else {
		vdev_raidz_map_alloc_read(zio, rm);
	}

	/* init RAIDZ parity ops */
	rm->rm_ops = vdev_raidz_math_get_ops();

	return (rm);
}

struct pqr_struct {
	uint64_t *p;
	uint64_t *q;
	uint64_t *r;
};

static int
vdev_raidz_p_func(void *buf, size_t size, void *private)
{
	struct pqr_struct *pqr = private;
	const uint64_t *src = buf;
	int i, cnt = size / sizeof (src[0]);

	ASSERT(pqr->p && !pqr->q && !pqr->r);

	for (i = 0; i < cnt; i++, src++, pqr->p++)
		*pqr->p ^= *src;

	return (0);
}

static int
vdev_raidz_pq_func(void *buf, size_t size, void *private)
{
	struct pqr_struct *pqr = private;
	const uint64_t *src = buf;
	uint64_t mask;
	int i, cnt = size / sizeof (src[0]);

	ASSERT(pqr->p && pqr->q && !pqr->r);

	for (i = 0; i < cnt; i++, src++, pqr->p++, pqr->q++) {
		*pqr->p ^= *src;
		VDEV_RAIDZ_64MUL_2(*pqr->q, mask);
		*pqr->q ^= *src;
	}

	return (0);
}

static int
vdev_raidz_pqr_func(void *buf, size_t size, void *private)
{
	struct pqr_struct *pqr = private;
	const uint64_t *src = buf;
	uint64_t mask;
	int i, cnt = size / sizeof (src[0]);

	ASSERT(pqr->p && pqr->q && pqr->r);

	for (i = 0; i < cnt; i++, src++, pqr->p++, pqr->q++, pqr->r++) {
		*pqr->p ^= *src;
		VDEV_RAIDZ_64MUL_2(*pqr->q, mask);
		*pqr->q ^= *src;
		VDEV_RAIDZ_64MUL_4(*pqr->r, mask);
		*pqr->r ^= *src;
	}

	return (0);
}

static void
vdev_raidz_generate_parity_p(raidz_row_t *rr)
{
	uint64_t *p = abd_to_buf(rr->rr_col[VDEV_RAIDZ_P].rc_abd);

	for (int c = rr->rr_firstdatacol; c < rr->rr_cols; c++) {
		abd_t *src = rr->rr_col[c].rc_abd;

		if (c == rr->rr_firstdatacol) {
			abd_copy_to_buf(p, src, rr->rr_col[c].rc_size);
		} else {
			struct pqr_struct pqr = { p, NULL, NULL };
			(void) abd_iterate_func(src, 0, rr->rr_col[c].rc_size,
			    vdev_raidz_p_func, &pqr);
		}
	}
}

static void
vdev_raidz_generate_parity_pq(raidz_row_t *rr)
{
	uint64_t *p = abd_to_buf(rr->rr_col[VDEV_RAIDZ_P].rc_abd);
	uint64_t *q = abd_to_buf(rr->rr_col[VDEV_RAIDZ_Q].rc_abd);
	uint64_t pcnt = rr->rr_col[VDEV_RAIDZ_P].rc_size / sizeof (p[0]);
	ASSERT(rr->rr_col[VDEV_RAIDZ_P].rc_size ==
	    rr->rr_col[VDEV_RAIDZ_Q].rc_size);

	for (int c = rr->rr_firstdatacol; c < rr->rr_cols; c++) {
		abd_t *src = rr->rr_col[c].rc_abd;

		uint64_t ccnt = rr->rr_col[c].rc_size / sizeof (p[0]);

		if (c == rr->rr_firstdatacol) {
			ASSERT(ccnt == pcnt || ccnt == 0);
			abd_copy_to_buf(p, src, rr->rr_col[c].rc_size);
			(void) memcpy(q, p, rr->rr_col[c].rc_size);

			for (uint64_t i = ccnt; i < pcnt; i++) {
				p[i] = 0;
				q[i] = 0;
			}
		} else {
			struct pqr_struct pqr = { p, q, NULL };

			ASSERT(ccnt <= pcnt);
			(void) abd_iterate_func(src, 0, rr->rr_col[c].rc_size,
			    vdev_raidz_pq_func, &pqr);

			/*
			 * Treat short columns as though they are full of 0s.
			 * Note that there's therefore nothing needed for P.
			 */
			uint64_t mask;
			for (uint64_t i = ccnt; i < pcnt; i++) {
				VDEV_RAIDZ_64MUL_2(q[i], mask);
			}
		}
	}
}

static void
vdev_raidz_generate_parity_pqr(raidz_row_t *rr)
{
	uint64_t *p = abd_to_buf(rr->rr_col[VDEV_RAIDZ_P].rc_abd);
	uint64_t *q = abd_to_buf(rr->rr_col[VDEV_RAIDZ_Q].rc_abd);
	uint64_t *r = abd_to_buf(rr->rr_col[VDEV_RAIDZ_R].rc_abd);
	uint64_t pcnt = rr->rr_col[VDEV_RAIDZ_P].rc_size / sizeof (p[0]);
	ASSERT(rr->rr_col[VDEV_RAIDZ_P].rc_size ==
	    rr->rr_col[VDEV_RAIDZ_Q].rc_size);
	ASSERT(rr->rr_col[VDEV_RAIDZ_P].rc_size ==
	    rr->rr_col[VDEV_RAIDZ_R].rc_size);

	for (int c = rr->rr_firstdatacol; c < rr->rr_cols; c++) {
		abd_t *src = rr->rr_col[c].rc_abd;

		uint64_t ccnt = rr->rr_col[c].rc_size / sizeof (p[0]);

		if (c == rr->rr_firstdatacol) {
			ASSERT(ccnt == pcnt || ccnt == 0);
			abd_copy_to_buf(p, src, rr->rr_col[c].rc_size);
			(void) memcpy(q, p, rr->rr_col[c].rc_size);
			(void) memcpy(r, p, rr->rr_col[c].rc_size);

			for (uint64_t i = ccnt; i < pcnt; i++) {
				p[i] = 0;
				q[i] = 0;
				r[i] = 0;
			}
		} else {
			struct pqr_struct pqr = { p, q, r };

			ASSERT(ccnt <= pcnt);
			(void) abd_iterate_func(src, 0, rr->rr_col[c].rc_size,
			    vdev_raidz_pqr_func, &pqr);

			/*
			 * Treat short columns as though they are full of 0s.
			 * Note that there's therefore nothing needed for P.
			 */
			uint64_t mask;
			for (uint64_t i = ccnt; i < pcnt; i++) {
				VDEV_RAIDZ_64MUL_2(q[i], mask);
				VDEV_RAIDZ_64MUL_4(r[i], mask);
			}
		}
	}
}

/*
 * Generate RAID parity in the first virtual columns according to the number of
 * parity columns available.
 */
void
vdev_raidz_generate_parity_row(raidz_map_t *rm, raidz_row_t *rr)
{
	ASSERT3U(rr->rr_cols, !=, 0);

	/* Generate using the new math implementation */
	if (vdev_raidz_math_generate(rm, rr) != RAIDZ_ORIGINAL_IMPL)
		return;

	switch (rr->rr_firstdatacol) {
	case 1:
		vdev_raidz_generate_parity_p(rr);
		break;
	case 2:
		vdev_raidz_generate_parity_pq(rr);
		break;
	case 3:
		vdev_raidz_generate_parity_pqr(rr);
		break;
	default:
		cmn_err(CE_PANIC, "invalid RAID-Z configuration");
	}
}

void
vdev_raidz_generate_parity(raidz_map_t *rm)
{
	for (int i = 0; i < rm->rm_nrows; i++) {
		raidz_row_t *rr = rm->rm_row[i];
		vdev_raidz_generate_parity_row(rm, rr);
	}
}

/* ARGSUSED */
static int
vdev_raidz_reconst_p_func(void *dbuf, void *sbuf, size_t size, void *private)
{
	uint64_t *dst = dbuf;
	uint64_t *src = sbuf;
	int cnt = size / sizeof (src[0]);

	for (int i = 0; i < cnt; i++) {
		dst[i] ^= src[i];
	}

	return (0);
}

/* ARGSUSED */
static int
vdev_raidz_reconst_q_pre_func(void *dbuf, void *sbuf, size_t size,
    void *private)
{
	uint64_t *dst = dbuf;
	uint64_t *src = sbuf;
	uint64_t mask;
	int cnt = size / sizeof (dst[0]);

	for (int i = 0; i < cnt; i++, dst++, src++) {
		VDEV_RAIDZ_64MUL_2(*dst, mask);
		*dst ^= *src;
	}

	return (0);
}

/* ARGSUSED */
static int
vdev_raidz_reconst_q_pre_tail_func(void *buf, size_t size, void *private)
{
	uint64_t *dst = buf;
	uint64_t mask;
	int cnt = size / sizeof (dst[0]);

	for (int i = 0; i < cnt; i++, dst++) {
		/* same operation as vdev_raidz_reconst_q_pre_func() on dst */
		VDEV_RAIDZ_64MUL_2(*dst, mask);
	}

	return (0);
}

struct reconst_q_struct {
	uint64_t *q;
	int exp;
};

static int
vdev_raidz_reconst_q_post_func(void *buf, size_t size, void *private)
{
	struct reconst_q_struct *rq = private;
	uint64_t *dst = buf;
	int cnt = size / sizeof (dst[0]);

	for (int i = 0; i < cnt; i++, dst++, rq->q++) {
		int j;
		uint8_t *b;

		*dst ^= *rq->q;
		for (j = 0, b = (uint8_t *)dst; j < 8; j++, b++) {
			*b = vdev_raidz_exp2(*b, rq->exp);
		}
	}

	return (0);
}

struct reconst_pq_struct {
	uint8_t *p;
	uint8_t *q;
	uint8_t *pxy;
	uint8_t *qxy;
	int aexp;
	int bexp;
};

static int
vdev_raidz_reconst_pq_func(void *xbuf, void *ybuf, size_t size, void *private)
{
	struct reconst_pq_struct *rpq = private;
	uint8_t *xd = xbuf;
	uint8_t *yd = ybuf;

	for (int i = 0; i < size;
	    i++, rpq->p++, rpq->q++, rpq->pxy++, rpq->qxy++, xd++, yd++) {
		*xd = vdev_raidz_exp2(*rpq->p ^ *rpq->pxy, rpq->aexp) ^
		    vdev_raidz_exp2(*rpq->q ^ *rpq->qxy, rpq->bexp);
		*yd = *rpq->p ^ *rpq->pxy ^ *xd;
	}

	return (0);
}

static int
vdev_raidz_reconst_pq_tail_func(void *xbuf, size_t size, void *private)
{
	struct reconst_pq_struct *rpq = private;
	uint8_t *xd = xbuf;

	for (int i = 0; i < size;
	    i++, rpq->p++, rpq->q++, rpq->pxy++, rpq->qxy++, xd++) {
		/* same operation as vdev_raidz_reconst_pq_func() on xd */
		*xd = vdev_raidz_exp2(*rpq->p ^ *rpq->pxy, rpq->aexp) ^
		    vdev_raidz_exp2(*rpq->q ^ *rpq->qxy, rpq->bexp);
	}

	return (0);
}

static void
vdev_raidz_reconstruct_p(raidz_row_t *rr, int *tgts, int ntgts)
{
	int x = tgts[0];
	abd_t *dst, *src;

	ASSERT3U(ntgts, ==, 1);
	ASSERT3U(x, >=, rr->rr_firstdatacol);
	ASSERT3U(x, <, rr->rr_cols);

	ASSERT3U(rr->rr_col[x].rc_size, <=, rr->rr_col[VDEV_RAIDZ_P].rc_size);

	src = rr->rr_col[VDEV_RAIDZ_P].rc_abd;
	dst = rr->rr_col[x].rc_abd;

	abd_copy_from_buf(dst, abd_to_buf(src), rr->rr_col[x].rc_size);

	for (int c = rr->rr_firstdatacol; c < rr->rr_cols; c++) {
		uint64_t size = MIN(rr->rr_col[x].rc_size,
		    rr->rr_col[c].rc_size);

		src = rr->rr_col[c].rc_abd;

		if (c == x)
			continue;

		(void) abd_iterate_func2(dst, src, 0, 0, size,
		    vdev_raidz_reconst_p_func, NULL);
	}
}

static void
vdev_raidz_reconstruct_q(raidz_row_t *rr, int *tgts, int ntgts)
{
	int x = tgts[0];
	int c, exp;
	abd_t *dst, *src;

	ASSERT(ntgts == 1);

	ASSERT(rr->rr_col[x].rc_size <= rr->rr_col[VDEV_RAIDZ_Q].rc_size);

	for (c = rr->rr_firstdatacol; c < rr->rr_cols; c++) {
		uint64_t size = (c == x) ? 0 : MIN(rr->rr_col[x].rc_size,
		    rr->rr_col[c].rc_size);

		src = rr->rr_col[c].rc_abd;
		dst = rr->rr_col[x].rc_abd;

		if (c == rr->rr_firstdatacol) {
			abd_copy(dst, src, size);
			if (rr->rr_col[x].rc_size > size) {
				abd_zero_off(dst, size,
				    rr->rr_col[x].rc_size - size);
			}
		} else {
			ASSERT3U(size, <=, rr->rr_col[x].rc_size);
			(void) abd_iterate_func2(dst, src, 0, 0, size,
			    vdev_raidz_reconst_q_pre_func, NULL);
			(void) abd_iterate_func(dst,
			    size, rr->rr_col[x].rc_size - size,
			    vdev_raidz_reconst_q_pre_tail_func, NULL);
		}
	}

	src = rr->rr_col[VDEV_RAIDZ_Q].rc_abd;
	dst = rr->rr_col[x].rc_abd;
	exp = 255 - (rr->rr_cols - 1 - x);

	struct reconst_q_struct rq = { abd_to_buf(src), exp };
	(void) abd_iterate_func(dst, 0, rr->rr_col[x].rc_size,
	    vdev_raidz_reconst_q_post_func, &rq);
}

static void
vdev_raidz_reconstruct_pq(raidz_row_t *rr, int *tgts, int ntgts)
{
	uint8_t *p, *q, *pxy, *qxy, tmp, a, b, aexp, bexp;
	abd_t *pdata, *qdata;
	uint64_t xsize, ysize;
	int x = tgts[0];
	int y = tgts[1];
	abd_t *xd, *yd;

	ASSERT(ntgts == 2);
	ASSERT(x < y);
	ASSERT(x >= rr->rr_firstdatacol);
	ASSERT(y < rr->rr_cols);

	ASSERT(rr->rr_col[x].rc_size >= rr->rr_col[y].rc_size);

	/*
	 * Move the parity data aside -- we're going to compute parity as
	 * though columns x and y were full of zeros -- Pxy and Qxy. We want to
	 * reuse the parity generation mechanism without trashing the actual
	 * parity so we make those columns appear to be full of zeros by
	 * setting their lengths to zero.
	 */
	pdata = rr->rr_col[VDEV_RAIDZ_P].rc_abd;
	qdata = rr->rr_col[VDEV_RAIDZ_Q].rc_abd;
	xsize = rr->rr_col[x].rc_size;
	ysize = rr->rr_col[y].rc_size;

	rr->rr_col[VDEV_RAIDZ_P].rc_abd =
	    abd_alloc_linear(rr->rr_col[VDEV_RAIDZ_P].rc_size, B_TRUE);
	rr->rr_col[VDEV_RAIDZ_Q].rc_abd =
	    abd_alloc_linear(rr->rr_col[VDEV_RAIDZ_Q].rc_size, B_TRUE);
	rr->rr_col[x].rc_size = 0;
	rr->rr_col[y].rc_size = 0;

	vdev_raidz_generate_parity_pq(rr);

	rr->rr_col[x].rc_size = xsize;
	rr->rr_col[y].rc_size = ysize;

	p = abd_to_buf(pdata);
	q = abd_to_buf(qdata);
	pxy = abd_to_buf(rr->rr_col[VDEV_RAIDZ_P].rc_abd);
	qxy = abd_to_buf(rr->rr_col[VDEV_RAIDZ_Q].rc_abd);
	xd = rr->rr_col[x].rc_abd;
	yd = rr->rr_col[y].rc_abd;

	/*
	 * We now have:
	 *	Pxy = P + D_x + D_y
	 *	Qxy = Q + 2^(ndevs - 1 - x) * D_x + 2^(ndevs - 1 - y) * D_y
	 *
	 * We can then solve for D_x:
	 *	D_x = A * (P + Pxy) + B * (Q + Qxy)
	 * where
	 *	A = 2^(x - y) * (2^(x - y) + 1)^-1
	 *	B = 2^(ndevs - 1 - x) * (2^(x - y) + 1)^-1
	 *
	 * With D_x in hand, we can easily solve for D_y:
	 *	D_y = P + Pxy + D_x
	 */

	a = vdev_raidz_pow2[255 + x - y];
	b = vdev_raidz_pow2[255 - (rr->rr_cols - 1 - x)];
	tmp = 255 - vdev_raidz_log2[a ^ 1];

	aexp = vdev_raidz_log2[vdev_raidz_exp2(a, tmp)];
	bexp = vdev_raidz_log2[vdev_raidz_exp2(b, tmp)];

	ASSERT3U(xsize, >=, ysize);
	struct reconst_pq_struct rpq = { p, q, pxy, qxy, aexp, bexp };

	(void) abd_iterate_func2(xd, yd, 0, 0, ysize,
	    vdev_raidz_reconst_pq_func, &rpq);
	(void) abd_iterate_func(xd, ysize, xsize - ysize,
	    vdev_raidz_reconst_pq_tail_func, &rpq);

	abd_free(rr->rr_col[VDEV_RAIDZ_P].rc_abd);
	abd_free(rr->rr_col[VDEV_RAIDZ_Q].rc_abd);

	/*
	 * Restore the saved parity data.
	 */
	rr->rr_col[VDEV_RAIDZ_P].rc_abd = pdata;
	rr->rr_col[VDEV_RAIDZ_Q].rc_abd = qdata;
}

/* BEGIN CSTYLED */
/*
 * In the general case of reconstruction, we must solve the system of linear
 * equations defined by the coefficients used to generate parity as well as
 * the contents of the data and parity disks. This can be expressed with
 * vectors for the original data (D) and the actual data (d) and parity (p)
 * and a matrix composed of the identity matrix (I) and a dispersal matrix (V):
 *
 *            __   __                     __     __
 *            |     |         __     __   |  p_0  |
 *            |  V  |         |  D_0  |   | p_m-1 |
 *            |     |    x    |   :   | = |  d_0  |
 *            |  I  |         | D_n-1 |   |   :   |
 *            |     |         ~~     ~~   | d_n-1 |
 *            ~~   ~~                     ~~     ~~
 *
 * I is simply a square identity matrix of size n, and V is a vandermonde
 * matrix defined by the coefficients we chose for the various parity columns
 * (1, 2, 4). Note that these values were chosen both for simplicity, speedy
 * computation as well as linear separability.
 *
 *      __               __               __     __
 *      |   1   ..  1 1 1 |               |  p_0  |
 *      | 2^n-1 ..  4 2 1 |   __     __   |   :   |
 *      | 4^n-1 .. 16 4 1 |   |  D_0  |   | p_m-1 |
 *      |   1   ..  0 0 0 |   |  D_1  |   |  d_0  |
 *      |   0   ..  0 0 0 | x |  D_2  | = |  d_1  |
 *      |   :       : : : |   |   :   |   |  d_2  |
 *      |   0   ..  1 0 0 |   | D_n-1 |   |   :   |
 *      |   0   ..  0 1 0 |   ~~     ~~   |   :   |
 *      |   0   ..  0 0 1 |               | d_n-1 |
 *      ~~               ~~               ~~     ~~
 *
 * Note that I, V, d, and p are known. To compute D, we must invert the
 * matrix and use the known data and parity values to reconstruct the unknown
 * data values. We begin by removing the rows in V|I and d|p that correspond
 * to failed or missing columns; we then make V|I square (n x n) and d|p
 * sized n by removing rows corresponding to unused parity from the bottom up
 * to generate (V|I)' and (d|p)'. We can then generate the inverse of (V|I)'
 * using Gauss-Jordan elimination. In the example below we use m=3 parity
 * columns, n=8 data columns, with errors in d_1, d_2, and p_1:
 *           __                               __
 *           |  1   1   1   1   1   1   1   1  |
 *           | 128  64  32  16  8   4   2   1  | <-----+-+-- missing disks
 *           |  19 205 116  29  64  16  4   1  |      / /
 *           |  1   0   0   0   0   0   0   0  |     / /
 *           |  0   1   0   0   0   0   0   0  | <--' /
 *  (V|I)  = |  0   0   1   0   0   0   0   0  | <---'
 *           |  0   0   0   1   0   0   0   0  |
 *           |  0   0   0   0   1   0   0   0  |
 *           |  0   0   0   0   0   1   0   0  |
 *           |  0   0   0   0   0   0   1   0  |
 *           |  0   0   0   0   0   0   0   1  |
 *           ~~                               ~~
 *           __                               __
 *           |  1   1   1   1   1   1   1   1  |
 *           | 128  64  32  16  8   4   2   1  |
 *           |  19 205 116  29  64  16  4   1  |
 *           |  1   0   0   0   0   0   0   0  |
 *           |  0   1   0   0   0   0   0   0  |
 *  (V|I)' = |  0   0   1   0   0   0   0   0  |
 *           |  0   0   0   1   0   0   0   0  |
 *           |  0   0   0   0   1   0   0   0  |
 *           |  0   0   0   0   0   1   0   0  |
 *           |  0   0   0   0   0   0   1   0  |
 *           |  0   0   0   0   0   0   0   1  |
 *           ~~                               ~~
 *
 * Here we employ Gauss-Jordan elimination to find the inverse of (V|I)'. We
 * have carefully chosen the seed values 1, 2, and 4 to ensure that this
 * matrix is not singular.
 * __                                                                 __
 * |  1   1   1   1   1   1   1   1     1   0   0   0   0   0   0   0  |
 * |  19 205 116  29  64  16  4   1     0   1   0   0   0   0   0   0  |
 * |  1   0   0   0   0   0   0   0     0   0   1   0   0   0   0   0  |
 * |  0   0   0   1   0   0   0   0     0   0   0   1   0   0   0   0  |
 * |  0   0   0   0   1   0   0   0     0   0   0   0   1   0   0   0  |
 * |  0   0   0   0   0   1   0   0     0   0   0   0   0   1   0   0  |
 * |  0   0   0   0   0   0   1   0     0   0   0   0   0   0   1   0  |
 * |  0   0   0   0   0   0   0   1     0   0   0   0   0   0   0   1  |
 * ~~                                                                 ~~
 * __                                                                 __
 * |  1   0   0   0   0   0   0   0     0   0   1   0   0   0   0   0  |
 * |  1   1   1   1   1   1   1   1     1   0   0   0   0   0   0   0  |
 * |  19 205 116  29  64  16  4   1     0   1   0   0   0   0   0   0  |
 * |  0   0   0   1   0   0   0   0     0   0   0   1   0   0   0   0  |
 * |  0   0   0   0   1   0   0   0     0   0   0   0   1   0   0   0  |
 * |  0   0   0   0   0   1   0   0     0   0   0   0   0   1   0   0  |
 * |  0   0   0   0   0   0   1   0     0   0   0   0   0   0   1   0  |
 * |  0   0   0   0   0   0   0   1     0   0   0   0   0   0   0   1  |
 * ~~                                                                 ~~
 * __                                                                 __
 * |  1   0   0   0   0   0   0   0     0   0   1   0   0   0   0   0  |
 * |  0   1   1   0   0   0   0   0     1   0   1   1   1   1   1   1  |
 * |  0  205 116  0   0   0   0   0     0   1   19  29  64  16  4   1  |
 * |  0   0   0   1   0   0   0   0     0   0   0   1   0   0   0   0  |
 * |  0   0   0   0   1   0   0   0     0   0   0   0   1   0   0   0  |
 * |  0   0   0   0   0   1   0   0     0   0   0   0   0   1   0   0  |
 * |  0   0   0   0   0   0   1   0     0   0   0   0   0   0   1   0  |
 * |  0   0   0   0   0   0   0   1     0   0   0   0   0   0   0   1  |
 * ~~                                                                 ~~
 * __                                                                 __
 * |  1   0   0   0   0   0   0   0     0   0   1   0   0   0   0   0  |
 * |  0   1   1   0   0   0   0   0     1   0   1   1   1   1   1   1  |
 * |  0   0  185  0   0   0   0   0    205  1  222 208 141 221 201 204 |
 * |  0   0   0   1   0   0   0   0     0   0   0   1   0   0   0   0  |
 * |  0   0   0   0   1   0   0   0     0   0   0   0   1   0   0   0  |
 * |  0   0   0   0   0   1   0   0     0   0   0   0   0   1   0   0  |
 * |  0   0   0   0   0   0   1   0     0   0   0   0   0   0   1   0  |
 * |  0   0   0   0   0   0   0   1     0   0   0   0   0   0   0   1  |
 * ~~                                                                 ~~
 * __                                                                 __
 * |  1   0   0   0   0   0   0   0     0   0   1   0   0   0   0   0  |
 * |  0   1   1   0   0   0   0   0     1   0   1   1   1   1   1   1  |
 * |  0   0   1   0   0   0   0   0    166 100  4   40 158 168 216 209 |
 * |  0   0   0   1   0   0   0   0     0   0   0   1   0   0   0   0  |
 * |  0   0   0   0   1   0   0   0     0   0   0   0   1   0   0   0  |
 * |  0   0   0   0   0   1   0   0     0   0   0   0   0   1   0   0  |
 * |  0   0   0   0   0   0   1   0     0   0   0   0   0   0   1   0  |
 * |  0   0   0   0   0   0   0   1     0   0   0   0   0   0   0   1  |
 * ~~                                                                 ~~
 * __                                                                 __
 * |  1   0   0   0   0   0   0   0     0   0   1   0   0   0   0   0  |
 * |  0   1   0   0   0   0   0   0    167 100  5   41 159 169 217 208 |
 * |  0   0   1   0   0   0   0   0    166 100  4   40 158 168 216 209 |
 * |  0   0   0   1   0   0   0   0     0   0   0   1   0   0   0   0  |
 * |  0   0   0   0   1   0   0   0     0   0   0   0   1   0   0   0  |
 * |  0   0   0   0   0   1   0   0     0   0   0   0   0   1   0   0  |
 * |  0   0   0   0   0   0   1   0     0   0   0   0   0   0   1   0  |
 * |  0   0   0   0   0   0   0   1     0   0   0   0   0   0   0   1  |
 * ~~                                                                 ~~
 *                   __                               __
 *                   |  0   0   1   0   0   0   0   0  |
 *                   | 167 100  5   41 159 169 217 208 |
 *                   | 166 100  4   40 158 168 216 209 |
 *       (V|I)'^-1 = |  0   0   0   1   0   0   0   0  |
 *                   |  0   0   0   0   1   0   0   0  |
 *                   |  0   0   0   0   0   1   0   0  |
 *                   |  0   0   0   0   0   0   1   0  |
 *                   |  0   0   0   0   0   0   0   1  |
 *                   ~~                               ~~
 *
 * We can then simply compute D = (V|I)'^-1 x (d|p)' to discover the values
 * of the missing data.
 *
 * As is apparent from the example above, the only non-trivial rows in the
 * inverse matrix correspond to the data disks that we're trying to
 * reconstruct. Indeed, those are the only rows we need as the others would
 * only be useful for reconstructing data known or assumed to be valid. For
 * that reason, we only build the coefficients in the rows that correspond to
 * targeted columns.
 */
/* END CSTYLED */

static void
vdev_raidz_matrix_init(raidz_row_t *rr, int n, int nmap, int *map,
    uint8_t **rows)
{
	int i, j;
	int pow;

	ASSERT(n == rr->rr_cols - rr->rr_firstdatacol);

	/*
	 * Fill in the missing rows of interest.
	 */
	for (i = 0; i < nmap; i++) {
		ASSERT3S(0, <=, map[i]);
		ASSERT3S(map[i], <=, 2);

		pow = map[i] * n;
		if (pow > 255)
			pow -= 255;
		ASSERT(pow <= 255);

		for (j = 0; j < n; j++) {
			pow -= map[i];
			if (pow < 0)
				pow += 255;
			rows[i][j] = vdev_raidz_pow2[pow];
		}
	}
}

static void
vdev_raidz_matrix_invert(raidz_row_t *rr, int n, int nmissing, int *missing,
    uint8_t **rows, uint8_t **invrows, const uint8_t *used)
{
	int i, j, ii, jj;
	uint8_t log;

	/*
	 * Assert that the first nmissing entries from the array of used
	 * columns correspond to parity columns and that subsequent entries
	 * correspond to data columns.
	 */
	for (i = 0; i < nmissing; i++) {
		ASSERT3S(used[i], <, rr->rr_firstdatacol);
	}
	for (; i < n; i++) {
		ASSERT3S(used[i], >=, rr->rr_firstdatacol);
	}

	/*
	 * First initialize the storage where we'll compute the inverse rows.
	 */
	for (i = 0; i < nmissing; i++) {
		for (j = 0; j < n; j++) {
			invrows[i][j] = (i == j) ? 1 : 0;
		}
	}

	/*
	 * Subtract all trivial rows from the rows of consequence.
	 */
	for (i = 0; i < nmissing; i++) {
		for (j = nmissing; j < n; j++) {
			ASSERT3U(used[j], >=, rr->rr_firstdatacol);
			jj = used[j] - rr->rr_firstdatacol;
			ASSERT3S(jj, <, n);
			invrows[i][j] = rows[i][jj];
			rows[i][jj] = 0;
		}
	}

	/*
	 * For each of the rows of interest, we must normalize it and subtract
	 * a multiple of it from the other rows.
	 */
	for (i = 0; i < nmissing; i++) {
		for (j = 0; j < missing[i]; j++) {
			ASSERT0(rows[i][j]);
		}
		ASSERT3U(rows[i][missing[i]], !=, 0);

		/*
		 * Compute the inverse of the first element and multiply each
		 * element in the row by that value.
		 */
		log = 255 - vdev_raidz_log2[rows[i][missing[i]]];

		for (j = 0; j < n; j++) {
			rows[i][j] = vdev_raidz_exp2(rows[i][j], log);
			invrows[i][j] = vdev_raidz_exp2(invrows[i][j], log);
		}

		for (ii = 0; ii < nmissing; ii++) {
			if (i == ii)
				continue;

			ASSERT3U(rows[ii][missing[i]], !=, 0);

			log = vdev_raidz_log2[rows[ii][missing[i]]];

			for (j = 0; j < n; j++) {
				rows[ii][j] ^=
				    vdev_raidz_exp2(rows[i][j], log);
				invrows[ii][j] ^=
				    vdev_raidz_exp2(invrows[i][j], log);
			}
		}
	}

	/*
	 * Verify that the data that is left in the rows are properly part of
	 * an identity matrix.
	 */
	for (i = 0; i < nmissing; i++) {
		for (j = 0; j < n; j++) {
			if (j == missing[i]) {
				ASSERT3U(rows[i][j], ==, 1);
			} else {
				ASSERT0(rows[i][j]);
			}
		}
	}
}

static void
vdev_raidz_matrix_reconstruct(raidz_row_t *rr, int n, int nmissing,
    int *missing, uint8_t **invrows, const uint8_t *used)
{
	int i, j, x, cc, c;
	uint8_t *src;
	uint64_t ccount;
	uint8_t *dst[VDEV_RAIDZ_MAXPARITY] = { NULL };
	uint64_t dcount[VDEV_RAIDZ_MAXPARITY] = { 0 };
	uint8_t log = 0;
	uint8_t val;
	int ll;
	uint8_t *invlog[VDEV_RAIDZ_MAXPARITY];
	uint8_t *p, *pp;
	size_t psize;

	psize = sizeof (invlog[0][0]) * n * nmissing;
	p = kmem_alloc(psize, KM_SLEEP);

	for (pp = p, i = 0; i < nmissing; i++) {
		invlog[i] = pp;
		pp += n;
	}

	for (i = 0; i < nmissing; i++) {
		for (j = 0; j < n; j++) {
			ASSERT3U(invrows[i][j], !=, 0);
			invlog[i][j] = vdev_raidz_log2[invrows[i][j]];
		}
	}

	for (i = 0; i < n; i++) {
		c = used[i];
		ASSERT3U(c, <, rr->rr_cols);

		ccount = rr->rr_col[c].rc_size;
		ASSERT(ccount >= rr->rr_col[missing[0]].rc_size || i > 0);
		if (ccount == 0)
			continue;
		src = abd_to_buf(rr->rr_col[c].rc_abd);
		for (j = 0; j < nmissing; j++) {
			cc = missing[j] + rr->rr_firstdatacol;
			ASSERT3U(cc, >=, rr->rr_firstdatacol);
			ASSERT3U(cc, <, rr->rr_cols);
			ASSERT3U(cc, !=, c);

			dcount[j] = rr->rr_col[cc].rc_size;
			if (dcount[j] != 0)
				dst[j] = abd_to_buf(rr->rr_col[cc].rc_abd);
		}

		for (x = 0; x < ccount; x++, src++) {
			if (*src != 0)
				log = vdev_raidz_log2[*src];

			for (cc = 0; cc < nmissing; cc++) {
				if (x >= dcount[cc])
					continue;

				if (*src == 0) {
					val = 0;
				} else {
					if ((ll = log + invlog[cc][i]) >= 255)
						ll -= 255;
					val = vdev_raidz_pow2[ll];
				}

				if (i == 0)
					dst[cc][x] = val;
				else
					dst[cc][x] ^= val;
			}
		}
	}

	kmem_free(p, psize);
}

static void
vdev_raidz_reconstruct_general(raidz_row_t *rr, int *tgts, int ntgts)
{
	int n, i, c, t, tt;
	int nmissing_rows;
	int missing_rows[VDEV_RAIDZ_MAXPARITY];
	int parity_map[VDEV_RAIDZ_MAXPARITY];
	uint8_t *p, *pp;
	size_t psize;
	uint8_t *rows[VDEV_RAIDZ_MAXPARITY];
	uint8_t *invrows[VDEV_RAIDZ_MAXPARITY];
	uint8_t *used;

	abd_t **bufs = NULL;

	/*
	 * Matrix reconstruction can't use scatter ABDs yet, so we allocate
	 * temporary linear ABDs if any non-linear ABDs are found.
	 */
	for (i = rr->rr_firstdatacol; i < rr->rr_cols; i++) {
		if (!abd_is_linear(rr->rr_col[i].rc_abd)) {
			bufs = kmem_alloc(rr->rr_cols * sizeof (abd_t *),
			    KM_PUSHPAGE);

			for (c = rr->rr_firstdatacol; c < rr->rr_cols; c++) {
				raidz_col_t *col = &rr->rr_col[c];

				bufs[c] = col->rc_abd;
				if (bufs[c] != NULL) {
					col->rc_abd = abd_alloc_linear(
					    col->rc_size, B_TRUE);
					abd_copy(col->rc_abd, bufs[c],
					    col->rc_size);
				}
			}

			break;
		}
	}

	n = rr->rr_cols - rr->rr_firstdatacol;

	/*
	 * Figure out which data columns are missing.
	 */
	nmissing_rows = 0;
	for (t = 0; t < ntgts; t++) {
		if (tgts[t] >= rr->rr_firstdatacol) {
			missing_rows[nmissing_rows++] =
			    tgts[t] - rr->rr_firstdatacol;
		}
	}

	/*
	 * Figure out which parity columns to use to help generate the missing
	 * data columns.
	 */
	for (tt = 0, c = 0, i = 0; i < nmissing_rows; c++) {
		ASSERT(tt < ntgts);
		ASSERT(c < rr->rr_firstdatacol);

		/*
		 * Skip any targeted parity columns.
		 */
		if (c == tgts[tt]) {
			tt++;
			continue;
		}

		parity_map[i] = c;
		i++;
	}

	psize = (sizeof (rows[0][0]) + sizeof (invrows[0][0])) *
	    nmissing_rows * n + sizeof (used[0]) * n;
	p = kmem_alloc(psize, KM_SLEEP);

	for (pp = p, i = 0; i < nmissing_rows; i++) {
		rows[i] = pp;
		pp += n;
		invrows[i] = pp;
		pp += n;
	}
	used = pp;

	for (i = 0; i < nmissing_rows; i++) {
		used[i] = parity_map[i];
	}

	for (tt = 0, c = rr->rr_firstdatacol; c < rr->rr_cols; c++) {
		if (tt < nmissing_rows &&
		    c == missing_rows[tt] + rr->rr_firstdatacol) {
			tt++;
			continue;
		}

		ASSERT3S(i, <, n);
		used[i] = c;
		i++;
	}

	/*
	 * Initialize the interesting rows of the matrix.
	 */
	vdev_raidz_matrix_init(rr, n, nmissing_rows, parity_map, rows);

	/*
	 * Invert the matrix.
	 */
	vdev_raidz_matrix_invert(rr, n, nmissing_rows, missing_rows, rows,
	    invrows, used);

	/*
	 * Reconstruct the missing data using the generated matrix.
	 */
	vdev_raidz_matrix_reconstruct(rr, n, nmissing_rows, missing_rows,
	    invrows, used);

	kmem_free(p, psize);

	/*
	 * copy back from temporary linear abds and free them
	 */
	if (bufs) {
		for (c = rr->rr_firstdatacol; c < rr->rr_cols; c++) {
			raidz_col_t *col = &rr->rr_col[c];

			if (bufs[c] != NULL) {
				abd_copy(bufs[c], col->rc_abd, col->rc_size);
				abd_free(col->rc_abd);
			}
			col->rc_abd = bufs[c];
		}
		kmem_free(bufs, rr->rr_cols * sizeof (abd_t *));
	}
}

static void
vdev_raidz_reconstruct_row(raidz_map_t *rm, raidz_row_t *rr,
    const int *t, int nt)
{
	int tgts[VDEV_RAIDZ_MAXPARITY], *dt;
	int ntgts;
	int i, c, ret;
	int nbadparity, nbaddata;
	int parity_valid[VDEV_RAIDZ_MAXPARITY];

	nbadparity = rr->rr_firstdatacol;
	nbaddata = rr->rr_cols - nbadparity;
	ntgts = 0;
	for (i = 0, c = 0; c < rr->rr_cols; c++) {
		if (c < rr->rr_firstdatacol)
			parity_valid[c] = B_FALSE;

		if (i < nt && c == t[i]) {
			tgts[ntgts++] = c;
			i++;
		} else if (rr->rr_col[c].rc_error != 0) {
			tgts[ntgts++] = c;
		} else if (c >= rr->rr_firstdatacol) {
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
	ret = vdev_raidz_math_reconstruct(rm, rr, parity_valid, dt, nbaddata);
	if (ret != RAIDZ_ORIGINAL_IMPL)
		return;

	/*
	 * See if we can use any of our optimized reconstruction routines.
	 */
	switch (nbaddata) {
	case 1:
		if (parity_valid[VDEV_RAIDZ_P]) {
			vdev_raidz_reconstruct_p(rr, dt, 1);
			return;
		}

		ASSERT(rr->rr_firstdatacol > 1);

		if (parity_valid[VDEV_RAIDZ_Q]) {
			vdev_raidz_reconstruct_q(rr, dt, 1);
			return;
		}

		ASSERT(rr->rr_firstdatacol > 2);
		break;

	case 2:
		ASSERT(rr->rr_firstdatacol > 1);

		if (parity_valid[VDEV_RAIDZ_P] &&
		    parity_valid[VDEV_RAIDZ_Q]) {
			vdev_raidz_reconstruct_pq(rr, dt, 2);
			return;
		}

		ASSERT(rr->rr_firstdatacol > 2);

		break;
	}

	vdev_raidz_reconstruct_general(rr, tgts, ntgts);
}

static int
vdev_raidz_open(vdev_t *vd, uint64_t *asize, uint64_t *max_asize,
    uint64_t *logical_ashift, uint64_t *physical_ashift)
{
	vdev_raidz_t *vdrz = vd->vdev_tsd;
	uint64_t nparity = vdrz->vd_nparity;
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
		vdev_t *cvd = vd->vdev_child[c];

		if (cvd->vdev_open_error != 0) {
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
	for (int c = 0; c < vd->vdev_children; c++) {
		if (vd->vdev_child[c] != NULL)
			vdev_close(vd->vdev_child[c]);
	}
}

static uint64_t
vdev_raidz_asize(vdev_t *vd, uint64_t psize)
{
	vdev_raidz_t *vdrz = vd->vdev_tsd;
	uint64_t asize;
	uint64_t ashift = vd->vdev_top->vdev_ashift;
	uint64_t cols = vdrz->vd_logical_width;
	uint64_t nparity = vdrz->vd_nparity;

	asize = ((psize - 1) >> ashift) + 1;
	asize += nparity * ((asize + cols - nparity - 1) / (cols - nparity));
	asize = roundup(asize, nparity + 1) << ashift;

	return (asize);
}

/*
 * The allocatable space for a raidz vdev is N * sizeof(smallest child)
 * so each child must provide at least 1/Nth of its asize.
 */
static uint64_t
vdev_raidz_min_asize(vdev_t *vd)
{
	return ((vd->vdev_min_asize + vd->vdev_children - 1) /
	    vd->vdev_children);
}

void
vdev_raidz_child_done(zio_t *zio)
{
	raidz_col_t *rc = zio->io_private;

	ASSERT3P(rc->rc_abd, !=, NULL);
	rc->rc_error = zio->io_error;
	rc->rc_tried = 1;
	rc->rc_skipped = 0;
}

static void
vdev_raidz_io_verify(vdev_t *vd, raidz_row_t *rr, int col)
{
#ifdef ZFS_DEBUG
	vdev_t *tvd = vd->vdev_top;

	range_seg64_t logical_rs, physical_rs, remain_rs;
	logical_rs.rs_start = rr->rr_offset;
	logical_rs.rs_end = logical_rs.rs_start +
	    vdev_raidz_asize(vd, rr->rr_size);

	raidz_col_t *rc = &rr->rr_col[col];
	vdev_t *cvd = vd->vdev_child[rc->rc_devidx];

	vdev_xlate(cvd, &logical_rs, &physical_rs, &remain_rs);
	ASSERT(vdev_xlate_is_empty(&remain_rs));
	ASSERT3U(rc->rc_offset, ==, physical_rs.rs_start);
	ASSERT3U(rc->rc_offset, <, physical_rs.rs_end);
	/*
	 * It would be nice to assert that rs_end is equal
	 * to rc_offset + rc_size but there might be an
	 * optional I/O at the end that is not accounted in
	 * rc_size.
	 */
	if (physical_rs.rs_end > rc->rc_offset + rc->rc_size) {
		ASSERT3U(physical_rs.rs_end, ==, rc->rc_offset +
		    rc->rc_size + (1 << tvd->vdev_ashift));
	} else {
		ASSERT3U(physical_rs.rs_end, ==, rc->rc_offset + rc->rc_size);
	}
#endif
}

static void
vdev_raidz_io_start_write(zio_t *zio, raidz_row_t *rr, uint64_t ashift)
{
	vdev_t *vd = zio->io_vd;
	raidz_map_t *rm = zio->io_vsd;

	vdev_raidz_generate_parity_row(rm, rr);

	for (int c = 0; c < rr->rr_scols; c++) {
		raidz_col_t *rc = &rr->rr_col[c];
		vdev_t *cvd = vd->vdev_child[rc->rc_devidx];

		/* Verify physical to logical translation */
		vdev_raidz_io_verify(vd, rr, c);

		if (rc->rc_size > 0) {
			ASSERT3P(rc->rc_abd, !=, NULL);
			zio_nowait(zio_vdev_child_io(zio, NULL, cvd,
			    rc->rc_offset, rc->rc_abd,
			    abd_get_size(rc->rc_abd), zio->io_type,
			    zio->io_priority, 0, vdev_raidz_child_done, rc));
		} else {
			/*
			 * Generate optional write for skip sector to improve
			 * aggregation contiguity.
			 */
			ASSERT3P(rc->rc_abd, ==, NULL);
			zio_nowait(zio_vdev_child_io(zio, NULL, cvd,
			    rc->rc_offset, NULL, 1ULL << ashift,
			    zio->io_type, zio->io_priority,
			    ZIO_FLAG_NODATA | ZIO_FLAG_OPTIONAL, NULL,
			    NULL));
		}
	}
}

static void
vdev_raidz_io_start_read(zio_t *zio, raidz_row_t *rr)
{
	vdev_t *vd = zio->io_vd;

	/*
	 * Iterate over the columns in reverse order so that we hit the parity
	 * last -- any errors along the way will force us to read the parity.
	 */
	for (int c = rr->rr_cols - 1; c >= 0; c--) {
		raidz_col_t *rc = &rr->rr_col[c];
		if (rc->rc_size == 0)
			continue;
		vdev_t *cvd = vd->vdev_child[rc->rc_devidx];
		if (!vdev_readable(cvd)) {
			if (c >= rr->rr_firstdatacol)
				rr->rr_missingdata++;
			else
				rr->rr_missingparity++;
			rc->rc_error = SET_ERROR(ENXIO);
			rc->rc_tried = 1;	/* don't even try */
			rc->rc_skipped = 1;
			continue;
		}
		if (vdev_dtl_contains(cvd, DTL_MISSING, zio->io_txg, 1)) {
			if (c >= rr->rr_firstdatacol)
				rr->rr_missingdata++;
			else
				rr->rr_missingparity++;
			rc->rc_error = SET_ERROR(ESTALE);
			rc->rc_skipped = 1;
			continue;
		}
		if (c >= rr->rr_firstdatacol || rr->rr_missingdata > 0 ||
		    (zio->io_flags & (ZIO_FLAG_SCRUB | ZIO_FLAG_RESILVER))) {
			zio_nowait(zio_vdev_child_io(zio, NULL, cvd,
			    rc->rc_offset, rc->rc_abd, rc->rc_size,
			    zio->io_type, zio->io_priority, 0,
			    vdev_raidz_child_done, rc));
		}
	}
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
	vdev_raidz_t *vdrz = vd->vdev_tsd;

	raidz_map_t *rm = vdev_raidz_map_alloc(zio, tvd->vdev_ashift,
	    vdrz->vd_logical_width, vdrz->vd_nparity);
	zio->io_vsd = rm;
	zio->io_vsd_ops = &vdev_raidz_vsd_ops;

	/*
	 * Until raidz expansion is implemented all maps for a raidz vdev
	 * contain a single row.
	 */
	ASSERT3U(rm->rm_nrows, ==, 1);
	raidz_row_t *rr = rm->rm_row[0];

	if (zio->io_type == ZIO_TYPE_WRITE) {
		vdev_raidz_io_start_write(zio, rr, tvd->vdev_ashift);
	} else {
		ASSERT(zio->io_type == ZIO_TYPE_READ);
		vdev_raidz_io_start_read(zio, rr);
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

	if (!(zio->io_flags & ZIO_FLAG_SPECULATIVE) &&
	    zio->io_priority != ZIO_PRIORITY_REBUILD) {
		zio_bad_cksum_t zbc;
		raidz_map_t *rm = zio->io_vsd;

		zbc.zbc_has_cksum = 0;
		zbc.zbc_injected = rm->rm_ecksuminjected;

		(void) zfs_ereport_post_checksum(zio->io_spa, vd,
		    &zio->io_bookmark, zio, rc->rc_offset, rc->rc_size,
		    rc->rc_abd, bad_data, &zbc);
		mutex_enter(&vd->vdev_stat_lock);
		vd->vdev_stat.vs_checksum_errors++;
		mutex_exit(&vd->vdev_stat_lock);
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

	bzero(&zbc, sizeof (zio_bad_cksum_t));

	int ret = zio_checksum_error(zio, &zbc);
	if (ret != 0 && zbc.zbc_injected != 0)
		rm->rm_ecksuminjected = 1;

	return (ret);
}

/*
 * Generate the parity from the data columns. If we tried and were able to
 * read the parity without error, verify that the generated parity matches the
 * data we read. If it doesn't, we fire off a checksum error. Return the
 * number of such failures.
 */
static int
raidz_parity_verify(zio_t *zio, raidz_row_t *rr)
{
	abd_t *orig[VDEV_RAIDZ_MAXPARITY];
	int c, ret = 0;
	raidz_map_t *rm = zio->io_vsd;
	raidz_col_t *rc;

	blkptr_t *bp = zio->io_bp;
	enum zio_checksum checksum = (bp == NULL ? zio->io_prop.zp_checksum :
	    (BP_IS_GANG(bp) ? ZIO_CHECKSUM_GANG_HEADER : BP_GET_CHECKSUM(bp)));

	if (checksum == ZIO_CHECKSUM_NOPARITY)
		return (ret);

	for (c = 0; c < rr->rr_firstdatacol; c++) {
		rc = &rr->rr_col[c];
		if (!rc->rc_tried || rc->rc_error != 0)
			continue;

		orig[c] = abd_alloc_sametype(rc->rc_abd, rc->rc_size);
		abd_copy(orig[c], rc->rc_abd, rc->rc_size);
	}

	/*
	 * Regenerates parity even for !tried||rc_error!=0 columns.  This
	 * isn't harmful but it does have the side effect of fixing stuff
	 * we didn't realize was necessary (i.e. even if we return 0).
	 */
	vdev_raidz_generate_parity_row(rm, rr);

	for (c = 0; c < rr->rr_firstdatacol; c++) {
		rc = &rr->rr_col[c];

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
vdev_raidz_worst_error(raidz_row_t *rr)
{
	int error = 0;

	for (int c = 0; c < rr->rr_cols; c++)
		error = zio_worst_error(error, rr->rr_col[c].rc_error);

	return (error);
}

static void
vdev_raidz_io_done_verified(zio_t *zio, raidz_row_t *rr)
{
	int unexpected_errors = 0;
	int parity_errors = 0;
	int parity_untried = 0;
	int data_errors = 0;

	ASSERT3U(zio->io_type, ==, ZIO_TYPE_READ);

	for (int c = 0; c < rr->rr_cols; c++) {
		raidz_col_t *rc = &rr->rr_col[c];

		if (rc->rc_error) {
			if (c < rr->rr_firstdatacol)
				parity_errors++;
			else
				data_errors++;

			if (!rc->rc_skipped)
				unexpected_errors++;
		} else if (c < rr->rr_firstdatacol && !rc->rc_tried) {
			parity_untried++;
		}
	}

	/*
	 * If we read more parity disks than were used for
	 * reconstruction, confirm that the other parity disks produced
	 * correct data.
	 *
	 * Note that we also regenerate parity when resilvering so we
	 * can write it out to failed devices later.
	 */
	if (parity_errors + parity_untried <
	    rr->rr_firstdatacol - data_errors ||
	    (zio->io_flags & ZIO_FLAG_RESILVER)) {
		int n = raidz_parity_verify(zio, rr);
		unexpected_errors += n;
		ASSERT3U(parity_errors + n, <=, rr->rr_firstdatacol);
	}

	if (zio->io_error == 0 && spa_writeable(zio->io_spa) &&
	    (unexpected_errors > 0 || (zio->io_flags & ZIO_FLAG_RESILVER))) {
		/*
		 * Use the good data we have in hand to repair damaged children.
		 */
		for (int c = 0; c < rr->rr_cols; c++) {
			raidz_col_t *rc = &rr->rr_col[c];
			vdev_t *vd = zio->io_vd;
			vdev_t *cvd = vd->vdev_child[rc->rc_devidx];

			if (!rc->rc_allow_repair) {
				continue;
			} else if (!rc->rc_force_repair &&
			    (rc->rc_error == 0 || rc->rc_size == 0)) {
				continue;
			}

			zio_nowait(zio_vdev_child_io(zio, NULL, cvd,
			    rc->rc_offset, rc->rc_abd, rc->rc_size,
			    ZIO_TYPE_WRITE,
			    zio->io_priority == ZIO_PRIORITY_REBUILD ?
			    ZIO_PRIORITY_REBUILD : ZIO_PRIORITY_ASYNC_WRITE,
			    ZIO_FLAG_IO_REPAIR | (unexpected_errors ?
			    ZIO_FLAG_SELF_HEAL : 0), NULL, NULL));
		}
	}
}

static void
raidz_restore_orig_data(raidz_map_t *rm)
{
	for (int i = 0; i < rm->rm_nrows; i++) {
		raidz_row_t *rr = rm->rm_row[i];
		for (int c = 0; c < rr->rr_cols; c++) {
			raidz_col_t *rc = &rr->rr_col[c];
			if (rc->rc_need_orig_restore) {
				abd_copy(rc->rc_abd,
				    rc->rc_orig_data, rc->rc_size);
				rc->rc_need_orig_restore = B_FALSE;
			}
		}
	}
}

/*
 * returns EINVAL if reconstruction of the block will not be possible
 * returns ECKSUM if this specific reconstruction failed
 * returns 0 on successful reconstruction
 */
static int
raidz_reconstruct(zio_t *zio, int *ltgts, int ntgts, int nparity)
{
	raidz_map_t *rm = zio->io_vsd;

	/* Reconstruct each row */
	for (int r = 0; r < rm->rm_nrows; r++) {
		raidz_row_t *rr = rm->rm_row[r];
		int my_tgts[VDEV_RAIDZ_MAXPARITY]; /* value is child id */
		int t = 0;
		int dead = 0;
		int dead_data = 0;

		for (int c = 0; c < rr->rr_cols; c++) {
			raidz_col_t *rc = &rr->rr_col[c];
			ASSERT0(rc->rc_need_orig_restore);
			if (rc->rc_error != 0) {
				dead++;
				if (c >= nparity)
					dead_data++;
				continue;
			}
			if (rc->rc_size == 0)
				continue;
			for (int lt = 0; lt < ntgts; lt++) {
				if (rc->rc_devidx == ltgts[lt]) {
					if (rc->rc_orig_data == NULL) {
						rc->rc_orig_data =
						    abd_alloc_linear(
						    rc->rc_size, B_TRUE);
						abd_copy(rc->rc_orig_data,
						    rc->rc_abd, rc->rc_size);
					}
					rc->rc_need_orig_restore = B_TRUE;

					dead++;
					if (c >= nparity)
						dead_data++;
					my_tgts[t++] = c;
					break;
				}
			}
		}
		if (dead > nparity) {
			/* reconstruction not possible */
			raidz_restore_orig_data(rm);
			return (EINVAL);
		}
		if (dead_data > 0)
			vdev_raidz_reconstruct_row(rm, rr, my_tgts, t);
	}

	/* Check for success */
	if (raidz_checksum_verify(zio) == 0) {

		/* Reconstruction succeeded - report errors */
		for (int i = 0; i < rm->rm_nrows; i++) {
			raidz_row_t *rr = rm->rm_row[i];

			for (int c = 0; c < rr->rr_cols; c++) {
				raidz_col_t *rc = &rr->rr_col[c];
				if (rc->rc_need_orig_restore) {
					/*
					 * Note: if this is a parity column,
					 * we don't really know if it's wrong.
					 * We need to let
					 * vdev_raidz_io_done_verified() check
					 * it, and if we set rc_error, it will
					 * think that it is a "known" error
					 * that doesn't need to be checked
					 * or corrected.
					 */
					if (rc->rc_error == 0 &&
					    c >= rr->rr_firstdatacol) {
						raidz_checksum_error(zio,
						    rc, rc->rc_orig_data);
						rc->rc_error =
						    SET_ERROR(ECKSUM);
					}
					rc->rc_need_orig_restore = B_FALSE;
				}
			}

			vdev_raidz_io_done_verified(zio, rr);
		}

		zio_checksum_verified(zio);

		return (0);
	}

	/* Reconstruction failed - restore original data */
	raidz_restore_orig_data(rm);
	return (ECKSUM);
}

/*
 * Iterate over all combinations of N bad vdevs and attempt a reconstruction.
 * Note that the algorithm below is non-optimal because it doesn't take into
 * account how reconstruction is actually performed. For example, with
 * triple-parity RAID-Z the reconstruction procedure is the same if column 4
 * is targeted as invalid as if columns 1 and 4 are targeted since in both
 * cases we'd only use parity information in column 0.
 *
 * The order that we find the various possible combinations of failed
 * disks is dictated by these rules:
 * - Examine each "slot" (the "i" in tgts[i])
 *   - Try to increment this slot (tgts[i] = tgts[i] + 1)
 *   - if we can't increment because it runs into the next slot,
 *     reset our slot to the minimum, and examine the next slot
 *
 *  For example, with a 6-wide RAIDZ3, and no known errors (so we have to choose
 *  3 columns to reconstruct), we will generate the following sequence:
 *
 *  STATE        ACTION
 *  0 1 2        special case: skip since these are all parity
 *  0 1   3      first slot: reset to 0; middle slot: increment to 2
 *  0   2 3      first slot: increment to 1
 *    1 2 3      first: reset to 0; middle: reset to 1; last: increment to 4
 *  0 1     4    first: reset to 0; middle: increment to 2
 *  0   2   4    first: increment to 1
 *    1 2   4    first: reset to 0; middle: increment to 3
 *  0     3 4    first: increment to 1
 *    1   3 4    first: increment to 2
 *      2 3 4    first: reset to 0; middle: reset to 1; last: increment to 5
 *  0 1       5  first: reset to 0; middle: increment to 2
 *  0   2     5  first: increment to 1
 *    1 2     5  first: reset to 0; middle: increment to 3
 *  0     3   5  first: increment to 1
 *    1   3   5  first: increment to 2
 *      2 3   5  first: reset to 0; middle: increment to 4
 *  0       4 5  first: increment to 1
 *    1     4 5  first: increment to 2
 *      2   4 5  first: increment to 3
 *        3 4 5  done
 *
 * This strategy works for dRAID but is less efficient when there are a large
 * number of child vdevs and therefore permutations to check. Furthermore,
 * since the raidz_map_t rows likely do not overlap reconstruction would be
 * possible as long as there are no more than nparity data errors per row.
 * These additional permutations are not currently checked but could be as
 * a future improvement.
 */
static int
vdev_raidz_combrec(zio_t *zio)
{
	int nparity = vdev_get_nparity(zio->io_vd);
	raidz_map_t *rm = zio->io_vsd;

	/* Check if there's enough data to attempt reconstrution. */
	for (int i = 0; i < rm->rm_nrows; i++) {
		raidz_row_t *rr = rm->rm_row[i];
		int total_errors = 0;

		for (int c = 0; c < rr->rr_cols; c++) {
			if (rr->rr_col[c].rc_error)
				total_errors++;
		}

		if (total_errors > nparity)
			return (vdev_raidz_worst_error(rr));
	}

	for (int num_failures = 1; num_failures <= nparity; num_failures++) {
		int tstore[VDEV_RAIDZ_MAXPARITY + 2];
		int *ltgts = &tstore[1]; /* value is logical child ID */

		/* Determine number of logical children, n */
		int n = zio->io_vd->vdev_children;

		ASSERT3U(num_failures, <=, nparity);
		ASSERT3U(num_failures, <=, VDEV_RAIDZ_MAXPARITY);

		/* Handle corner cases in combrec logic */
		ltgts[-1] = -1;
		for (int i = 0; i < num_failures; i++) {
			ltgts[i] = i;
		}
		ltgts[num_failures] = n;

		for (;;) {
			int err = raidz_reconstruct(zio, ltgts, num_failures,
			    nparity);
			if (err == EINVAL) {
				/*
				 * Reconstruction not possible with this #
				 * failures; try more failures.
				 */
				break;
			} else if (err == 0)
				return (0);

			/* Compute next targets to try */
			for (int t = 0; ; t++) {
				ASSERT3U(t, <, num_failures);
				ltgts[t]++;
				if (ltgts[t] == n) {
					/* try more failures */
					ASSERT3U(t, ==, num_failures - 1);
					break;
				}

				ASSERT3U(ltgts[t], <, n);
				ASSERT3U(ltgts[t], <=, ltgts[t + 1]);

				/*
				 * If that spot is available, we're done here.
				 * Try the next combination.
				 */
				if (ltgts[t] != ltgts[t + 1])
					break;

				/*
				 * Otherwise, reset this tgt to the minimum,
				 * and move on to the next tgt.
				 */
				ltgts[t] = ltgts[t - 1] + 1;
				ASSERT3U(ltgts[t], ==, t);
			}

			/* Increase the number of failures and keep trying. */
			if (ltgts[num_failures - 1] == n)
				break;
		}
	}

	return (ECKSUM);
}

void
vdev_raidz_reconstruct(raidz_map_t *rm, const int *t, int nt)
{
	for (uint64_t row = 0; row < rm->rm_nrows; row++) {
		raidz_row_t *rr = rm->rm_row[row];
		vdev_raidz_reconstruct_row(rm, rr, t, nt);
	}
}

/*
 * Complete a write IO operation on a RAIDZ VDev
 *
 * Outline:
 *   1. Check for errors on the child IOs.
 *   2. Return, setting an error code if too few child VDevs were written
 *      to reconstruct the data later.  Note that partial writes are
 *      considered successful if they can be reconstructed at all.
 */
static void
vdev_raidz_io_done_write_impl(zio_t *zio, raidz_row_t *rr)
{
	int total_errors = 0;

	ASSERT3U(rr->rr_missingparity, <=, rr->rr_firstdatacol);
	ASSERT3U(rr->rr_missingdata, <=, rr->rr_cols - rr->rr_firstdatacol);
	ASSERT3U(zio->io_type, ==, ZIO_TYPE_WRITE);

	for (int c = 0; c < rr->rr_cols; c++) {
		raidz_col_t *rc = &rr->rr_col[c];

		if (rc->rc_error) {
			ASSERT(rc->rc_error != ECKSUM);	/* child has no bp */

			total_errors++;
		}
	}

	/*
	 * Treat partial writes as a success. If we couldn't write enough
	 * columns to reconstruct the data, the I/O failed.  Otherwise,
	 * good enough.
	 *
	 * Now that we support write reallocation, it would be better
	 * to treat partial failure as real failure unless there are
	 * no non-degraded top-level vdevs left, and not update DTLs
	 * if we intend to reallocate.
	 */
	if (total_errors > rr->rr_firstdatacol) {
		zio->io_error = zio_worst_error(zio->io_error,
		    vdev_raidz_worst_error(rr));
	}
}

static void
vdev_raidz_io_done_reconstruct_known_missing(zio_t *zio, raidz_map_t *rm,
    raidz_row_t *rr)
{
	int parity_errors = 0;
	int parity_untried = 0;
	int data_errors = 0;
	int total_errors = 0;

	ASSERT3U(rr->rr_missingparity, <=, rr->rr_firstdatacol);
	ASSERT3U(rr->rr_missingdata, <=, rr->rr_cols - rr->rr_firstdatacol);
	ASSERT3U(zio->io_type, ==, ZIO_TYPE_READ);

	for (int c = 0; c < rr->rr_cols; c++) {
		raidz_col_t *rc = &rr->rr_col[c];

		if (rc->rc_error) {
			ASSERT(rc->rc_error != ECKSUM);	/* child has no bp */

			if (c < rr->rr_firstdatacol)
				parity_errors++;
			else
				data_errors++;

			total_errors++;
		} else if (c < rr->rr_firstdatacol && !rc->rc_tried) {
			parity_untried++;
		}
	}

	/*
	 * If there were data errors and the number of errors we saw was
	 * correctable -- less than or equal to the number of parity disks read
	 * -- reconstruct based on the missing data.
	 */
	if (data_errors != 0 &&
	    total_errors <= rr->rr_firstdatacol - parity_untried) {
		/*
		 * We either attempt to read all the parity columns or
		 * none of them. If we didn't try to read parity, we
		 * wouldn't be here in the correctable case. There must
		 * also have been fewer parity errors than parity
		 * columns or, again, we wouldn't be in this code path.
		 */
		ASSERT(parity_untried == 0);
		ASSERT(parity_errors < rr->rr_firstdatacol);

		/*
		 * Identify the data columns that reported an error.
		 */
		int n = 0;
		int tgts[VDEV_RAIDZ_MAXPARITY];
		for (int c = rr->rr_firstdatacol; c < rr->rr_cols; c++) {
			raidz_col_t *rc = &rr->rr_col[c];
			if (rc->rc_error != 0) {
				ASSERT(n < VDEV_RAIDZ_MAXPARITY);
				tgts[n++] = c;
			}
		}

		ASSERT(rr->rr_firstdatacol >= n);

		vdev_raidz_reconstruct_row(rm, rr, tgts, n);
	}
}

/*
 * Return the number of reads issued.
 */
static int
vdev_raidz_read_all(zio_t *zio, raidz_row_t *rr)
{
	vdev_t *vd = zio->io_vd;
	int nread = 0;

	rr->rr_missingdata = 0;
	rr->rr_missingparity = 0;

	/*
	 * If this rows contains empty sectors which are not required
	 * for a normal read then allocate an ABD for them now so they
	 * may be read, verified, and any needed repairs performed.
	 */
	if (rr->rr_nempty && rr->rr_abd_empty == NULL)
		vdev_draid_map_alloc_empty(zio, rr);

	for (int c = 0; c < rr->rr_cols; c++) {
		raidz_col_t *rc = &rr->rr_col[c];
		if (rc->rc_tried || rc->rc_size == 0)
			continue;

		zio_nowait(zio_vdev_child_io(zio, NULL,
		    vd->vdev_child[rc->rc_devidx],
		    rc->rc_offset, rc->rc_abd, rc->rc_size,
		    zio->io_type, zio->io_priority, 0,
		    vdev_raidz_child_done, rc));
		nread++;
	}
	return (nread);
}

/*
 * We're here because either there were too many errors to even attempt
 * reconstruction (total_errors == rm_first_datacol), or vdev_*_combrec()
 * failed. In either case, there is enough bad data to prevent reconstruction.
 * Start checksum ereports for all children which haven't failed.
 */
static void
vdev_raidz_io_done_unrecoverable(zio_t *zio)
{
	raidz_map_t *rm = zio->io_vsd;

	for (int i = 0; i < rm->rm_nrows; i++) {
		raidz_row_t *rr = rm->rm_row[i];

		for (int c = 0; c < rr->rr_cols; c++) {
			raidz_col_t *rc = &rr->rr_col[c];
			vdev_t *cvd = zio->io_vd->vdev_child[rc->rc_devidx];

			if (rc->rc_error != 0)
				continue;

			zio_bad_cksum_t zbc;
			zbc.zbc_has_cksum = 0;
			zbc.zbc_injected = rm->rm_ecksuminjected;

			(void) zfs_ereport_start_checksum(zio->io_spa,
			    cvd, &zio->io_bookmark, zio, rc->rc_offset,
			    rc->rc_size, &zbc);
			mutex_enter(&cvd->vdev_stat_lock);
			cvd->vdev_stat.vs_checksum_errors++;
			mutex_exit(&cvd->vdev_stat_lock);
		}
	}
}

void
vdev_raidz_io_done(zio_t *zio)
{
	raidz_map_t *rm = zio->io_vsd;

	if (zio->io_type == ZIO_TYPE_WRITE) {
		for (int i = 0; i < rm->rm_nrows; i++) {
			vdev_raidz_io_done_write_impl(zio, rm->rm_row[i]);
		}
	} else {
		for (int i = 0; i < rm->rm_nrows; i++) {
			raidz_row_t *rr = rm->rm_row[i];
			vdev_raidz_io_done_reconstruct_known_missing(zio,
			    rm, rr);
		}

		if (raidz_checksum_verify(zio) == 0) {
			for (int i = 0; i < rm->rm_nrows; i++) {
				raidz_row_t *rr = rm->rm_row[i];
				vdev_raidz_io_done_verified(zio, rr);
			}
			zio_checksum_verified(zio);
		} else {
			/*
			 * A sequential resilver has no checksum which makes
			 * combinatoral reconstruction impossible. This code
			 * path is unreachable since raidz_checksum_verify()
			 * has no checksum to verify and must succeed.
			 */
			ASSERT3U(zio->io_priority, !=, ZIO_PRIORITY_REBUILD);

			/*
			 * This isn't a typical situation -- either we got a
			 * read error or a child silently returned bad data.
			 * Read every block so we can try again with as much
			 * data and parity as we can track down. If we've
			 * already been through once before, all children will
			 * be marked as tried so we'll proceed to combinatorial
			 * reconstruction.
			 */
			int nread = 0;
			for (int i = 0; i < rm->rm_nrows; i++) {
				nread += vdev_raidz_read_all(zio,
				    rm->rm_row[i]);
			}
			if (nread != 0) {
				/*
				 * Normally our stage is VDEV_IO_DONE, but if
				 * we've already called redone(), it will have
				 * changed to VDEV_IO_START, in which case we
				 * don't want to call redone() again.
				 */
				if (zio->io_stage != ZIO_STAGE_VDEV_IO_START)
					zio_vdev_io_redone(zio);
				return;
			}

			zio->io_error = vdev_raidz_combrec(zio);
			if (zio->io_error == ECKSUM &&
			    !(zio->io_flags & ZIO_FLAG_SPECULATIVE)) {
				vdev_raidz_io_done_unrecoverable(zio);
			}
		}
	}
}

static void
vdev_raidz_state_change(vdev_t *vd, int faulted, int degraded)
{
	vdev_raidz_t *vdrz = vd->vdev_tsd;
	if (faulted > vdrz->vd_nparity)
		vdev_set_state(vd, B_FALSE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_NO_REPLICAS);
	else if (degraded + faulted != 0)
		vdev_set_state(vd, B_FALSE, VDEV_STATE_DEGRADED, VDEV_AUX_NONE);
	else
		vdev_set_state(vd, B_FALSE, VDEV_STATE_HEALTHY, VDEV_AUX_NONE);
}

/*
 * Determine if any portion of the provided block resides on a child vdev
 * with a dirty DTL and therefore needs to be resilvered.  The function
 * assumes that at least one DTL is dirty which implies that full stripe
 * width blocks must be resilvered.
 */
static boolean_t
vdev_raidz_need_resilver(vdev_t *vd, const dva_t *dva, size_t psize,
    uint64_t phys_birth)
{
	vdev_raidz_t *vdrz = vd->vdev_tsd;
	uint64_t dcols = vd->vdev_children;
	uint64_t nparity = vdrz->vd_nparity;
	uint64_t ashift = vd->vdev_top->vdev_ashift;
	/* The starting RAIDZ (parent) vdev sector of the block. */
	uint64_t b = DVA_GET_OFFSET(dva) >> ashift;
	/* The zio's size in units of the vdev's minimum sector size. */
	uint64_t s = ((psize - 1) >> ashift) + 1;
	/* The first column for this stripe. */
	uint64_t f = b % dcols;

	/* Unreachable by sequential resilver. */
	ASSERT3U(phys_birth, !=, TXG_UNKNOWN);

	if (!vdev_dtl_contains(vd, DTL_PARTIAL, phys_birth, 1))
		return (B_FALSE);

	if (s + nparity >= dcols)
		return (B_TRUE);

	for (uint64_t c = 0; c < s + nparity; c++) {
		uint64_t devidx = (f + c) % dcols;
		vdev_t *cvd = vd->vdev_child[devidx];

		/*
		 * dsl_scan_need_resilver() already checked vd with
		 * vdev_dtl_contains(). So here just check cvd with
		 * vdev_dtl_empty(), cheaper and a good approximation.
		 */
		if (!vdev_dtl_empty(cvd, DTL_PARTIAL))
			return (B_TRUE);
	}

	return (B_FALSE);
}

static void
vdev_raidz_xlate(vdev_t *cvd, const range_seg64_t *logical_rs,
    range_seg64_t *physical_rs, range_seg64_t *remain_rs)
{
	vdev_t *raidvd = cvd->vdev_parent;
	ASSERT(raidvd->vdev_ops == &vdev_raidz_ops);

	uint64_t width = raidvd->vdev_children;
	uint64_t tgt_col = cvd->vdev_id;
	uint64_t ashift = raidvd->vdev_top->vdev_ashift;

	/* make sure the offsets are block-aligned */
	ASSERT0(logical_rs->rs_start % (1 << ashift));
	ASSERT0(logical_rs->rs_end % (1 << ashift));
	uint64_t b_start = logical_rs->rs_start >> ashift;
	uint64_t b_end = logical_rs->rs_end >> ashift;

	uint64_t start_row = 0;
	if (b_start > tgt_col) /* avoid underflow */
		start_row = ((b_start - tgt_col - 1) / width) + 1;

	uint64_t end_row = 0;
	if (b_end > tgt_col)
		end_row = ((b_end - tgt_col - 1) / width) + 1;

	physical_rs->rs_start = start_row << ashift;
	physical_rs->rs_end = end_row << ashift;

	ASSERT3U(physical_rs->rs_start, <=, logical_rs->rs_start);
	ASSERT3U(physical_rs->rs_end - physical_rs->rs_start, <=,
	    logical_rs->rs_end - logical_rs->rs_start);
}

/*
 * Initialize private RAIDZ specific fields from the nvlist.
 */
static int
vdev_raidz_init(spa_t *spa, nvlist_t *nv, void **tsd)
{
	vdev_raidz_t *vdrz;
	uint64_t nparity;

	uint_t children;
	nvlist_t **child;
	int error = nvlist_lookup_nvlist_array(nv,
	    ZPOOL_CONFIG_CHILDREN, &child, &children);
	if (error != 0)
		return (SET_ERROR(EINVAL));

	if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_NPARITY, &nparity) == 0) {
		if (nparity == 0 || nparity > VDEV_RAIDZ_MAXPARITY)
			return (SET_ERROR(EINVAL));

		/*
		 * Previous versions could only support 1 or 2 parity
		 * device.
		 */
		if (nparity > 1 && spa_version(spa) < SPA_VERSION_RAIDZ2)
			return (SET_ERROR(EINVAL));
		else if (nparity > 2 && spa_version(spa) < SPA_VERSION_RAIDZ3)
			return (SET_ERROR(EINVAL));
	} else {
		/*
		 * We require the parity to be specified for SPAs that
		 * support multiple parity levels.
		 */
		if (spa_version(spa) >= SPA_VERSION_RAIDZ2)
			return (SET_ERROR(EINVAL));

		/*
		 * Otherwise, we default to 1 parity device for RAID-Z.
		 */
		nparity = 1;
	}

	vdrz = kmem_zalloc(sizeof (*vdrz), KM_SLEEP);
	vdrz->vd_logical_width = children;
	vdrz->vd_nparity = nparity;

	*tsd = vdrz;

	return (0);
}

static void
vdev_raidz_fini(vdev_t *vd)
{
	kmem_free(vd->vdev_tsd, sizeof (vdev_raidz_t));
}

/*
 * Add RAIDZ specific fields to the config nvlist.
 */
static void
vdev_raidz_config_generate(vdev_t *vd, nvlist_t *nv)
{
	ASSERT3P(vd->vdev_ops, ==, &vdev_raidz_ops);
	vdev_raidz_t *vdrz = vd->vdev_tsd;

	/*
	 * Make sure someone hasn't managed to sneak a fancy new vdev
	 * into a crufty old storage pool.
	 */
	ASSERT(vdrz->vd_nparity == 1 ||
	    (vdrz->vd_nparity <= 2 &&
	    spa_version(vd->vdev_spa) >= SPA_VERSION_RAIDZ2) ||
	    (vdrz->vd_nparity <= 3 &&
	    spa_version(vd->vdev_spa) >= SPA_VERSION_RAIDZ3));

	/*
	 * Note that we'll add these even on storage pools where they
	 * aren't strictly required -- older software will just ignore
	 * it.
	 */
	fnvlist_add_uint64(nv, ZPOOL_CONFIG_NPARITY, vdrz->vd_nparity);
}

static uint64_t
vdev_raidz_nparity(vdev_t *vd)
{
	vdev_raidz_t *vdrz = vd->vdev_tsd;
	return (vdrz->vd_nparity);
}

static uint64_t
vdev_raidz_ndisks(vdev_t *vd)
{
	return (vd->vdev_children);
}

vdev_ops_t vdev_raidz_ops = {
	.vdev_op_init = vdev_raidz_init,
	.vdev_op_fini = vdev_raidz_fini,
	.vdev_op_open = vdev_raidz_open,
	.vdev_op_close = vdev_raidz_close,
	.vdev_op_asize = vdev_raidz_asize,
	.vdev_op_min_asize = vdev_raidz_min_asize,
	.vdev_op_min_alloc = NULL,
	.vdev_op_io_start = vdev_raidz_io_start,
	.vdev_op_io_done = vdev_raidz_io_done,
	.vdev_op_state_change = vdev_raidz_state_change,
	.vdev_op_need_resilver = vdev_raidz_need_resilver,
	.vdev_op_hold = NULL,
	.vdev_op_rele = NULL,
	.vdev_op_remap = NULL,
	.vdev_op_xlate = vdev_raidz_xlate,
	.vdev_op_rebuild_asize = NULL,
	.vdev_op_metaslab_init = NULL,
	.vdev_op_config_generate = vdev_raidz_config_generate,
	.vdev_op_nparity = vdev_raidz_nparity,
	.vdev_op_ndisks = vdev_raidz_ndisks,
	.vdev_op_type = VDEV_TYPE_RAIDZ,	/* name of this vdev type */
	.vdev_op_leaf = B_FALSE			/* not a leaf vdev */
};
