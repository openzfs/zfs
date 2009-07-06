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

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/vdev_impl.h>
#include <sys/zio.h>
#include <sys/zio_checksum.h>
#include <sys/fs/zfs.h>
#include <sys/fm/fs/zfs.h>

/*
 * Virtual device vector for RAID-Z.
 *
 * This vdev supports both single and double parity. For single parity, we
 * use a simple XOR of all the data columns. For double parity, we use both
 * the simple XOR as well as a technique described in "The mathematics of
 * RAID-6" by H. Peter Anvin. This technique defines a Galois field, GF(2^8),
 * over the integers expressable in a single byte. Briefly, the operations on
 * the field are defined as follows:
 *
 *   o addition (+) is represented by a bitwise XOR
 *   o subtraction (-) is therefore identical to addition: A + B = A - B
 *   o multiplication of A by 2 is defined by the following bitwise expression:
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
 *
 * Observe that any number in the field (except for 0) can be expressed as a
 * power of 2 -- a generator for the field. We store a table of the powers of
 * 2 and logs base 2 for quick look ups, and exploit the fact that A * B can
 * be rewritten as 2^(log_2(A) + log_2(B)) (where '+' is normal addition rather
 * than field addition). The inverse of a field element A (A^-1) is A^254.
 *
 * The two parity columns, P and Q, over several data columns, D_0, ... D_n-1,
 * can be expressed by field operations:
 *
 *	P = D_0 + D_1 + ... + D_n-2 + D_n-1
 *	Q = 2^n-1 * D_0 + 2^n-2 * D_1 + ... + 2^1 * D_n-2 + 2^0 * D_n-1
 *	  = ((...((D_0) * 2 + D_1) * 2 + ...) * 2 + D_n-2) * 2 + D_n-1
 *
 * See the reconstruction code below for how P and Q can used individually or
 * in concert to recover missing data columns.
 */

typedef struct raidz_col {
	uint64_t rc_devidx;		/* child device index for I/O */
	uint64_t rc_offset;		/* device offset */
	uint64_t rc_size;		/* I/O size */
	void *rc_data;			/* I/O data */
	int rc_error;			/* I/O error for this device */
	uint8_t rc_tried;		/* Did we attempt this I/O column? */
	uint8_t rc_skipped;		/* Did we skip this I/O column? */
} raidz_col_t;

typedef struct raidz_map {
	uint64_t rm_cols;		/* Column count */
	uint64_t rm_bigcols;		/* Number of oversized columns */
	uint64_t rm_asize;		/* Actual total I/O size */
	uint64_t rm_missingdata;	/* Count of missing data devices */
	uint64_t rm_missingparity;	/* Count of missing parity devices */
	uint64_t rm_firstdatacol;	/* First data column/parity count */
	raidz_col_t rm_col[1];		/* Flexible array of I/O columns */
} raidz_map_t;

#define	VDEV_RAIDZ_P		0
#define	VDEV_RAIDZ_Q		1

#define	VDEV_RAIDZ_MAXPARITY	2

#define	VDEV_RAIDZ_MUL_2(a)	(((a) << 1) ^ (((a) & 0x80) ? 0x1d : 0))

/*
 * These two tables represent powers and logs of 2 in the Galois field defined
 * above. These values were computed by repeatedly multiplying by 2 as above.
 */
static const uint8_t vdev_raidz_pow2[256] = {
	0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80,
	0x1d, 0x3a, 0x74, 0xe8, 0xcd, 0x87, 0x13, 0x26,
	0x4c, 0x98, 0x2d, 0x5a, 0xb4, 0x75, 0xea, 0xc9,
	0x8f, 0x03, 0x06, 0x0c, 0x18, 0x30, 0x60, 0xc0,
	0x9d, 0x27, 0x4e, 0x9c, 0x25, 0x4a, 0x94, 0x35,
	0x6a, 0xd4, 0xb5, 0x77, 0xee, 0xc1, 0x9f, 0x23,
	0x46, 0x8c, 0x05, 0x0a, 0x14, 0x28, 0x50, 0xa0,
	0x5d, 0xba, 0x69, 0xd2, 0xb9, 0x6f, 0xde, 0xa1,
	0x5f, 0xbe, 0x61, 0xc2, 0x99, 0x2f, 0x5e, 0xbc,
	0x65, 0xca, 0x89, 0x0f, 0x1e, 0x3c, 0x78, 0xf0,
	0xfd, 0xe7, 0xd3, 0xbb, 0x6b, 0xd6, 0xb1, 0x7f,
	0xfe, 0xe1, 0xdf, 0xa3, 0x5b, 0xb6, 0x71, 0xe2,
	0xd9, 0xaf, 0x43, 0x86, 0x11, 0x22, 0x44, 0x88,
	0x0d, 0x1a, 0x34, 0x68, 0xd0, 0xbd, 0x67, 0xce,
	0x81, 0x1f, 0x3e, 0x7c, 0xf8, 0xed, 0xc7, 0x93,
	0x3b, 0x76, 0xec, 0xc5, 0x97, 0x33, 0x66, 0xcc,
	0x85, 0x17, 0x2e, 0x5c, 0xb8, 0x6d, 0xda, 0xa9,
	0x4f, 0x9e, 0x21, 0x42, 0x84, 0x15, 0x2a, 0x54,
	0xa8, 0x4d, 0x9a, 0x29, 0x52, 0xa4, 0x55, 0xaa,
	0x49, 0x92, 0x39, 0x72, 0xe4, 0xd5, 0xb7, 0x73,
	0xe6, 0xd1, 0xbf, 0x63, 0xc6, 0x91, 0x3f, 0x7e,
	0xfc, 0xe5, 0xd7, 0xb3, 0x7b, 0xf6, 0xf1, 0xff,
	0xe3, 0xdb, 0xab, 0x4b, 0x96, 0x31, 0x62, 0xc4,
	0x95, 0x37, 0x6e, 0xdc, 0xa5, 0x57, 0xae, 0x41,
	0x82, 0x19, 0x32, 0x64, 0xc8, 0x8d, 0x07, 0x0e,
	0x1c, 0x38, 0x70, 0xe0, 0xdd, 0xa7, 0x53, 0xa6,
	0x51, 0xa2, 0x59, 0xb2, 0x79, 0xf2, 0xf9, 0xef,
	0xc3, 0x9b, 0x2b, 0x56, 0xac, 0x45, 0x8a, 0x09,
	0x12, 0x24, 0x48, 0x90, 0x3d, 0x7a, 0xf4, 0xf5,
	0xf7, 0xf3, 0xfb, 0xeb, 0xcb, 0x8b, 0x0b, 0x16,
	0x2c, 0x58, 0xb0, 0x7d, 0xfa, 0xe9, 0xcf, 0x83,
	0x1b, 0x36, 0x6c, 0xd8, 0xad, 0x47, 0x8e, 0x01
};
static const uint8_t vdev_raidz_log2[256] = {
	0x00, 0x00, 0x01, 0x19, 0x02, 0x32, 0x1a, 0xc6,
	0x03, 0xdf, 0x33, 0xee, 0x1b, 0x68, 0xc7, 0x4b,
	0x04, 0x64, 0xe0, 0x0e, 0x34, 0x8d, 0xef, 0x81,
	0x1c, 0xc1, 0x69, 0xf8, 0xc8, 0x08, 0x4c, 0x71,
	0x05, 0x8a, 0x65, 0x2f, 0xe1, 0x24, 0x0f, 0x21,
	0x35, 0x93, 0x8e, 0xda, 0xf0, 0x12, 0x82, 0x45,
	0x1d, 0xb5, 0xc2, 0x7d, 0x6a, 0x27, 0xf9, 0xb9,
	0xc9, 0x9a, 0x09, 0x78, 0x4d, 0xe4, 0x72, 0xa6,
	0x06, 0xbf, 0x8b, 0x62, 0x66, 0xdd, 0x30, 0xfd,
	0xe2, 0x98, 0x25, 0xb3, 0x10, 0x91, 0x22, 0x88,
	0x36, 0xd0, 0x94, 0xce, 0x8f, 0x96, 0xdb, 0xbd,
	0xf1, 0xd2, 0x13, 0x5c, 0x83, 0x38, 0x46, 0x40,
	0x1e, 0x42, 0xb6, 0xa3, 0xc3, 0x48, 0x7e, 0x6e,
	0x6b, 0x3a, 0x28, 0x54, 0xfa, 0x85, 0xba, 0x3d,
	0xca, 0x5e, 0x9b, 0x9f, 0x0a, 0x15, 0x79, 0x2b,
	0x4e, 0xd4, 0xe5, 0xac, 0x73, 0xf3, 0xa7, 0x57,
	0x07, 0x70, 0xc0, 0xf7, 0x8c, 0x80, 0x63, 0x0d,
	0x67, 0x4a, 0xde, 0xed, 0x31, 0xc5, 0xfe, 0x18,
	0xe3, 0xa5, 0x99, 0x77, 0x26, 0xb8, 0xb4, 0x7c,
	0x11, 0x44, 0x92, 0xd9, 0x23, 0x20, 0x89, 0x2e,
	0x37, 0x3f, 0xd1, 0x5b, 0x95, 0xbc, 0xcf, 0xcd,
	0x90, 0x87, 0x97, 0xb2, 0xdc, 0xfc, 0xbe, 0x61,
	0xf2, 0x56, 0xd3, 0xab, 0x14, 0x2a, 0x5d, 0x9e,
	0x84, 0x3c, 0x39, 0x53, 0x47, 0x6d, 0x41, 0xa2,
	0x1f, 0x2d, 0x43, 0xd8, 0xb7, 0x7b, 0xa4, 0x76,
	0xc4, 0x17, 0x49, 0xec, 0x7f, 0x0c, 0x6f, 0xf6,
	0x6c, 0xa1, 0x3b, 0x52, 0x29, 0x9d, 0x55, 0xaa,
	0xfb, 0x60, 0x86, 0xb1, 0xbb, 0xcc, 0x3e, 0x5a,
	0xcb, 0x59, 0x5f, 0xb0, 0x9c, 0xa9, 0xa0, 0x51,
	0x0b, 0xf5, 0x16, 0xeb, 0x7a, 0x75, 0x2c, 0xd7,
	0x4f, 0xae, 0xd5, 0xe9, 0xe6, 0xe7, 0xad, 0xe8,
	0x74, 0xd6, 0xf4, 0xea, 0xa8, 0x50, 0x58, 0xaf,
};

/*
 * Multiply a given number by 2 raised to the given power.
 */
static uint8_t
vdev_raidz_exp2(uint_t a, int exp)
{
	if (a == 0)
		return (0);

	ASSERT(exp >= 0);
	ASSERT(vdev_raidz_log2[a] > 0 || a == 1);

	exp += vdev_raidz_log2[a];
	if (exp > 255)
		exp -= 255;

	return (vdev_raidz_pow2[exp]);
}

static void
vdev_raidz_map_free(zio_t *zio)
{
	raidz_map_t *rm = zio->io_vsd;
	int c;

	for (c = 0; c < rm->rm_firstdatacol; c++)
		zio_buf_free(rm->rm_col[c].rc_data, rm->rm_col[c].rc_size);

	kmem_free(rm, offsetof(raidz_map_t, rm_col[rm->rm_cols]));
}

static raidz_map_t *
vdev_raidz_map_alloc(zio_t *zio, uint64_t unit_shift, uint64_t dcols,
    uint64_t nparity)
{
	raidz_map_t *rm;
	uint64_t b = zio->io_offset >> unit_shift;
	uint64_t s = zio->io_size >> unit_shift;
	uint64_t f = b % dcols;
	uint64_t o = (b / dcols) << unit_shift;
	uint64_t q, r, c, bc, col, acols, coff, devidx;

	q = s / (dcols - nparity);
	r = s - q * (dcols - nparity);
	bc = (r == 0 ? 0 : r + nparity);

	acols = (q == 0 ? bc : dcols);

	rm = kmem_alloc(offsetof(raidz_map_t, rm_col[acols]), KM_SLEEP);

	rm->rm_cols = acols;
	rm->rm_bigcols = bc;
	rm->rm_asize = 0;
	rm->rm_missingdata = 0;
	rm->rm_missingparity = 0;
	rm->rm_firstdatacol = nparity;

	for (c = 0; c < acols; c++) {
		col = f + c;
		coff = o;
		if (col >= dcols) {
			col -= dcols;
			coff += 1ULL << unit_shift;
		}
		rm->rm_col[c].rc_devidx = col;
		rm->rm_col[c].rc_offset = coff;
		rm->rm_col[c].rc_size = (q + (c < bc)) << unit_shift;
		rm->rm_col[c].rc_data = NULL;
		rm->rm_col[c].rc_error = 0;
		rm->rm_col[c].rc_tried = 0;
		rm->rm_col[c].rc_skipped = 0;
		rm->rm_asize += rm->rm_col[c].rc_size;
	}

	rm->rm_asize = roundup(rm->rm_asize, (nparity + 1) << unit_shift);

	for (c = 0; c < rm->rm_firstdatacol; c++)
		rm->rm_col[c].rc_data = zio_buf_alloc(rm->rm_col[c].rc_size);

	rm->rm_col[c].rc_data = zio->io_data;

	for (c = c + 1; c < acols; c++)
		rm->rm_col[c].rc_data = (char *)rm->rm_col[c - 1].rc_data +
		    rm->rm_col[c - 1].rc_size;

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
	}

	zio->io_vsd = rm;
	zio->io_vsd_free = vdev_raidz_map_free;
	return (rm);
}

static void
vdev_raidz_generate_parity_p(raidz_map_t *rm)
{
	uint64_t *p, *src, pcount, ccount, i;
	int c;

	pcount = rm->rm_col[VDEV_RAIDZ_P].rc_size / sizeof (src[0]);

	for (c = rm->rm_firstdatacol; c < rm->rm_cols; c++) {
		src = rm->rm_col[c].rc_data;
		p = rm->rm_col[VDEV_RAIDZ_P].rc_data;
		ccount = rm->rm_col[c].rc_size / sizeof (src[0]);

		if (c == rm->rm_firstdatacol) {
			ASSERT(ccount == pcount);
			for (i = 0; i < ccount; i++, p++, src++) {
				*p = *src;
			}
		} else {
			ASSERT(ccount <= pcount);
			for (i = 0; i < ccount; i++, p++, src++) {
				*p ^= *src;
			}
		}
	}
}

static void
vdev_raidz_generate_parity_pq(raidz_map_t *rm)
{
	uint64_t *q, *p, *src, pcount, ccount, mask, i;
	int c;

	pcount = rm->rm_col[VDEV_RAIDZ_P].rc_size / sizeof (src[0]);
	ASSERT(rm->rm_col[VDEV_RAIDZ_P].rc_size ==
	    rm->rm_col[VDEV_RAIDZ_Q].rc_size);

	for (c = rm->rm_firstdatacol; c < rm->rm_cols; c++) {
		src = rm->rm_col[c].rc_data;
		p = rm->rm_col[VDEV_RAIDZ_P].rc_data;
		q = rm->rm_col[VDEV_RAIDZ_Q].rc_data;
		ccount = rm->rm_col[c].rc_size / sizeof (src[0]);

		if (c == rm->rm_firstdatacol) {
			ASSERT(ccount == pcount || ccount == 0);
			for (i = 0; i < ccount; i++, p++, q++, src++) {
				*q = *src;
				*p = *src;
			}
			for (; i < pcount; i++, p++, q++, src++) {
				*q = 0;
				*p = 0;
			}
		} else {
			ASSERT(ccount <= pcount);

			/*
			 * Rather than multiplying each byte individually (as
			 * described above), we are able to handle 8 at once
			 * by generating a mask based on the high bit in each
			 * byte and using that to conditionally XOR in 0x1d.
			 */
			for (i = 0; i < ccount; i++, p++, q++, src++) {
				mask = *q & 0x8080808080808080ULL;
				mask = (mask << 1) - (mask >> 7);
				*q = ((*q << 1) & 0xfefefefefefefefeULL) ^
				    (mask & 0x1d1d1d1d1d1d1d1dULL);
				*q ^= *src;
				*p ^= *src;
			}

			/*
			 * Treat short columns as though they are full of 0s.
			 */
			for (; i < pcount; i++, q++) {
				mask = *q & 0x8080808080808080ULL;
				mask = (mask << 1) - (mask >> 7);
				*q = ((*q << 1) & 0xfefefefefefefefeULL) ^
				    (mask & 0x1d1d1d1d1d1d1d1dULL);
			}
		}
	}
}

static void
vdev_raidz_reconstruct_p(raidz_map_t *rm, int x)
{
	uint64_t *dst, *src, xcount, ccount, count, i;
	int c;

	xcount = rm->rm_col[x].rc_size / sizeof (src[0]);
	ASSERT(xcount <= rm->rm_col[VDEV_RAIDZ_P].rc_size / sizeof (src[0]));
	ASSERT(xcount > 0);

	src = rm->rm_col[VDEV_RAIDZ_P].rc_data;
	dst = rm->rm_col[x].rc_data;
	for (i = 0; i < xcount; i++, dst++, src++) {
		*dst = *src;
	}

	for (c = rm->rm_firstdatacol; c < rm->rm_cols; c++) {
		src = rm->rm_col[c].rc_data;
		dst = rm->rm_col[x].rc_data;

		if (c == x)
			continue;

		ccount = rm->rm_col[c].rc_size / sizeof (src[0]);
		count = MIN(ccount, xcount);

		for (i = 0; i < count; i++, dst++, src++) {
			*dst ^= *src;
		}
	}
}

static void
vdev_raidz_reconstruct_q(raidz_map_t *rm, int x)
{
	uint64_t *dst, *src, xcount, ccount, count, mask, i;
	uint8_t *b;
	int c, j, exp;

	xcount = rm->rm_col[x].rc_size / sizeof (src[0]);
	ASSERT(xcount <= rm->rm_col[VDEV_RAIDZ_Q].rc_size / sizeof (src[0]));

	for (c = rm->rm_firstdatacol; c < rm->rm_cols; c++) {
		src = rm->rm_col[c].rc_data;
		dst = rm->rm_col[x].rc_data;

		if (c == x)
			ccount = 0;
		else
			ccount = rm->rm_col[c].rc_size / sizeof (src[0]);

		count = MIN(ccount, xcount);

		if (c == rm->rm_firstdatacol) {
			for (i = 0; i < count; i++, dst++, src++) {
				*dst = *src;
			}
			for (; i < xcount; i++, dst++) {
				*dst = 0;
			}

		} else {
			/*
			 * For an explanation of this, see the comment in
			 * vdev_raidz_generate_parity_pq() above.
			 */
			for (i = 0; i < count; i++, dst++, src++) {
				mask = *dst & 0x8080808080808080ULL;
				mask = (mask << 1) - (mask >> 7);
				*dst = ((*dst << 1) & 0xfefefefefefefefeULL) ^
				    (mask & 0x1d1d1d1d1d1d1d1dULL);
				*dst ^= *src;
			}

			for (; i < xcount; i++, dst++) {
				mask = *dst & 0x8080808080808080ULL;
				mask = (mask << 1) - (mask >> 7);
				*dst = ((*dst << 1) & 0xfefefefefefefefeULL) ^
				    (mask & 0x1d1d1d1d1d1d1d1dULL);
			}
		}
	}

	src = rm->rm_col[VDEV_RAIDZ_Q].rc_data;
	dst = rm->rm_col[x].rc_data;
	exp = 255 - (rm->rm_cols - 1 - x);

	for (i = 0; i < xcount; i++, dst++, src++) {
		*dst ^= *src;
		for (j = 0, b = (uint8_t *)dst; j < 8; j++, b++) {
			*b = vdev_raidz_exp2(*b, exp);
		}
	}
}

static void
vdev_raidz_reconstruct_pq(raidz_map_t *rm, int x, int y)
{
	uint8_t *p, *q, *pxy, *qxy, *xd, *yd, tmp, a, b, aexp, bexp;
	void *pdata, *qdata;
	uint64_t xsize, ysize, i;

	ASSERT(x < y);
	ASSERT(x >= rm->rm_firstdatacol);
	ASSERT(y < rm->rm_cols);

	ASSERT(rm->rm_col[x].rc_size >= rm->rm_col[y].rc_size);

	/*
	 * Move the parity data aside -- we're going to compute parity as
	 * though columns x and y were full of zeros -- Pxy and Qxy. We want to
	 * reuse the parity generation mechanism without trashing the actual
	 * parity so we make those columns appear to be full of zeros by
	 * setting their lengths to zero.
	 */
	pdata = rm->rm_col[VDEV_RAIDZ_P].rc_data;
	qdata = rm->rm_col[VDEV_RAIDZ_Q].rc_data;
	xsize = rm->rm_col[x].rc_size;
	ysize = rm->rm_col[y].rc_size;

	rm->rm_col[VDEV_RAIDZ_P].rc_data =
	    zio_buf_alloc(rm->rm_col[VDEV_RAIDZ_P].rc_size);
	rm->rm_col[VDEV_RAIDZ_Q].rc_data =
	    zio_buf_alloc(rm->rm_col[VDEV_RAIDZ_Q].rc_size);
	rm->rm_col[x].rc_size = 0;
	rm->rm_col[y].rc_size = 0;

	vdev_raidz_generate_parity_pq(rm);

	rm->rm_col[x].rc_size = xsize;
	rm->rm_col[y].rc_size = ysize;

	p = pdata;
	q = qdata;
	pxy = rm->rm_col[VDEV_RAIDZ_P].rc_data;
	qxy = rm->rm_col[VDEV_RAIDZ_Q].rc_data;
	xd = rm->rm_col[x].rc_data;
	yd = rm->rm_col[y].rc_data;

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
	b = vdev_raidz_pow2[255 - (rm->rm_cols - 1 - x)];
	tmp = 255 - vdev_raidz_log2[a ^ 1];

	aexp = vdev_raidz_log2[vdev_raidz_exp2(a, tmp)];
	bexp = vdev_raidz_log2[vdev_raidz_exp2(b, tmp)];

	for (i = 0; i < xsize; i++, p++, q++, pxy++, qxy++, xd++, yd++) {
		*xd = vdev_raidz_exp2(*p ^ *pxy, aexp) ^
		    vdev_raidz_exp2(*q ^ *qxy, bexp);

		if (i < ysize)
			*yd = *p ^ *pxy ^ *xd;
	}

	zio_buf_free(rm->rm_col[VDEV_RAIDZ_P].rc_data,
	    rm->rm_col[VDEV_RAIDZ_P].rc_size);
	zio_buf_free(rm->rm_col[VDEV_RAIDZ_Q].rc_data,
	    rm->rm_col[VDEV_RAIDZ_Q].rc_size);

	/*
	 * Restore the saved parity data.
	 */
	rm->rm_col[VDEV_RAIDZ_P].rc_data = pdata;
	rm->rm_col[VDEV_RAIDZ_Q].rc_data = qdata;
}


static int
vdev_raidz_open(vdev_t *vd, uint64_t *asize, uint64_t *ashift)
{
	vdev_t *cvd;
	uint64_t nparity = vd->vdev_nparity;
	int c, error;
	int lasterror = 0;
	int numerrors = 0;

	ASSERT(nparity > 0);

	if (nparity > VDEV_RAIDZ_MAXPARITY ||
	    vd->vdev_children < nparity + 1) {
		vd->vdev_stat.vs_aux = VDEV_AUX_BAD_LABEL;
		return (EINVAL);
	}

	for (c = 0; c < vd->vdev_children; c++) {
		cvd = vd->vdev_child[c];

		if ((error = vdev_open(cvd)) != 0) {
			lasterror = error;
			numerrors++;
			continue;
		}

		*asize = MIN(*asize - 1, cvd->vdev_asize - 1) + 1;
		*ashift = MAX(*ashift, cvd->vdev_ashift);
	}

	*asize *= vd->vdev_children;

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

static int
vdev_raidz_io_start(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;
	vdev_t *tvd = vd->vdev_top;
	vdev_t *cvd;
	blkptr_t *bp = zio->io_bp;
	raidz_map_t *rm;
	raidz_col_t *rc;
	int c;

	rm = vdev_raidz_map_alloc(zio, tvd->vdev_ashift, vd->vdev_children,
	    vd->vdev_nparity);

	ASSERT3U(rm->rm_asize, ==, vdev_psize_to_asize(vd, zio->io_size));

	if (zio->io_type == ZIO_TYPE_WRITE) {
		/*
		 * Generate RAID parity in the first virtual columns.
		 */
		if (rm->rm_firstdatacol == 1)
			vdev_raidz_generate_parity_p(rm);
		else
			vdev_raidz_generate_parity_pq(rm);

		for (c = 0; c < rm->rm_cols; c++) {
			rc = &rm->rm_col[c];
			cvd = vd->vdev_child[rc->rc_devidx];
			zio_nowait(zio_vdev_child_io(zio, NULL, cvd,
			    rc->rc_offset, rc->rc_data, rc->rc_size,
			    zio->io_type, zio->io_priority, 0,
			    vdev_raidz_child_done, rc));
		}

		return (ZIO_PIPELINE_CONTINUE);
	}

	ASSERT(zio->io_type == ZIO_TYPE_READ);

	/*
	 * Iterate over the columns in reverse order so that we hit the parity
	 * last -- any errors along the way will force us to read the parity
	 * data.
	 */
	for (c = rm->rm_cols - 1; c >= 0; c--) {
		rc = &rm->rm_col[c];
		cvd = vd->vdev_child[rc->rc_devidx];
		if (!vdev_readable(cvd)) {
			if (c >= rm->rm_firstdatacol)
				rm->rm_missingdata++;
			else
				rm->rm_missingparity++;
			rc->rc_error = ENXIO;
			rc->rc_tried = 1;	/* don't even try */
			rc->rc_skipped = 1;
			continue;
		}
		if (vdev_dtl_contains(cvd, DTL_MISSING, bp->blk_birth, 1)) {
			if (c >= rm->rm_firstdatacol)
				rm->rm_missingdata++;
			else
				rm->rm_missingparity++;
			rc->rc_error = ESTALE;
			rc->rc_skipped = 1;
			continue;
		}
		if (c >= rm->rm_firstdatacol || rm->rm_missingdata > 0 ||
		    (zio->io_flags & (ZIO_FLAG_SCRUB | ZIO_FLAG_RESILVER))) {
			zio_nowait(zio_vdev_child_io(zio, NULL, cvd,
			    rc->rc_offset, rc->rc_data, rc->rc_size,
			    zio->io_type, zio->io_priority, 0,
			    vdev_raidz_child_done, rc));
		}
	}

	return (ZIO_PIPELINE_CONTINUE);
}

/*
 * Report a checksum error for a child of a RAID-Z device.
 */
static void
raidz_checksum_error(zio_t *zio, raidz_col_t *rc)
{
	vdev_t *vd = zio->io_vd->vdev_child[rc->rc_devidx];

	if (!(zio->io_flags & ZIO_FLAG_SPECULATIVE)) {
		mutex_enter(&vd->vdev_stat_lock);
		vd->vdev_stat.vs_checksum_errors++;
		mutex_exit(&vd->vdev_stat_lock);
	}

	if (!(zio->io_flags & ZIO_FLAG_SPECULATIVE))
		zfs_ereport_post(FM_EREPORT_ZFS_CHECKSUM,
		    zio->io_spa, vd, zio, rc->rc_offset, rc->rc_size);
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
	void *orig[VDEV_RAIDZ_MAXPARITY];
	int c, ret = 0;
	raidz_col_t *rc;

	for (c = 0; c < rm->rm_firstdatacol; c++) {
		rc = &rm->rm_col[c];
		if (!rc->rc_tried || rc->rc_error != 0)
			continue;
		orig[c] = zio_buf_alloc(rc->rc_size);
		bcopy(rc->rc_data, orig[c], rc->rc_size);
	}

	if (rm->rm_firstdatacol == 1)
		vdev_raidz_generate_parity_p(rm);
	else
		vdev_raidz_generate_parity_pq(rm);

	for (c = 0; c < rm->rm_firstdatacol; c++) {
		rc = &rm->rm_col[c];
		if (!rc->rc_tried || rc->rc_error != 0)
			continue;
		if (bcmp(orig[c], rc->rc_data, rc->rc_size) != 0) {
			raidz_checksum_error(zio, rc);
			rc->rc_error = ECKSUM;
			ret++;
		}
		zio_buf_free(orig[c], rc->rc_size);
	}

	return (ret);
}

static uint64_t raidz_corrected_p;
static uint64_t raidz_corrected_q;
static uint64_t raidz_corrected_pq;

static int
vdev_raidz_worst_error(raidz_map_t *rm)
{
	int error = 0;

	for (int c = 0; c < rm->rm_cols; c++)
		error = zio_worst_error(error, rm->rm_col[c].rc_error);

	return (error);
}

static void
vdev_raidz_io_done(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;
	vdev_t *cvd;
	raidz_map_t *rm = zio->io_vsd;
	raidz_col_t *rc, *rc1;
	int unexpected_errors = 0;
	int parity_errors = 0;
	int parity_untried = 0;
	int data_errors = 0;
	int total_errors = 0;
	int n, c, c1;

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
		switch (data_errors) {
		case 0:
			if (zio_checksum_error(zio) == 0) {
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
			break;

		case 1:
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
			 * Find the column that reported the error.
			 */
			for (c = rm->rm_firstdatacol; c < rm->rm_cols; c++) {
				rc = &rm->rm_col[c];
				if (rc->rc_error != 0)
					break;
			}
			ASSERT(c != rm->rm_cols);
			ASSERT(!rc->rc_skipped || rc->rc_error == ENXIO ||
			    rc->rc_error == ESTALE);

			if (rm->rm_col[VDEV_RAIDZ_P].rc_error == 0) {
				vdev_raidz_reconstruct_p(rm, c);
			} else {
				ASSERT(rm->rm_firstdatacol > 1);
				vdev_raidz_reconstruct_q(rm, c);
			}

			if (zio_checksum_error(zio) == 0) {
				if (rm->rm_col[VDEV_RAIDZ_P].rc_error == 0)
					atomic_inc_64(&raidz_corrected_p);
				else
					atomic_inc_64(&raidz_corrected_q);

				/*
				 * If there's more than one parity disk that
				 * was successfully read, confirm that the
				 * other parity disk produced the correct data.
				 * This routine is suboptimal in that it
				 * regenerates both the parity we wish to test
				 * as well as the parity we just used to
				 * perform the reconstruction, but this should
				 * be a relatively uncommon case, and can be
				 * optimized if it becomes a problem.
				 * We also regenerate parity when resilvering
				 * so we can write it out to the failed device
				 * later.
				 */
				if (parity_errors < rm->rm_firstdatacol - 1 ||
				    (zio->io_flags & ZIO_FLAG_RESILVER)) {
					n = raidz_parity_verify(zio, rm);
					unexpected_errors += n;
					ASSERT(parity_errors + n <=
					    rm->rm_firstdatacol);
				}

				goto done;
			}
			break;

		case 2:
			/*
			 * Two data column errors require double parity.
			 */
			ASSERT(rm->rm_firstdatacol == 2);

			/*
			 * Find the two columns that reported errors.
			 */
			for (c = rm->rm_firstdatacol; c < rm->rm_cols; c++) {
				rc = &rm->rm_col[c];
				if (rc->rc_error != 0)
					break;
			}
			ASSERT(c != rm->rm_cols);
			ASSERT(!rc->rc_skipped || rc->rc_error == ENXIO ||
			    rc->rc_error == ESTALE);

			for (c1 = c++; c < rm->rm_cols; c++) {
				rc = &rm->rm_col[c];
				if (rc->rc_error != 0)
					break;
			}
			ASSERT(c != rm->rm_cols);
			ASSERT(!rc->rc_skipped || rc->rc_error == ENXIO ||
			    rc->rc_error == ESTALE);

			vdev_raidz_reconstruct_pq(rm, c1, c);

			if (zio_checksum_error(zio) == 0) {
				atomic_inc_64(&raidz_corrected_pq);
				goto done;
			}
			break;

		default:
			ASSERT(rm->rm_firstdatacol <= 2);
			ASSERT(0);
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
			    rc->rc_offset, rc->rc_data, rc->rc_size,
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
	 * in absent data. Before we attempt combinatorial reconstruction make
	 * sure we have a chance of coming up with the right answer.
	 */
	if (total_errors >= rm->rm_firstdatacol) {
		zio->io_error = vdev_raidz_worst_error(rm);
		/*
		 * If there were exactly as many device errors as parity
		 * columns, yet we couldn't reconstruct the data, then at
		 * least one device must have returned bad data silently.
		 */
		if (total_errors == rm->rm_firstdatacol)
			zio->io_error = zio_worst_error(zio->io_error, ECKSUM);
		goto done;
	}

	if (rm->rm_col[VDEV_RAIDZ_P].rc_error == 0) {
		/*
		 * Attempt to reconstruct the data from parity P.
		 */
		for (c = rm->rm_firstdatacol; c < rm->rm_cols; c++) {
			void *orig;
			rc = &rm->rm_col[c];

			orig = zio_buf_alloc(rc->rc_size);
			bcopy(rc->rc_data, orig, rc->rc_size);
			vdev_raidz_reconstruct_p(rm, c);

			if (zio_checksum_error(zio) == 0) {
				zio_buf_free(orig, rc->rc_size);
				atomic_inc_64(&raidz_corrected_p);

				/*
				 * If this child didn't know that it returned
				 * bad data, inform it.
				 */
				if (rc->rc_tried && rc->rc_error == 0)
					raidz_checksum_error(zio, rc);
				rc->rc_error = ECKSUM;
				goto done;
			}

			bcopy(orig, rc->rc_data, rc->rc_size);
			zio_buf_free(orig, rc->rc_size);
		}
	}

	if (rm->rm_firstdatacol > 1 && rm->rm_col[VDEV_RAIDZ_Q].rc_error == 0) {
		/*
		 * Attempt to reconstruct the data from parity Q.
		 */
		for (c = rm->rm_firstdatacol; c < rm->rm_cols; c++) {
			void *orig;
			rc = &rm->rm_col[c];

			orig = zio_buf_alloc(rc->rc_size);
			bcopy(rc->rc_data, orig, rc->rc_size);
			vdev_raidz_reconstruct_q(rm, c);

			if (zio_checksum_error(zio) == 0) {
				zio_buf_free(orig, rc->rc_size);
				atomic_inc_64(&raidz_corrected_q);

				/*
				 * If this child didn't know that it returned
				 * bad data, inform it.
				 */
				if (rc->rc_tried && rc->rc_error == 0)
					raidz_checksum_error(zio, rc);
				rc->rc_error = ECKSUM;
				goto done;
			}

			bcopy(orig, rc->rc_data, rc->rc_size);
			zio_buf_free(orig, rc->rc_size);
		}
	}

	if (rm->rm_firstdatacol > 1 &&
	    rm->rm_col[VDEV_RAIDZ_P].rc_error == 0 &&
	    rm->rm_col[VDEV_RAIDZ_Q].rc_error == 0) {
		/*
		 * Attempt to reconstruct the data from both P and Q.
		 */
		for (c = rm->rm_firstdatacol; c < rm->rm_cols - 1; c++) {
			void *orig, *orig1;
			rc = &rm->rm_col[c];

			orig = zio_buf_alloc(rc->rc_size);
			bcopy(rc->rc_data, orig, rc->rc_size);

			for (c1 = c + 1; c1 < rm->rm_cols; c1++) {
				rc1 = &rm->rm_col[c1];

				orig1 = zio_buf_alloc(rc1->rc_size);
				bcopy(rc1->rc_data, orig1, rc1->rc_size);

				vdev_raidz_reconstruct_pq(rm, c, c1);

				if (zio_checksum_error(zio) == 0) {
					zio_buf_free(orig, rc->rc_size);
					zio_buf_free(orig1, rc1->rc_size);
					atomic_inc_64(&raidz_corrected_pq);

					/*
					 * If these children didn't know they
					 * returned bad data, inform them.
					 */
					if (rc->rc_tried && rc->rc_error == 0)
						raidz_checksum_error(zio, rc);
					if (rc1->rc_tried && rc1->rc_error == 0)
						raidz_checksum_error(zio, rc1);

					rc->rc_error = ECKSUM;
					rc1->rc_error = ECKSUM;

					goto done;
				}

				bcopy(orig1, rc1->rc_data, rc1->rc_size);
				zio_buf_free(orig1, rc1->rc_size);
			}

			bcopy(orig, rc->rc_data, rc->rc_size);
			zio_buf_free(orig, rc->rc_size);
		}
	}

	/*
	 * All combinations failed to checksum. Generate checksum ereports for
	 * all children.
	 */
	zio->io_error = ECKSUM;

	if (!(zio->io_flags & ZIO_FLAG_SPECULATIVE)) {
		for (c = 0; c < rm->rm_cols; c++) {
			rc = &rm->rm_col[c];
			zfs_ereport_post(FM_EREPORT_ZFS_CHECKSUM,
			    zio->io_spa, vd->vdev_child[rc->rc_devidx], zio,
			    rc->rc_offset, rc->rc_size);
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
			    rc->rc_offset, rc->rc_data, rc->rc_size,
			    ZIO_TYPE_WRITE, zio->io_priority,
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
	VDEV_TYPE_RAIDZ,	/* name of this vdev type */
	B_FALSE			/* not a leaf vdev */
};
