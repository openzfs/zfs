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
 * Copyright (C) 2016 Gvozden Nešković. All rights reserved.
 */

#ifndef _VDEV_RAIDZ_MATH_IMPL_H
#define	_VDEV_RAIDZ_MATH_IMPL_H

#include <sys/types.h>


/*
 * Method for adding (XORing) two buffers.
 * Source and destination are XORed together and result is stored in
 * destination buffer. This method is used by multiple for gen/rec functions.
 *
 * @dc		Destination buffer
 * @sc		Source buffer
 * @dsize	Destination buffer size
 * @ssize	Source buffer size
 * @private	Unused
 */
static int
raidz_add_abd(void *dc, void *sc, uint64_t dsize, uint64_t ssize, void *private)
{
	v_t *dst = (v_t *) dc;
	const v_t *src = (v_t *) sc;
	const v_t * const src_end = src + (ssize / sizeof (v_t));

	ADD_DEFINE();

	(void) private;

	for (; src < src_end; src += ADD_STRIDE, dst += ADD_STRIDE) {
		LOAD(dst, ADD_D);
		XOR_ACC(src, ADD_D);
		STORE(dst, ADD_D);
	}
	return (0);
}


/*
 * Method for multiplying a buffer with a constant in GF(2^8).
 * Symbols from buffer are multiplied by a constant and result is stored
 * back in the same buffer.
 *
 * @dc		In/Out data buffer.
 * @size	Size of the buffer
 * @private	pointer to the multiplication constant (unsigned)
 */
static int
raidz_mul_abd(void *dc, uint64_t size, void *private)
{
	const unsigned mul = *((unsigned *) private);
	v_t *d = (v_t *) dc;
	v_t * const dend = d + (size / sizeof (v_t));

	MUL_DEFINE();

	for (; d < dend; d += MUL_STRIDE) {
		LOAD(d, MUL_D);
		MUL(mul, MUL_D);
		STORE(d, MUL_D);
	}
	return (0);
}


/*
 * Syndrome generation/update macros
 *
 * Require LOAD(), XOR(), STORE(), MUL2(), and MUL4() macros
 */
#define	P_D_SYNDROME(D, T, t)		\
{					\
	LOAD((t), T);			\
	XOR(D, T);			\
	STORE((t), T);			\
}

#define	Q_D_SYNDROME(D, T, t)		\
{					\
	LOAD((t), T);			\
	MUL2(T);			\
	XOR(D, T);			\
	STORE((t), T);			\
}

#define	Q_SYNDROME(T, t)		\
{					\
	LOAD((t), T);			\
	MUL2(T);			\
	STORE((t), T);			\
}

#define	R_D_SYNDROME(D, T, t)		\
{					\
	LOAD((t), T);			\
	MUL4(T);			\
	XOR(D, T);			\
	STORE((t), T);			\
}

#define	R_SYNDROME(T, t)		\
{					\
	LOAD((t), T);			\
	MUL4(T);			\
	STORE((t), T);			\
}


/*
 * PARITY CALCULATION
 *
 * Macros *_SYNDROME are used for parity/syndrome calculation.
 * *_D_SYNDROME() macros are used to calculate syndrome between 0 and
 * length of data column, and *_SYNDROME() macros are only for updating
 * the parity/syndrome if data column is shorter.
 *
 * P parity is calculated using raidz_add_abd().
 */

/*
 * Generate P parity (RAIDZ1)
 *
 * @rm	RAIDZ map
 */
static raidz_inline void
raidz_generate_p_impl(raidz_map_t * const rm)
{
	size_t c;
	const size_t firstdc = raidz_parity(rm);
	const size_t ncols = raidz_ncols(rm);
	const size_t psize = rm->rm_col[CODE_P].rc_size;
	abd_t *pabd = rm->rm_col[CODE_P].rc_data;
	size_t dsize;
	abd_t *dabd;

	ASSERT3U(psize, ==, rm->rm_col[firstdc].rc_size);
	ASSERT3U(psize % 512, ==, 0);

	/* start with first data column */
	abd_copy(pabd, rm->rm_col[firstdc].rc_data, psize);

	raidz_math_begin();

	for (c = firstdc+1; c < ncols; c++) {
		dabd = rm->rm_col[c].rc_data;
		dsize = rm->rm_col[c].rc_size;

		/* add data column */
		abd_iterate_func2(pabd, dabd, dsize, dsize,
			raidz_add_abd, NULL);
	}

	raidz_math_end();
}


/*
 * Generate PQ parity (RAIDZ2)
 * The function is called per data column.
 *
 * @c		array of pointers to parity (code) columns
 * @dc		pointer to data column
 * @csize	size of parity columns
 * @dsize	size of data column
 */
static void
raidz_gen_pq_add(void **c, const void *dc, const size_t csize,
	const size_t dsize)
{
	v_t *p = (v_t *) c[0];
	v_t *q = (v_t *) c[1];
	const v_t *d = (v_t *) dc;
	const v_t * const dend = d + (dsize / sizeof (v_t));
	const v_t * const qend = q + (csize / sizeof (v_t));

	GEN_PQ_DEFINE();

	MUL2_SETUP();

	for (; d < dend; d += GEN_STRIDE, p += GEN_STRIDE, q += GEN_STRIDE) {
		LOAD(d, GEN_PQ_D);
		P_D_SYNDROME(GEN_PQ_D, GEN_PQ_C, p);
		Q_D_SYNDROME(GEN_PQ_D, GEN_PQ_C, q);
	}
	for (; q < qend; q += GEN_STRIDE) {
		Q_SYNDROME(GEN_PQ_C, q);
	}
}


/*
 * Generate PQ parity (RAIDZ2)
 *
 * @rm	RAIDZ map
 */
static raidz_inline void
raidz_generate_pq_impl(raidz_map_t * const rm)
{
	size_t c;
	const size_t firstdc = raidz_parity(rm);
	const size_t ncols = raidz_ncols(rm);
	const size_t csize = rm->rm_col[CODE_P].rc_size;
	size_t dsize;
	abd_t *dabd;
	abd_t *cabds[] = {
		rm->rm_col[CODE_P].rc_data,
		rm->rm_col[CODE_Q].rc_data
	};

	ASSERT3U(csize % 512, ==, 0);

	abd_copy(cabds[CODE_P], rm->rm_col[firstdc].rc_data, csize);
	abd_copy(cabds[CODE_Q], rm->rm_col[firstdc].rc_data, csize);

	raidz_math_begin();

	for (c = firstdc+1; c < ncols; c++) {
		dabd = rm->rm_col[c].rc_data;
		dsize = rm->rm_col[c].rc_size;

		ASSERT3U(dsize % 512, ==, 0);

		abd_raidz_gen_iterate(cabds, dabd, csize, dsize, 2,
			raidz_gen_pq_add);
	}

	raidz_math_end();
}


/*
 * Generate PQR parity (RAIDZ3)
 * The function is called per data column.
 *
 * @c		array of pointers to parity (code) columns
 * @dc		pointer to data column
 * @csize	size of parity columns
 * @dsize	size of data column
 */
static void
raidz_gen_pqr_add(void **c, const void *dc, const size_t csize,
	const size_t dsize)
{
	v_t *p = (v_t *) c[0];
	v_t *q = (v_t *) c[1];
	v_t *r = (v_t *) c[CODE_R];
	const v_t *d = (v_t *) dc;
	const v_t * const dend = d + (dsize / sizeof (v_t));
	const v_t * const qend = q + (csize / sizeof (v_t));

	GEN_PQR_DEFINE();

	MUL2_SETUP();

	for (; d < dend; d += GEN_STRIDE, p += GEN_STRIDE, q += GEN_STRIDE,
	    r += GEN_STRIDE) {
		LOAD(d, GEN_PQR_D);
		P_D_SYNDROME(GEN_PQR_D, GEN_PQR_C, p);
		Q_D_SYNDROME(GEN_PQR_D, GEN_PQR_C, q);
		R_D_SYNDROME(GEN_PQR_D, GEN_PQR_C, r);
	}
	for (; q < qend; q += GEN_STRIDE, r += GEN_STRIDE) {
		Q_SYNDROME(GEN_PQR_C, q);
		R_SYNDROME(GEN_PQR_C, r);
	}
}


/*
 * Generate PQR parity (RAIDZ2)
 *
 * @rm	RAIDZ map
 */
static raidz_inline void
raidz_generate_pqr_impl(raidz_map_t * const rm)
{
	size_t c;
	const size_t firstdc = raidz_parity(rm);
	const size_t ncols = raidz_ncols(rm);
	const size_t csize = rm->rm_col[CODE_P].rc_size;
	size_t dsize;
	abd_t *dabd;
	abd_t *cabds[] = {
		rm->rm_col[CODE_P].rc_data,
		rm->rm_col[CODE_Q].rc_data,
		rm->rm_col[CODE_R].rc_data
	};

	abd_copy(cabds[CODE_P], rm->rm_col[firstdc].rc_data, csize);
	abd_copy(cabds[CODE_Q], rm->rm_col[firstdc].rc_data, csize);
	abd_copy(cabds[CODE_R], rm->rm_col[firstdc].rc_data, csize);

	raidz_math_begin();

	for (c = firstdc+1; c < ncols; c++) {
		dabd = rm->rm_col[c].rc_data;
		dsize = rm->rm_col[c].rc_size;

		ASSERT3U(dsize % 512, ==, 0);

		abd_raidz_gen_iterate(cabds, dabd, csize, dsize, 3,
			raidz_gen_pqr_add);
	}

	raidz_math_end();
}


/*
 * DATA RECONSTRUCTION
 *
 * Data reconstruction process consists of two phases:
 * 	- Syndrome calculation
 * 	- Data reconstruction
 *
 * Syndrome is the process of calculating parity with missing data columns set
 * to zero. Syndrome symbols are stored in missing data columns to avoid
 * additional buffer allocations and improve cache locality. Calculated
 * syndromes must correspond to parity used in reconstruction.
 *
 * Calculated syndrome symbols, together with parity symbols, are used to
 * reconstruct the missing data symbols:
 * 	P = Psyn + Dx + Dy + Dz
 * 	Q = Qsyn + 2^x * Dx + 2^y * Dy + 2^z * Dz
 * 	R = Rsyn + 4^x * Dx + 4^y * Dy + 4^z * Dz
 *
 * For the data reconstruction phase, the corresponding equations are solved
 * for Dx, Dy, Dz (missing data). This generally involves multiplying known
 * symbols and adding together. The multiplication constants are calculated
 * ahead of the operation. For single missing data methods function
 * raidz_mul_abd() is used since only single multiplication is required.
 */


/*
 * Reconstruct single data column using P parity
 *
 * @syn_method	raidz_add_abd()
 * @rec_method	not applicable
 *
 * @rm		RAIDZ map
 * @tgtidx	array of missing data indexes
 */
static raidz_inline int
raidz_reconstruct_p_impl(raidz_map_t *rm, const int *tgtidx)
{
	size_t c;
	const size_t firstdc = raidz_parity(rm);
	const size_t ncols = raidz_ncols(rm);
	const size_t x = tgtidx[0];
	const size_t xsize = rm->rm_col[x].rc_size;
	abd_t *xabd = rm->rm_col[x].rc_data;
	size_t size;
	abd_t *dabd;

	/* copy P into target */
	abd_copy(xabd, rm->rm_col[CODE_P].rc_data, xsize);

	raidz_math_begin();

	/* generate p_syndrome */
	for (c = firstdc; c < ncols; c++) {
		if (c == x)
			continue;

		dabd = rm->rm_col[c].rc_data;
		size = MIN(rm->rm_col[c].rc_size, xsize);

		ASSERT3U(size % 512, ==, 0);

		abd_iterate_func2(xabd, dabd, size, size, raidz_add_abd, NULL);
	}
	raidz_math_end();

	return (1 << 0);
}


/*
 * Generate Q syndrome (Qsyn)
 *
 * @xc		array of pointers to syndrome columns
 * @dc		data column (NULL if missing)
 * @xsize	size of syndrome columns
 * @dsize	size of data column (0 if missing)
 */
static void
raidz_syn_q_abd(void **xc, const void *dc, const size_t xsize,
	const size_t dsize)
{
	v_t *x = (v_t *) xc[0];
	const v_t *d = (v_t *) dc;
	const v_t * const dend = d + (dsize / sizeof (v_t));
	const v_t * const xend = x + (xsize / sizeof (v_t));

	SYN_Q_DEFINE();

	MUL2_SETUP();

	for (; d < dend; d += SYN_STRIDE, x += SYN_STRIDE) {
		LOAD(d, SYN_Q_D);
		Q_D_SYNDROME(SYN_Q_D, SYN_Q_X, x);
	}
	for (; x < xend; x += SYN_STRIDE) {
		Q_SYNDROME(SYN_Q_X, x);
	}
}


/*
 * Reconstruct single data column using Q parity
 *
 * @syn_method	raidz_add_abd()
 * @rec_method	raidz_mul_abd()
 *
 * @rm		RAIDZ map
 * @tgtidx	array of missing data indexes
 */
static raidz_inline int
raidz_reconstruct_q_impl(raidz_map_t *rm, const int *tgtidx)
{
	size_t c;
	size_t dsize;
	abd_t *dabd;
	const size_t firstdc = raidz_parity(rm);
	const size_t ncols = raidz_ncols(rm);
	const size_t x = tgtidx[0];
	abd_t *xabd = rm->rm_col[x].rc_data;
	const size_t xsize = rm->rm_col[x].rc_size;
	abd_t *tabds[] = { xabd };
	const unsigned mul[] = {
	[MUL_Q_X] = fix_mul_exp(255 - (ncols - x - 1))
	};

	/* Start with first data column if present */
	if (firstdc != x) {
		ASSERT3U(xsize, <=, rm->rm_col[firstdc].rc_size);
		abd_copy(xabd, rm->rm_col[firstdc].rc_data, xsize);
	} else {
		abd_zero(xabd, xsize);
	}

	raidz_math_begin();

	/* generate q_syndrome */
	for (c = firstdc+1; c < ncols; c++) {
		if (c == x) {
			dabd = NULL;
			dsize = 0;
		} else {
			dabd = rm->rm_col[c].rc_data;
			dsize = rm->rm_col[c].rc_size;
		}

		ASSERT3U(dsize % 512, ==, 0);

		abd_raidz_gen_iterate(tabds, dabd, xsize, dsize, 1,
			raidz_syn_q_abd);
	}

	/* add Q to the syndrome */
	abd_iterate_func2(xabd, rm->rm_col[CODE_Q].rc_data, xsize, xsize,
		raidz_add_abd, NULL);

	/* transform the syndrome */
	abd_iterate_wfunc(xabd, xsize, raidz_mul_abd, (void*)mul);

	raidz_math_end();

	return (1 << 1);
}


/*
 * Generate R syndrome (Rsyn)
 *
 * @xc		array of pointers to syndrome columns
 * @dc		data column (NULL if missing)
 * @tsize	size of syndrome columns
 * @dsize	size of data column (0 if missing)
 */
static void
raidz_syn_r_abd(void **xc, const void *dc, const size_t tsize,
	const size_t dsize)
{
	v_t *x = (v_t *) xc[0];
	const v_t *d = (v_t *) dc;
	const v_t * const dend = d + (dsize / sizeof (v_t));
	const v_t * const xend = x + (tsize / sizeof (v_t));

	SYN_R_DEFINE();

	MUL2_SETUP();

	for (; d < dend; d += SYN_STRIDE, x += SYN_STRIDE) {
		LOAD(d, SYN_R_D);
		R_D_SYNDROME(SYN_R_D, SYN_R_X, x);
	}
	for (; x < xend; x += SYN_STRIDE) {
		R_SYNDROME(SYN_R_X, x);
	}
}


/*
 * Reconstruct single data column using R parity
 *
 * @syn_method	raidz_add_abd()
 * @rec_method	raidz_mul_abd()
 *
 * @rm		RAIDZ map
 * @tgtidx	array of missing data indexes
 */
static raidz_inline int
raidz_reconstruct_r_impl(raidz_map_t *rm, const int *tgtidx)
{
	size_t c;
	size_t dsize;
	abd_t *dabd;
	const size_t firstdc = raidz_parity(rm);
	const size_t ncols = raidz_ncols(rm);
	const size_t x = tgtidx[0];
	const size_t xsize = rm->rm_col[x].rc_size;
	abd_t *xabd = rm->rm_col[x].rc_data;
	abd_t *tabds[] = { xabd };
	const unsigned mul[] = {
	[MUL_R_X] = fix_mul_exp(255 - 2 * (ncols - x - 1))
	};

	/* Start with first data column if present */
	if (firstdc != x) {
		ASSERT3U(xsize, <=, rm->rm_col[firstdc].rc_size);
		abd_copy(xabd, rm->rm_col[firstdc].rc_data, xsize);
	} else {
		abd_zero(xabd, xsize);
	}

	raidz_math_begin();

	/* generate q_syndrome */
	for (c = firstdc+1; c < ncols; c++) {
		if (c == x) {
			dabd = NULL;
			dsize = 0;
		} else {
			dabd = rm->rm_col[c].rc_data;
			dsize = rm->rm_col[c].rc_size;
		}
		ASSERT3U(dsize % 512, ==, 0);

		abd_raidz_gen_iterate(tabds, dabd, xsize, dsize, 1,
			raidz_syn_r_abd);
	}

	/* add R to the syndrome */
	abd_iterate_func2(xabd, rm->rm_col[CODE_R].rc_data, xsize, xsize,
		raidz_add_abd, NULL);

	/* transform the syndrome */
	abd_iterate_wfunc(xabd, xsize, raidz_mul_abd, (void *)mul);

	raidz_math_end();

	return (1 << 2);
}


/*
 * Generate P and Q syndromes
 *
 * @xc		array of pointers to syndrome columns
 * @dc		data column (NULL if missing)
 * @tsize	size of syndrome columns
 * @dsize	size of data column (0 if missing)
 */
static void
raidz_syn_pq_abd(void **tc, const void *dc, const size_t tsize,
	const size_t dsize)
{
	v_t *x = (v_t *) tc[0];
	v_t *y = (v_t *) tc[1];
	const v_t *d = (v_t *) dc;
	const v_t * const dend = d + (dsize / sizeof (v_t));
	const v_t * const yend = y + (tsize / sizeof (v_t));

	SYN_PQ_DEFINE();

	MUL2_SETUP();

	for (; d < dend; d += SYN_STRIDE, x += SYN_STRIDE, y += SYN_STRIDE) {
		LOAD(d, SYN_PQ_D);
		P_D_SYNDROME(SYN_PQ_D, SYN_PQ_X, x);
		Q_D_SYNDROME(SYN_PQ_D, SYN_PQ_X, y);
	}
	for (; y < yend; y += SYN_STRIDE) {
		Q_SYNDROME(SYN_PQ_X, y);
	}
}

/*
 * Reconstruct data using PQ parity and PQ syndromes
 *
 * @tc		syndrome/result columns
 * @tsize	size of syndrome/result columns
 * @c		parity columns
 * @mul		array of multiplication constants
 */
static void
raidz_rec_pq_abd(void **tc, const size_t tsize, void **c,
	const unsigned *mul)
{
	v_t *x = (v_t *) tc[0];
	v_t *y = (v_t *) tc[1];
	const v_t * const xend = x + (tsize / sizeof (v_t));
	const v_t *p = (v_t *) c[0];
	const v_t *q = (v_t *) c[1];

	REC_PQ_DEFINE();

	for (; x < xend; x += REC_STRIDE, y += REC_STRIDE, p += REC_STRIDE,
	    q += REC_STRIDE) {
		LOAD(x, REC_PQ_X);
		LOAD(y, REC_PQ_Y);

		XOR_ACC(p, REC_PQ_X);
		XOR_ACC(q, REC_PQ_Y);

		/* Save Pxy */
		COPY(REC_PQ_X,  REC_PQ_T);

		/* Calc X */
		MUL(mul[MUL_PQ_X], REC_PQ_X);
		MUL(mul[MUL_PQ_Y], REC_PQ_Y);
		XOR(REC_PQ_Y,  REC_PQ_X);
		STORE(x, REC_PQ_X);

		/* Calc Y */
		XOR(REC_PQ_T,  REC_PQ_X);
		STORE(y, REC_PQ_X);
	}
}


/*
 * Reconstruct two data columns using PQ parity
 *
 * @syn_method	raidz_syn_pq_abd()
 * @rec_method	raidz_rec_pq_abd()
 *
 * @rm		RAIDZ map
 * @tgtidx	array of missing data indexes
 */
static raidz_inline int
raidz_reconstruct_pq_impl(raidz_map_t *rm, const int *tgtidx)
{
	size_t c;
	size_t dsize;
	abd_t *dabd;
	const size_t firstdc = raidz_parity(rm);
	const size_t ncols = raidz_ncols(rm);
	const size_t x = tgtidx[0];
	const size_t y = tgtidx[1];
	const size_t xsize = rm->rm_col[x].rc_size;
	const size_t ysize = rm->rm_col[y].rc_size;
	abd_t *xabd = rm->rm_col[x].rc_data;
	abd_t *yabd = rm->rm_col[y].rc_data;
	abd_t *tabds[2] = { xabd, yabd };
	abd_t *cabds[] = {
		rm->rm_col[CODE_P].rc_data,
		rm->rm_col[CODE_Q].rc_data
	};
	const unsigned a = vdev_raidz_pow2[255 + x - y];
	const unsigned b = vdev_raidz_pow2[255 - (ncols - 1 - x)];
	const unsigned e = 255 - vdev_raidz_log2[a ^ 0x01];
	const unsigned mul[] = {
	[MUL_PQ_X] = fix_mul_exp(vdev_raidz_log2[vdev_raidz_exp2(a, e)]),
	[MUL_PQ_Y] = fix_mul_exp(vdev_raidz_log2[vdev_raidz_exp2(b, e)])
	};

	/*
	 * Check if some of targets is shorter then others
	 * In this case, shorter target needs to be replaced with
	 * new buffer so that syndrome can be calculated.
	 */
	if (ysize < xsize) {
		yabd = abd_alloc_scatter(xsize);
		VERIFY(yabd);
		tabds[1] = yabd;
	}

	/* Start with first data column if present */
	if (firstdc != x) {
		ASSERT3U(xsize, <=, rm->rm_col[firstdc].rc_size);
		abd_copy(xabd, rm->rm_col[firstdc].rc_data, xsize);
		abd_copy(yabd, rm->rm_col[firstdc].rc_data, xsize);
	} else {
		abd_zero(xabd, xsize);
		abd_zero(yabd, xsize);
	}

	raidz_math_begin();

	/* generate q_syndrome */
	for (c = firstdc+1; c < ncols; c++) {
		if (c == x || c == y) {
			dabd = NULL;
			dsize = 0;
		} else {
			dabd = rm->rm_col[c].rc_data;
			dsize = rm->rm_col[c].rc_size;
		}
		ASSERT3U(dsize % 512, ==, 0);

		abd_raidz_gen_iterate(tabds, dabd, xsize, dsize, 2,
			raidz_syn_pq_abd);
	}

	abd_raidz_rec_iterate(cabds, tabds, xsize, 2, raidz_rec_pq_abd, mul);

	raidz_math_end();

	/*
	 * Copy shorter targets back to the original abd buffer
	 */
	if (ysize < xsize) {
		abd_copy(rm->rm_col[y].rc_data, yabd, ysize);
		abd_free(yabd, xsize);
	}

	return ((1 << 0) | (1 << 1));
}


/*
 * Generate P and R syndromes
 *
 * @xc		array of pointers to syndrome columns
 * @dc		data column (NULL if missing)
 * @tsize	size of syndrome columns
 * @dsize	size of data column (0 if missing)
 */
static void
raidz_syn_pr_abd(void **c, const void *dc, const size_t tsize,
	const size_t dsize)
{
	v_t *x = (v_t *) c[0];
	v_t *y = (v_t *) c[1];
	const v_t *d = (v_t *) dc;
	const v_t * const dend = d + (dsize / sizeof (v_t));
	const v_t * const yend = y + (tsize / sizeof (v_t));

	SYN_PR_DEFINE();

	MUL2_SETUP();

	for (; d < dend; d += SYN_STRIDE, x += SYN_STRIDE, y += SYN_STRIDE) {
		LOAD(d, SYN_PR_D);
		P_D_SYNDROME(SYN_PR_D, SYN_PR_X, x);
		R_D_SYNDROME(SYN_PR_D, SYN_PR_X, y);
	}
	for (; y < yend; y += SYN_STRIDE) {
		R_SYNDROME(SYN_PR_X, y);
	}
}

/*
 * Reconstruct data using PR parity and PR syndromes
 *
 * @tc		syndrome/result columns
 * @tsize	size of syndrome/result columns
 * @c		parity columns
 * @mul		array of multiplication constants
 */
static void
raidz_rec_pr_abd(void **t, const size_t tsize, void **c,
	const unsigned *mul)
{
	v_t *x = (v_t *) t[0];
	v_t *y = (v_t *) t[1];
	const v_t * const xend = x + (tsize / sizeof (v_t));
	const v_t *p = (v_t *) c[0];
	const v_t *q = (v_t *) c[1];

	REC_PR_DEFINE();

	for (; x < xend; x += REC_STRIDE, y += REC_STRIDE, p += REC_STRIDE,
	    q += REC_STRIDE) {
		LOAD(x, REC_PR_X);
		LOAD(y, REC_PR_Y);
		XOR_ACC(p, REC_PR_X);
		XOR_ACC(q, REC_PR_Y);

		/* Save Pxy */
		COPY(REC_PR_X,  REC_PR_T);

		/* Calc X */
		MUL(mul[MUL_PR_X], REC_PR_X);
		MUL(mul[MUL_PR_Y], REC_PR_Y);
		XOR(REC_PR_Y,  REC_PR_X);
		STORE(x, REC_PR_X);

		/* Calc Y */
		XOR(REC_PR_T,  REC_PR_X);
		STORE(y, REC_PR_X);
	}
}


/*
 * Reconstruct two data columns using PR parity
 *
 * @syn_method	raidz_syn_pr_abd()
 * @rec_method	raidz_rec_pr_abd()
 *
 * @rm		RAIDZ map
 * @tgtidx	array of missing data indexes
 */
static raidz_inline int
raidz_reconstruct_pr_impl(raidz_map_t *rm, const int *tgtidx)
{
	size_t c;
	size_t dsize;
	abd_t *dabd;
	const size_t firstdc = raidz_parity(rm);
	const size_t ncols = raidz_ncols(rm);
	const size_t x = tgtidx[0];
	const size_t y = tgtidx[1];
	const size_t xsize = rm->rm_col[x].rc_size;
	const size_t ysize = rm->rm_col[y].rc_size;
	abd_t *xabd = rm->rm_col[x].rc_data;
	abd_t *yabd = rm->rm_col[y].rc_data;
	abd_t *tabds[2] = { xabd, yabd };
	abd_t *cabds[] = {
		rm->rm_col[CODE_P].rc_data,
		rm->rm_col[CODE_R].rc_data
	};
	const unsigned a = vdev_raidz_pow2[255 + 2 * x - 2 * y];
	const unsigned b = vdev_raidz_pow2[255 - 2 * (ncols - 1 - x)];
	const unsigned e = 255 - vdev_raidz_log2[a ^ 0x01];
	const unsigned mul[] = {
	[MUL_PR_X] = fix_mul_exp(vdev_raidz_log2[vdev_raidz_exp2(a, e)]),
	[MUL_PR_Y] = fix_mul_exp(vdev_raidz_log2[vdev_raidz_exp2(b, e)])
	};

	/*
	 * Check if some of targets is shorter then others
	 * In this case, shorter target needs to be replaced with
	 * new buffer so that syndrome can be calculated.
	 */
	if (ysize < xsize) {
		yabd = abd_alloc_scatter(xsize);
		VERIFY(yabd);
		tabds[1] = yabd;
	}

	/* Start with first data column if present */
	if (firstdc != x) {
		ASSERT3U(xsize, <=, rm->rm_col[firstdc].rc_size);
		abd_copy(xabd, rm->rm_col[firstdc].rc_data, xsize);
		abd_copy(yabd, rm->rm_col[firstdc].rc_data, xsize);
	} else {
		abd_zero(xabd, xsize);
		abd_zero(yabd, xsize);
	}

	raidz_math_begin();

	/* generate q_syndrome */
	for (c = firstdc+1; c < ncols; c++) {
		if (c == x || c == y) {
			dabd = NULL;
			dsize = 0;
		} else {
			dabd = rm->rm_col[c].rc_data;
			dsize = rm->rm_col[c].rc_size;
		}
		ASSERT3U(dsize % 512, ==, 0);

		abd_raidz_gen_iterate(tabds, dabd, xsize, dsize, 2,
			raidz_syn_pr_abd);
	}

	abd_raidz_rec_iterate(cabds, tabds, xsize, 2, raidz_rec_pr_abd, mul);

	raidz_math_end();

	/*
	 * Copy shorter targets back to the original abd buffer
	 */
	if (ysize < xsize) {
		abd_copy(rm->rm_col[y].rc_data, yabd, ysize);
		abd_free(yabd, xsize);
	}

	return ((1 << 0) | (1 << 2));
}


/*
 * Generate Q and R syndromes
 *
 * @xc		array of pointers to syndrome columns
 * @dc		data column (NULL if missing)
 * @tsize	size of syndrome columns
 * @dsize	size of data column (0 if missing)
 */
static void
raidz_syn_qr_abd(void **c, const void *dc, const size_t tsize,
	const size_t dsize)
{
	v_t *x = (v_t *) c[0];
	v_t *y = (v_t *) c[1];
	const v_t * const xend = x + (tsize / sizeof (v_t));
	const v_t *d = (v_t *) dc;
	const v_t * const dend = d + (dsize / sizeof (v_t));

	SYN_QR_DEFINE();

	MUL2_SETUP();

	for (; d < dend; d += SYN_STRIDE, x += SYN_STRIDE, y += SYN_STRIDE) {
		LOAD(d, SYN_PQ_D);
		Q_D_SYNDROME(SYN_QR_D, SYN_QR_X, x);
		R_D_SYNDROME(SYN_QR_D, SYN_QR_X, y);
	}
	for (; x < xend; x += SYN_STRIDE, y += SYN_STRIDE) {
		Q_SYNDROME(SYN_QR_X, x);
		R_SYNDROME(SYN_QR_X, y);
	}
}


/*
 * Reconstruct data using QR parity and QR syndromes
 *
 * @tc		syndrome/result columns
 * @tsize	size of syndrome/result columns
 * @c		parity columns
 * @mul		array of multiplication constants
 */
static void
raidz_rec_qr_abd(void **t, const size_t tsize, void **c,
	const unsigned *mul)
{
	v_t *x = (v_t *) t[0];
	v_t *y = (v_t *) t[1];
	const v_t * const xend = x + (tsize / sizeof (v_t));
	const v_t *p = (v_t *) c[0];
	const v_t *q = (v_t *) c[1];

	REC_QR_DEFINE();

	for (; x < xend; x += REC_STRIDE, y += REC_STRIDE, p += REC_STRIDE,
	    q += REC_STRIDE) {
		LOAD(x, REC_QR_X);
		LOAD(y, REC_QR_Y);

		XOR_ACC(p, REC_QR_X);
		XOR_ACC(q, REC_QR_Y);

		/* Save Pxy */
		COPY(REC_QR_X,  REC_QR_T);

		/* Calc X */
		MUL(mul[MUL_QR_XQ], REC_QR_X);	/* X = Q * xqm */
		XOR(REC_QR_Y, REC_QR_X);	/* X = R ^ X   */
		MUL(mul[MUL_QR_X], REC_QR_X);	/* X = X * xm  */
		STORE(x, REC_QR_X);

		/* Calc Y */
		MUL(mul[MUL_QR_YQ], REC_QR_T);	/* X = Q * xqm */
		XOR(REC_QR_Y, REC_QR_T);	/* X = R ^ X   */
		MUL(mul[MUL_QR_Y], REC_QR_T);	/* X = X * xm  */
		STORE(y, REC_QR_T);
	}
}


/*
 * Reconstruct two data columns using QR parity
 *
 * @syn_method	raidz_syn_qr_abd()
 * @rec_method	raidz_rec_qr_abd()
 *
 * @rm		RAIDZ map
 * @tgtidx	array of missing data indexes
 */
static raidz_inline int
raidz_reconstruct_qr_impl(raidz_map_t *rm, const int *tgtidx)
{
	size_t c;
	size_t dsize;
	abd_t *dabd;
	const size_t firstdc = raidz_parity(rm);
	const size_t ncols = raidz_ncols(rm);
	const size_t x = tgtidx[0];
	const size_t y = tgtidx[1];
	const size_t xsize = rm->rm_col[x].rc_size;
	const size_t ysize = rm->rm_col[y].rc_size;
	abd_t *xabd = rm->rm_col[x].rc_data;
	abd_t *yabd = rm->rm_col[y].rc_data;
	abd_t *tabds[2] = { xabd, yabd };
	abd_t *cabds[] = {
		rm->rm_col[CODE_Q].rc_data,
		rm->rm_col[CODE_R].rc_data
	};
	const unsigned denom = 255 - vdev_raidz_log2[
		vdev_raidz_pow2[3 * ncols - 3 - x - 2 * y] ^
		vdev_raidz_pow2[3 * ncols - 3 - 2 * x - y]
	];
	const unsigned mul[] = {
		[MUL_QR_XQ]	= fix_mul_exp(ncols - 1 - y),
		[MUL_QR_X]	= fix_mul_exp(ncols - 1 - y + denom),
		[MUL_QR_YQ]	= fix_mul_exp(ncols - 1 - x),
		[MUL_QR_Y]	= fix_mul_exp(ncols - 1 - x + denom)
	};

	/*
	 * Check if some of targets is shorter then others
	 * In this case, shorter target needs to be replaced with
	 * new buffer so that syndrome can be calculated.
	 */
	if (ysize < xsize) {
		yabd = abd_alloc_scatter(xsize);
		VERIFY(yabd);
		tabds[1] = yabd;
	}

	/* Start with first data column if present */
	if (firstdc != x) {
		ASSERT3U(xsize, <=, rm->rm_col[firstdc].rc_size);
		abd_copy(xabd, rm->rm_col[firstdc].rc_data, xsize);
		abd_copy(yabd, rm->rm_col[firstdc].rc_data, xsize);
	} else {
		abd_zero(xabd, xsize);
		abd_zero(yabd, xsize);
	}

	raidz_math_begin();

	/* generate q_syndrome */
	for (c = firstdc+1; c < ncols; c++) {
		if (c == x || c == y) {
			dabd = NULL;
			dsize = 0;
		} else {
			dabd = rm->rm_col[c].rc_data;
			dsize = rm->rm_col[c].rc_size;
		}
		ASSERT3U(dsize % 512, ==, 0);

		abd_raidz_gen_iterate(tabds, dabd, xsize, dsize, 2,
			raidz_syn_qr_abd);
	}

	abd_raidz_rec_iterate(cabds, tabds, xsize, 2, raidz_rec_qr_abd, mul);

	raidz_math_end();

	/*
	 * Copy shorter targets back to the original abd buffer
	 */
	if (ysize < xsize) {
		abd_copy(rm->rm_col[y].rc_data, yabd, ysize);
		abd_free(yabd, xsize);
	}

	return ((1 << 1) | (1 << 2));
}


/*
 * Generate P, Q, and R syndromes
 *
 * @xc		array of pointers to syndrome columns
 * @dc		data column (NULL if missing)
 * @tsize	size of syndrome columns
 * @dsize	size of data column (0 if missing)
 */
static void
raidz_syn_pqr_abd(void **c, const void *dc, const size_t tsize,
	const size_t dsize)
{
	v_t *x = (v_t *) c[0];
	v_t *y = (v_t *) c[1];
	v_t *z = (v_t *) c[2];
	const v_t * const yend = y + (tsize / sizeof (v_t));
	const v_t *d = (v_t *) dc;
	const v_t * const dend = d + (dsize / sizeof (v_t));

	SYN_PQR_DEFINE();

	MUL2_SETUP();

	for (; d < dend;  d += SYN_STRIDE, x += SYN_STRIDE, y += SYN_STRIDE,
	    z += SYN_STRIDE) {
		LOAD(d, SYN_PQR_D);
		P_D_SYNDROME(SYN_PQR_D, SYN_PQR_X, x)
		Q_D_SYNDROME(SYN_PQR_D, SYN_PQR_X, y);
		R_D_SYNDROME(SYN_PQR_D, SYN_PQR_X, z);
	}
	for (; y < yend; y += SYN_STRIDE, z += SYN_STRIDE) {
		Q_SYNDROME(SYN_PQR_X, y);
		R_SYNDROME(SYN_PQR_X, z);
	}
}


/*
 * Reconstruct data using PRQ parity and PQR syndromes
 *
 * @tc		syndrome/result columns
 * @tsize	size of syndrome/result columns
 * @c		parity columns
 * @mul		array of multiplication constants
 */
static void
raidz_rec_pqr_abd(void **t, const size_t tsize, void **c,
	const unsigned * const mul)
{
	v_t *x = (v_t *) t[0];
	v_t *y = (v_t *) t[1];
	v_t *z = (v_t *) t[2];
	const v_t * const xend = x + (tsize / sizeof (v_t));
	const v_t *p = (v_t *) c[0];
	const v_t *q = (v_t *) c[1];
	const v_t *r = (v_t *) c[CODE_R];

	REC_PQR_DEFINE();

	for (; x < xend; x += REC_STRIDE, y += REC_STRIDE, z += REC_STRIDE,
	    p += REC_STRIDE, q += REC_STRIDE, r += REC_STRIDE) {
		LOAD(x, REC_PQR_X);
		LOAD(y, REC_PQR_Y);
		LOAD(z, REC_PQR_Z);

		XOR_ACC(p, REC_PQR_X);
		XOR_ACC(q, REC_PQR_Y);
		XOR_ACC(r, REC_PQR_Z);

		/* Save Pxyz and Qxyz */
		COPY(REC_PQR_X, REC_PQR_XS);
		COPY(REC_PQR_Y, REC_PQR_YS);

		/* Calc X */
		MUL(mul[MUL_PQR_XP], REC_PQR_X);	/* Xp = Pxyz * xp   */
		MUL(mul[MUL_PQR_XQ], REC_PQR_Y);	/* Xq = Qxyz * xq   */
		XOR(REC_PQR_Y, REC_PQR_X);
		MUL(mul[MUL_PQR_XR], REC_PQR_Z);	/* Xr = Rxyz * xr   */
		XOR(REC_PQR_Z, REC_PQR_X);		/* X = Xp + Xq + Xr */
		STORE(x, REC_PQR_X);

		/* Calc Y */
		XOR(REC_PQR_X, REC_PQR_XS); 		/* Pyz = Pxyz + X */
		MUL(mul[MUL_PQR_YU], REC_PQR_X);  	/* Xq = X * upd_q */
		XOR(REC_PQR_X, REC_PQR_YS); 		/* Qyz = Qxyz + Xq */
		COPY(REC_PQR_XS, REC_PQR_X);		/* restore Pyz */
		MUL(mul[MUL_PQR_YP], REC_PQR_X);	/* Yp = Pyz * yp */
		MUL(mul[MUL_PQR_YQ], REC_PQR_YS);	/* Yq = Qyz * yq */
		XOR(REC_PQR_X, REC_PQR_YS); 		/* Y = Yp + Yq */
		STORE(y, REC_PQR_YS);

		/* Calc Z */
		XOR(REC_PQR_XS, REC_PQR_YS);		/* Z = Pz = Pyz + Y */
		STORE(z, REC_PQR_YS);
	}
}


/*
 * Reconstruct three data columns using PQR parity
 *
 * @syn_method	raidz_syn_pqr_abd()
 * @rec_method	raidz_rec_pqr_abd()
 *
 * @rm		RAIDZ map
 * @tgtidx	array of missing data indexes
 */
static raidz_inline int
raidz_reconstruct_pqr_impl(raidz_map_t *rm, const int *tgtidx)
{
	size_t c;
	size_t dsize;
	abd_t *dabd;
	const size_t firstdc = raidz_parity(rm);
	const size_t ncols = raidz_ncols(rm);
	const size_t x = tgtidx[0];
	const size_t y = tgtidx[1];
	const size_t z = tgtidx[2];
	const size_t xsize = rm->rm_col[x].rc_size;
	const size_t ysize = rm->rm_col[y].rc_size;
	const size_t zsize = rm->rm_col[z].rc_size;
	abd_t *xabd = rm->rm_col[x].rc_data;
	abd_t *yabd = rm->rm_col[y].rc_data;
	abd_t *zabd = rm->rm_col[z].rc_data;
	abd_t *tabds[] = { xabd, yabd, zabd };
	abd_t *cabds[] = {
		rm->rm_col[CODE_P].rc_data,
		rm->rm_col[CODE_Q].rc_data,
		rm->rm_col[CODE_R].rc_data
	};
	const unsigned x_d = 255 - vdev_raidz_log2[
		vdev_raidz_pow2[3 * ncols - 3 - 2 * x - y] ^
		vdev_raidz_pow2[3 * ncols - 3 - x - 2 * y] ^
		vdev_raidz_pow2[3 * ncols - 3 - 2 * x - z] ^
		vdev_raidz_pow2[3 * ncols - 3 - x - 2 * z] ^
		vdev_raidz_pow2[3 * ncols - 3 - 2 * y - z] ^
		vdev_raidz_pow2[3 * ncols - 3 - y - 2 * z]
	];
	const unsigned y_d = 255 - vdev_raidz_log2[
		vdev_raidz_pow2[ncols - 1 - y] ^
		vdev_raidz_pow2[ncols - 1 - z]
	];
	const unsigned mul[] = {
	[MUL_PQR_XP] = fix_mul_exp(vdev_raidz_log2[
			vdev_raidz_pow2[3 * ncols - 3 - 2 * y - z] ^
			vdev_raidz_pow2[3 * ncols - 3 - y - 2 * z]
		] + x_d),
	[MUL_PQR_XQ] = fix_mul_exp(vdev_raidz_log2[
			vdev_raidz_pow2[2 * ncols - 2 - 2 * y] ^
			vdev_raidz_pow2[2 * ncols - 2 - 2 * z]
		] + x_d),
	[MUL_PQR_XR] = fix_mul_exp(vdev_raidz_log2[
			vdev_raidz_pow2[ncols - 1 - y] ^
			vdev_raidz_pow2[ncols - 1 - z]
		] + x_d),
	[MUL_PQR_YU] = fix_mul_exp(ncols - 1 - x),
	[MUL_PQR_YP] = fix_mul_exp(ncols - 1 - z + y_d),
	[MUL_PQR_YQ] = fix_mul_exp(y_d)
	};

	/*
	 * Check if some of targets is shorter then others
	 * In this case, shorter target needs to be replaced with
	 * new buffer so that syndrome can be calculated.
	 */
	if (ysize < xsize) {
		yabd = abd_alloc_scatter(xsize);
		VERIFY(yabd);
		tabds[1] = yabd;
	}
	if (zsize < xsize) {
		zabd = abd_alloc_scatter(xsize);
		VERIFY(zabd);
		tabds[2] = zabd;
	}

	/* Start with first data column if present */
	if (firstdc != x) {
		ASSERT3U(xsize, <=, rm->rm_col[firstdc].rc_size);
		abd_copy(xabd, rm->rm_col[firstdc].rc_data, xsize);
		abd_copy(yabd, rm->rm_col[firstdc].rc_data, xsize);
		abd_copy(zabd, rm->rm_col[firstdc].rc_data, xsize);
	} else {
		abd_zero(xabd, xsize);
		abd_zero(yabd, xsize);
		abd_zero(zabd, xsize);
	}

	raidz_math_begin();

	/* generate q_syndrome */
	for (c = firstdc+1; c < ncols; c++) {
		if (c == x || c == y || c == z) {
			dabd = NULL;
			dsize = 0;
		} else {
			dabd = rm->rm_col[c].rc_data;
			dsize = rm->rm_col[c].rc_size;
		}
		ASSERT3U(dsize % 512, ==, 0);

		abd_raidz_gen_iterate(tabds, dabd, xsize, dsize, 3,
			raidz_syn_pqr_abd);
	}

	abd_raidz_rec_iterate(cabds, tabds, xsize, 3, raidz_rec_pqr_abd, mul);

	raidz_math_end();

	/*
	 * Copy shorter targets back to the original abd buffer
	 */
	if (ysize < xsize) {
		abd_copy(rm->rm_col[y].rc_data, yabd, ysize);
		abd_free(yabd, xsize);
	}
	if (zsize < xsize) {
		abd_copy(rm->rm_col[z].rc_data, zabd, zsize);
		abd_free(zabd, xsize);
	}

	return ((1 << 0) | (1 << 1) | (1 << 2));
}

#endif /* _VDEV_RAIDZ_MATH_IMPL_H */
