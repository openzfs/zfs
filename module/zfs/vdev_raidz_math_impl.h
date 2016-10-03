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

#define	raidz_inline inline __attribute__((always_inline))
#ifndef noinline
#define	noinline __attribute__((noinline))
#endif

/* Calculate data offset in raidz column, offset is in bytes */
#define	COL_OFF(col, off)	((v_t *)(((char *)(col)->rc_data) + (off)))

/*
 * PARITY CALCULATION
 * An optimized function is called for a full length of data columns
 * If RAIDZ map contains remainder columns (shorter columns) the same function
 * is called for reminder of full columns.
 *
 * GEN_[P|PQ|PQR]_BLOCK() functions are designed to be efficiently in-lined by
 * the compiler. This removes a lot of conditionals from the inside loop which
 * makes the code faster, especially for vectorized code.
 * They are also highly parametrized, allowing for each implementation to define
 * most optimal stride, and register allocation.
 */

static raidz_inline void
GEN_P_BLOCK(raidz_map_t * const rm, const size_t off, const size_t end,
    const int ncols)
{
	int c;
	size_t ioff;
	raidz_col_t * const pcol = raidz_col_p(rm, CODE_P);
	raidz_col_t *col;

	GEN_P_DEFINE();

	for (ioff = off; ioff < end; ioff += (GEN_P_STRIDE * sizeof (v_t))) {
		LOAD(COL_OFF(&(rm->rm_col[1]), ioff), GEN_P_P);

		for (c = 2; c < ncols; c++) {
			col = &rm->rm_col[c];
			XOR_ACC(COL_OFF(col, ioff), GEN_P_P);
		}

		STORE(COL_OFF(pcol, ioff), GEN_P_P);
	}
}

/*
 * Generate P parity (RAIDZ1)
 *
 * @rm	RAIDZ map
 */
static raidz_inline void
raidz_generate_p_impl(raidz_map_t * const rm)
{
	const int ncols = raidz_ncols(rm);
	const size_t psize = raidz_big_size(rm);
	const size_t short_size = raidz_short_size(rm);

	raidz_math_begin();

	/* short_size */
	GEN_P_BLOCK(rm, 0, short_size, ncols);

	/* fullcols */
	GEN_P_BLOCK(rm, short_size, psize, raidz_nbigcols(rm));

	raidz_math_end();
}

static raidz_inline void
GEN_PQ_BLOCK(raidz_map_t * const rm, const size_t off, const size_t end,
    const int ncols, const int nbigcols)
{
	int c;
	size_t ioff;
	raidz_col_t * const pcol = raidz_col_p(rm, CODE_P);
	raidz_col_t * const qcol = raidz_col_p(rm, CODE_Q);
	raidz_col_t *col;

	GEN_PQ_DEFINE();

	MUL2_SETUP();

	for (ioff = off; ioff < end; ioff += (GEN_PQ_STRIDE * sizeof (v_t))) {
		LOAD(COL_OFF(&rm->rm_col[2], ioff), GEN_PQ_P);
		COPY(GEN_PQ_P, GEN_PQ_Q);

		for (c = 3; c < nbigcols; c++) {
			col = &rm->rm_col[c];
			LOAD(COL_OFF(col, ioff), GEN_PQ_D);
			MUL2(GEN_PQ_Q);
			XOR(GEN_PQ_D, GEN_PQ_P);
			XOR(GEN_PQ_D, GEN_PQ_Q);
		}

		STORE(COL_OFF(pcol, ioff), GEN_PQ_P);

		for (; c < ncols; c++)
			MUL2(GEN_PQ_Q);

		STORE(COL_OFF(qcol, ioff), GEN_PQ_Q);
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
	const int ncols = raidz_ncols(rm);
	const size_t psize = raidz_big_size(rm);
	const size_t short_size = raidz_short_size(rm);

	raidz_math_begin();

	/* short_size */
	GEN_PQ_BLOCK(rm, 0, short_size, ncols, ncols);

	/* fullcols */
	GEN_PQ_BLOCK(rm, short_size, psize, ncols, raidz_nbigcols(rm));

	raidz_math_end();
}


static raidz_inline void
GEN_PQR_BLOCK(raidz_map_t * const rm, const size_t off, const size_t end,
    const int ncols, const int nbigcols)
{
	int c;
	size_t ioff;
	raidz_col_t *col;
	raidz_col_t * const pcol = raidz_col_p(rm, CODE_P);
	raidz_col_t * const qcol = raidz_col_p(rm, CODE_Q);
	raidz_col_t * const rcol = raidz_col_p(rm, CODE_R);

	GEN_PQR_DEFINE();

	MUL2_SETUP();

	for (ioff = off; ioff < end; ioff += (GEN_PQR_STRIDE * sizeof (v_t))) {
		LOAD(COL_OFF(&rm->rm_col[3], ioff), GEN_PQR_P);
		COPY(GEN_PQR_P, GEN_PQR_Q);
		COPY(GEN_PQR_P, GEN_PQR_R);

		for (c = 4; c < nbigcols; c++) {
			col = &rm->rm_col[c];
			LOAD(COL_OFF(col, ioff), GEN_PQR_D);
			MUL2(GEN_PQR_Q);
			MUL4(GEN_PQR_R);
			XOR(GEN_PQR_D, GEN_PQR_P);
			XOR(GEN_PQR_D, GEN_PQR_Q);
			XOR(GEN_PQR_D, GEN_PQR_R);
		}

		STORE(COL_OFF(pcol, ioff), GEN_PQR_P);

		for (; c < ncols; c++) {
			MUL2(GEN_PQR_Q);
			MUL4(GEN_PQR_R);
		}

		STORE(COL_OFF(qcol, ioff), GEN_PQR_Q);
		STORE(COL_OFF(rcol, ioff), GEN_PQR_R);
	}
}


/*
 * Generate PQR parity (RAIDZ3)
 *
 * @rm	RAIDZ map
 */
static raidz_inline void
raidz_generate_pqr_impl(raidz_map_t * const rm)
{
	const int ncols = raidz_ncols(rm);
	const size_t psize = raidz_big_size(rm);
	const size_t short_size = raidz_short_size(rm);

	raidz_math_begin();

	/* short_size */
	GEN_PQR_BLOCK(rm, 0, short_size, ncols, ncols);

	/* fullcols */
	GEN_PQR_BLOCK(rm, short_size, psize, ncols, raidz_nbigcols(rm));

	raidz_math_end();
}

/*
 * DATA RECONSTRUCTION
 *
 * Data reconstruction process consists of two phases:
 * 	- Syndrome calculation
 * 	- Data reconstruction
 *
 * Syndrome is calculated by generating parity using available data columns
 * and zeros in places of erasure. Existing parity is added to corresponding
 * syndrome value to obtain the [P|Q|R]syn values from equation:
 * 	P = Psyn + Dx + Dy + Dz
 * 	Q = Qsyn + 2^x * Dx + 2^y * Dy + 2^z * Dz
 * 	R = Rsyn + 4^x * Dx + 4^y * Dy + 4^z * Dz
 *
 * For data reconstruction phase, the corresponding equations are solved
 * for missing data (Dx, Dy, Dz). This generally involves multiplying known
 * symbols by an coefficient and adding them together. The multiplication
 * constant coefficients are calculated ahead of the operation in
 * raidz_rec_[q|r|pq|pq|qr|pqr]_coeff() functions.
 *
 * IMPLEMENTATION NOTE: RAID-Z block can have complex geometry, with "big"
 * and "short" columns.
 * For this reason, reconstruction is performed in minimum of
 * two steps. First, from offset 0 to short_size, then from short_size to
 * short_size. Calculation functions REC_[*]_BLOCK() are implemented to work
 * over both ranges. The split also enables removal of conditional expressions
 * from loop bodies, improving throughput of SIMD implementations.
 * For the best performance, all functions marked with raidz_inline attribute
 * must be inlined by compiler.
 *
 *    parity          data
 *    columns         columns
 * <----------> <------------------>
 *                   x       y  <----+ missing columns (x, y)
 *                   |       |
 * +---+---+---+---+-v-+---+-v-+---+   ^ 0
 * |   |   |   |   |   |   |   |   |   |
 * |   |   |   |   |   |   |   |   |   |
 * | P | Q | R | D | D | D | D | D |   |
 * |   |   |   | 0 | 1 | 2 | 3 | 4 |   |
 * |   |   |   |   |   |   |   |   |   v
 * |   |   |   |   |   +---+---+---+   ^ short_size
 * |   |   |   |   |   |               |
 * +---+---+---+---+---+               v big_size
 * <------------------> <---------->
 *      big columns     short columns
 *
 */

/*
 * Functions calculate multiplication constants for data reconstruction.
 * Coefficients depend on RAIDZ geometry, indexes of failed child vdevs, and
 * used parity columns for reconstruction.
 * @rm			RAIDZ map
 * @tgtidx		array of missing data indexes
 * @coeff		output array of coefficients. Array must be user
 *         		provided and must hold minimum MUL_CNT values
 */
static noinline void
raidz_rec_q_coeff(const raidz_map_t *rm, const int *tgtidx, unsigned *coeff)
{
	const unsigned ncols = raidz_ncols(rm);
	const unsigned x = tgtidx[TARGET_X];

	coeff[MUL_Q_X] = gf_exp2(255 - (ncols - x - 1));
}

static noinline void
raidz_rec_r_coeff(const raidz_map_t *rm, const int *tgtidx, unsigned *coeff)
{
	const unsigned ncols = raidz_ncols(rm);
	const unsigned x = tgtidx[TARGET_X];

	coeff[MUL_R_X] = gf_exp4(255 - (ncols - x - 1));
}

static noinline void
raidz_rec_pq_coeff(const raidz_map_t *rm, const int *tgtidx, unsigned *coeff)
{
	const unsigned ncols = raidz_ncols(rm);
	const unsigned x = tgtidx[TARGET_X];
	const unsigned y = tgtidx[TARGET_Y];
	gf_t a, b, e;

	a = gf_exp2(x + 255 - y);
	b = gf_exp2(255 - (ncols - x - 1));
	e = a ^ 0x01;

	coeff[MUL_PQ_X] = gf_div(a, e);
	coeff[MUL_PQ_Y] = gf_div(b, e);
}

static noinline void
raidz_rec_pr_coeff(const raidz_map_t *rm, const int *tgtidx, unsigned *coeff)
{
	const unsigned ncols = raidz_ncols(rm);
	const unsigned x = tgtidx[TARGET_X];
	const unsigned y = tgtidx[TARGET_Y];

	gf_t a, b, e;

	a = gf_exp4(x + 255 - y);
	b = gf_exp4(255 - (ncols - x - 1));
	e = a ^ 0x01;

	coeff[MUL_PR_X] = gf_div(a, e);
	coeff[MUL_PR_Y] = gf_div(b, e);
}

static noinline void
raidz_rec_qr_coeff(const raidz_map_t *rm, const int *tgtidx, unsigned *coeff)
{
	const unsigned ncols = raidz_ncols(rm);
	const unsigned x = tgtidx[TARGET_X];
	const unsigned y = tgtidx[TARGET_Y];

	gf_t nx, ny, nxxy, nxyy, d;

	nx = gf_exp2(ncols - x - 1);
	ny = gf_exp2(ncols - y - 1);
	nxxy = gf_mul(gf_mul(nx, nx), ny);
	nxyy = gf_mul(gf_mul(nx, ny), ny);
	d = nxxy ^ nxyy;

	coeff[MUL_QR_XQ] = ny;
	coeff[MUL_QR_X]	= gf_div(ny, d);
	coeff[MUL_QR_YQ] = nx;
	coeff[MUL_QR_Y]	= gf_div(nx, d);
}

static noinline void
raidz_rec_pqr_coeff(const raidz_map_t *rm, const int *tgtidx, unsigned *coeff)
{
	const unsigned ncols = raidz_ncols(rm);
	const unsigned x = tgtidx[TARGET_X];
	const unsigned y = tgtidx[TARGET_Y];
	const unsigned z = tgtidx[TARGET_Z];

	gf_t nx, ny, nz, nxx, nyy, nzz, nyyz, nyzz, xd, yd;

	nx = gf_exp2(ncols - x - 1);
	ny = gf_exp2(ncols - y - 1);
	nz = gf_exp2(ncols - z - 1);

	nxx = gf_exp4(ncols - x - 1);
	nyy = gf_exp4(ncols - y - 1);
	nzz = gf_exp4(ncols - z - 1);

	nyyz = gf_mul(gf_mul(ny, nz), ny);
	nyzz = gf_mul(nzz, ny);

	xd = gf_mul(nxx, ny) ^ gf_mul(nx, nyy) ^ nyyz ^
	    gf_mul(nxx, nz) ^ gf_mul(nzz, nx) ^  nyzz;

	yd = gf_inv(ny ^ nz);

	coeff[MUL_PQR_XP] = gf_div(nyyz ^ nyzz, xd);
	coeff[MUL_PQR_XQ] = gf_div(nyy ^ nzz, xd);
	coeff[MUL_PQR_XR] = gf_div(ny ^ nz, xd);
	coeff[MUL_PQR_YU] = nx;
	coeff[MUL_PQR_YP] = gf_mul(nz, yd);
	coeff[MUL_PQR_YQ] = yd;
}


/*
 * Reconstruction using P parity
 * @rm		RAIDZ map
 * @off		starting offset
 * @end		ending offset
 * @x		missing data column
 * @ncols	number of column
 */
static raidz_inline void
REC_P_BLOCK(raidz_map_t * const rm, const size_t off, const size_t end,
    const int x, const int ncols)
{
	int c;
	size_t ioff;
	const size_t firstdc = raidz_parity(rm);
	raidz_col_t * const pcol = raidz_col_p(rm, CODE_P);
	raidz_col_t * const xcol = raidz_col_p(rm, x);
	raidz_col_t *col;

	REC_P_DEFINE();

	for (ioff = off; ioff < end; ioff += (REC_P_STRIDE * sizeof (v_t))) {
		LOAD(COL_OFF(pcol, ioff), REC_P_X);

		for (c = firstdc; c < x; c++) {
			col = &rm->rm_col[c];
			XOR_ACC(COL_OFF(col, ioff), REC_P_X);
		}

		for (c++; c < ncols; c++) {
			col = &rm->rm_col[c];
			XOR_ACC(COL_OFF(col, ioff), REC_P_X);
		}

		STORE(COL_OFF(xcol, ioff), REC_P_X);
	}
}

/*
 * Reconstruct single data column using P parity
 * @rec_method	REC_P_BLOCK()
 *
 * @rm		RAIDZ map
 * @tgtidx	array of missing data indexes
 */
static raidz_inline int
raidz_reconstruct_p_impl(raidz_map_t *rm, const int *tgtidx)
{
	const int x = tgtidx[TARGET_X];
	const int ncols = raidz_ncols(rm);
	const int nbigcols = raidz_nbigcols(rm);
	const size_t xsize = raidz_col_size(rm, x);
	const size_t short_size = raidz_short_size(rm);

	raidz_math_begin();

	/* 0 - short_size */
	REC_P_BLOCK(rm, 0, short_size, x, ncols);

	/* short_size - xsize */
	REC_P_BLOCK(rm, short_size, xsize, x, nbigcols);

	raidz_math_end();

	return (1 << CODE_P);
}

/*
 * Reconstruct using Q parity
 */

#define	REC_Q_SYN_UPDATE()	MUL2(REC_Q_X)

#define	REC_Q_INNER_LOOP(c)			\
{						\
	col = &rm->rm_col[c];			\
	REC_Q_SYN_UPDATE();			\
	XOR_ACC(COL_OFF(col, ioff), REC_Q_X);	\
}

/*
 * Reconstruction using Q parity
 * @rm		RAIDZ map
 * @off		starting offset
 * @end		ending offset
 * @x		missing data column
 * @coeff	multiplication coefficients
 * @ncols	number of column
 * @nbigcols	number of big columns
 */
static raidz_inline void
REC_Q_BLOCK(raidz_map_t * const rm, const size_t off, const size_t end,
    const int x, const unsigned *coeff, const int ncols, const int nbigcols)
{
	int c;
	size_t ioff = 0;
	const size_t firstdc = raidz_parity(rm);
	raidz_col_t * const qcol = raidz_col_p(rm, CODE_Q);
	raidz_col_t * const xcol = raidz_col_p(rm, x);
	raidz_col_t *col;

	REC_Q_DEFINE();

	for (ioff = off; ioff < end; ioff += (REC_Q_STRIDE * sizeof (v_t))) {
		MUL2_SETUP();

		ZERO(REC_Q_X);

		if (ncols == nbigcols) {
			for (c = firstdc; c < x; c++)
				REC_Q_INNER_LOOP(c);

			REC_Q_SYN_UPDATE();
			for (c++; c < nbigcols; c++)
				REC_Q_INNER_LOOP(c);
		} else {
			for (c = firstdc; c < nbigcols; c++) {
				REC_Q_SYN_UPDATE();
				if (x != c) {
					col = &rm->rm_col[c];
					XOR_ACC(COL_OFF(col, ioff), REC_Q_X);
				}
			}
			for (; c < ncols; c++)
				REC_Q_SYN_UPDATE();
		}

		XOR_ACC(COL_OFF(qcol, ioff), REC_Q_X);
		MUL(coeff[MUL_Q_X], REC_Q_X);
		STORE(COL_OFF(xcol, ioff), REC_Q_X);
	}
}

/*
 * Reconstruct single data column using Q parity
 * @rec_method	REC_Q_BLOCK()
 *
 * @rm		RAIDZ map
 * @tgtidx	array of missing data indexes
 */
static raidz_inline int
raidz_reconstruct_q_impl(raidz_map_t *rm, const int *tgtidx)
{
	const int x = tgtidx[TARGET_X];
	const int ncols = raidz_ncols(rm);
	const int nbigcols = raidz_nbigcols(rm);
	const size_t xsize = raidz_col_size(rm, x);
	const size_t short_size = raidz_short_size(rm);
	unsigned coeff[MUL_CNT];

	raidz_rec_q_coeff(rm, tgtidx, coeff);

	raidz_math_begin();

	/* 0 - short_size */
	REC_Q_BLOCK(rm, 0, short_size, x, coeff, ncols, ncols);

	/* short_size - xsize */
	REC_Q_BLOCK(rm, short_size, xsize, x, coeff, ncols, nbigcols);

	raidz_math_end();

	return (1 << CODE_Q);
}

/*
 * Reconstruct using R parity
 */

#define	REC_R_SYN_UPDATE()	MUL4(REC_R_X)
#define	REC_R_INNER_LOOP(c)			\
{						\
	col = &rm->rm_col[c];			\
	REC_R_SYN_UPDATE();			\
	XOR_ACC(COL_OFF(col, ioff), REC_R_X);	\
}

/*
 * Reconstruction using R parity
 * @rm		RAIDZ map
 * @off		starting offset
 * @end		ending offset
 * @x		missing data column
 * @coeff	multiplication coefficients
 * @ncols	number of column
 * @nbigcols	number of big columns
 */
static raidz_inline void
REC_R_BLOCK(raidz_map_t * const rm, const size_t off, const size_t end,
    const int x, const unsigned *coeff, const int ncols, const int nbigcols)
{
	int c;
	size_t ioff = 0;
	const size_t firstdc = raidz_parity(rm);
	raidz_col_t * const rcol = raidz_col_p(rm, CODE_R);
	raidz_col_t * const xcol = raidz_col_p(rm, x);
	raidz_col_t *col;

	REC_R_DEFINE();

	for (ioff = off; ioff < end; ioff += (REC_R_STRIDE * sizeof (v_t))) {
		MUL2_SETUP();

		ZERO(REC_R_X);

		if (ncols == nbigcols) {
			for (c = firstdc; c < x; c++)
				REC_R_INNER_LOOP(c);

			REC_R_SYN_UPDATE();
			for (c++; c < nbigcols; c++)
				REC_R_INNER_LOOP(c);
		} else {
			for (c = firstdc; c < nbigcols; c++) {
				REC_R_SYN_UPDATE();
				if (c != x) {
					col = &rm->rm_col[c];
					XOR_ACC(COL_OFF(col, ioff), REC_R_X);
				}
			}
			for (; c < ncols; c++)
				REC_R_SYN_UPDATE();
		}

		XOR_ACC(COL_OFF(rcol, ioff), REC_R_X);
		MUL(coeff[MUL_R_X], REC_R_X);
		STORE(COL_OFF(xcol, ioff), REC_R_X);
	}
}

/*
 * Reconstruct single data column using R parity
 * @rec_method	REC_R_BLOCK()
 *
 * @rm		RAIDZ map
 * @tgtidx	array of missing data indexes
 */
static raidz_inline int
raidz_reconstruct_r_impl(raidz_map_t *rm, const int *tgtidx)
{
	const int x = tgtidx[TARGET_X];
	const int ncols = raidz_ncols(rm);
	const int nbigcols = raidz_nbigcols(rm);
	const size_t xsize = raidz_col_size(rm, x);
	const size_t short_size = raidz_short_size(rm);
	unsigned coeff[MUL_CNT];

	raidz_rec_r_coeff(rm, tgtidx, coeff);

	raidz_math_begin();

	/* 0 - short_size */
	REC_R_BLOCK(rm, 0, short_size, x, coeff, ncols, ncols);

	/* short_size - xsize */
	REC_R_BLOCK(rm, short_size, xsize, x, coeff, ncols, nbigcols);

	raidz_math_end();

	return (1 << CODE_R);
}

/*
 * Reconstruct using PQ parity
 */

#define	REC_PQ_SYN_UPDATE()	MUL2(REC_PQ_Y)
#define	REC_PQ_INNER_LOOP(c)			\
{						\
	col = &rm->rm_col[c];			\
	LOAD(COL_OFF(col, ioff), REC_PQ_D);	\
	REC_PQ_SYN_UPDATE();			\
	XOR(REC_PQ_D, REC_PQ_X);		\
	XOR(REC_PQ_D, REC_PQ_Y);		\
}

/*
 * Reconstruction using PQ parity
 * @rm		RAIDZ map
 * @off		starting offset
 * @end		ending offset
 * @x		missing data column
 * @y		missing data column
 * @coeff	multiplication coefficients
 * @ncols	number of column
 * @nbigcols	number of big columns
 * @calcy	calculate second data column
 */
static raidz_inline void
REC_PQ_BLOCK(raidz_map_t * const rm, const size_t off, const size_t end,
    const int x, const int y, const unsigned *coeff, const int ncols,
    const int nbigcols, const boolean_t calcy)
{
	int c;
	size_t ioff = 0;
	const size_t firstdc = raidz_parity(rm);
	raidz_col_t * const pcol = raidz_col_p(rm, CODE_P);
	raidz_col_t * const qcol = raidz_col_p(rm, CODE_Q);
	raidz_col_t * const xcol = raidz_col_p(rm, x);
	raidz_col_t * const ycol = raidz_col_p(rm, y);
	raidz_col_t *col;

	REC_PQ_DEFINE();

	for (ioff = off; ioff < end; ioff += (REC_PQ_STRIDE * sizeof (v_t))) {
		LOAD(COL_OFF(pcol, ioff), REC_PQ_X);
		ZERO(REC_PQ_Y);
		MUL2_SETUP();

		if (ncols == nbigcols) {
			for (c = firstdc; c < x; c++)
				REC_PQ_INNER_LOOP(c);

			REC_PQ_SYN_UPDATE();
			for (c++; c < y; c++)
				REC_PQ_INNER_LOOP(c);

			REC_PQ_SYN_UPDATE();
			for (c++; c < nbigcols; c++)
				REC_PQ_INNER_LOOP(c);
		} else {
			for (c = firstdc; c < nbigcols; c++) {
				REC_PQ_SYN_UPDATE();
				if (c != x && c != y) {
					col = &rm->rm_col[c];
					LOAD(COL_OFF(col, ioff), REC_PQ_D);
					XOR(REC_PQ_D, REC_PQ_X);
					XOR(REC_PQ_D, REC_PQ_Y);
				}
			}
			for (; c < ncols; c++)
				REC_PQ_SYN_UPDATE();
		}

		XOR_ACC(COL_OFF(qcol, ioff), REC_PQ_Y);

		/* Save Pxy */
		COPY(REC_PQ_X, REC_PQ_D);

		/* Calc X */
		MUL(coeff[MUL_PQ_X], REC_PQ_X);
		MUL(coeff[MUL_PQ_Y], REC_PQ_Y);
		XOR(REC_PQ_Y,  REC_PQ_X);
		STORE(COL_OFF(xcol, ioff), REC_PQ_X);

		if (calcy) {
			/* Calc Y */
			XOR(REC_PQ_D,  REC_PQ_X);
			STORE(COL_OFF(ycol, ioff), REC_PQ_X);
		}
	}
}

/*
 * Reconstruct two data columns using PQ parity
 * @rec_method	REC_PQ_BLOCK()
 *
 * @rm		RAIDZ map
 * @tgtidx	array of missing data indexes
 */
static raidz_inline int
raidz_reconstruct_pq_impl(raidz_map_t *rm, const int *tgtidx)
{
	const int x = tgtidx[TARGET_X];
	const int y = tgtidx[TARGET_Y];
	const int ncols = raidz_ncols(rm);
	const int nbigcols = raidz_nbigcols(rm);
	const size_t xsize = raidz_col_size(rm, x);
	const size_t ysize = raidz_col_size(rm, y);
	const size_t short_size = raidz_short_size(rm);
	unsigned coeff[MUL_CNT];

	raidz_rec_pq_coeff(rm, tgtidx, coeff);

	raidz_math_begin();

	/* 0 - short_size */
	REC_PQ_BLOCK(rm, 0, short_size, x, y, coeff, ncols, ncols, B_TRUE);

	/* short_size - xsize */
	REC_PQ_BLOCK(rm, short_size, xsize, x, y, coeff, ncols, nbigcols,
	    xsize == ysize);

	raidz_math_end();

	return ((1 << CODE_P) | (1 << CODE_Q));
}

/*
 * Reconstruct using PR parity
 */

#define	REC_PR_SYN_UPDATE()	MUL4(REC_PR_Y)
#define	REC_PR_INNER_LOOP(c)			\
{						\
	col = &rm->rm_col[c];			\
	LOAD(COL_OFF(col, ioff), REC_PR_D);	\
	REC_PR_SYN_UPDATE();			\
	XOR(REC_PR_D, REC_PR_X);		\
	XOR(REC_PR_D, REC_PR_Y);		\
}

/*
 * Reconstruction using PR parity
 * @rm		RAIDZ map
 * @off		starting offset
 * @end		ending offset
 * @x		missing data column
 * @y		missing data column
 * @coeff	multiplication coefficients
 * @ncols	number of column
 * @nbigcols	number of big columns
 * @calcy	calculate second data column
 */
static raidz_inline void
REC_PR_BLOCK(raidz_map_t * const rm, const size_t off, const size_t end,
    const int x, const int y, const unsigned *coeff, const int ncols,
    const int nbigcols, const boolean_t calcy)
{
	int c;
	size_t ioff;
	const size_t firstdc = raidz_parity(rm);
	raidz_col_t * const pcol = raidz_col_p(rm, CODE_P);
	raidz_col_t * const rcol = raidz_col_p(rm, CODE_R);
	raidz_col_t * const xcol = raidz_col_p(rm, x);
	raidz_col_t * const ycol = raidz_col_p(rm, y);
	raidz_col_t *col;

	REC_PR_DEFINE();

	for (ioff = off; ioff < end; ioff += (REC_PR_STRIDE * sizeof (v_t))) {
		LOAD(COL_OFF(pcol, ioff), REC_PR_X);
		ZERO(REC_PR_Y);
		MUL2_SETUP();

		if (ncols == nbigcols) {
			for (c = firstdc; c < x; c++)
				REC_PR_INNER_LOOP(c);

			REC_PR_SYN_UPDATE();
			for (c++; c < y; c++)
				REC_PR_INNER_LOOP(c);

			REC_PR_SYN_UPDATE();
			for (c++; c < nbigcols; c++)
				REC_PR_INNER_LOOP(c);
		} else {
			for (c = firstdc; c < nbigcols; c++) {
				REC_PR_SYN_UPDATE();
				if (c != x && c != y) {
					col = &rm->rm_col[c];
					LOAD(COL_OFF(col, ioff), REC_PR_D);
					XOR(REC_PR_D, REC_PR_X);
					XOR(REC_PR_D, REC_PR_Y);
				}
			}
			for (; c < ncols; c++)
				REC_PR_SYN_UPDATE();
		}

		XOR_ACC(COL_OFF(rcol, ioff), REC_PR_Y);

		/* Save Pxy */
		COPY(REC_PR_X,  REC_PR_D);

		/* Calc X */
		MUL(coeff[MUL_PR_X], REC_PR_X);
		MUL(coeff[MUL_PR_Y], REC_PR_Y);
		XOR(REC_PR_Y,  REC_PR_X);
		STORE(COL_OFF(xcol, ioff), REC_PR_X);

		if (calcy) {
			/* Calc Y */
			XOR(REC_PR_D,  REC_PR_X);
			STORE(COL_OFF(ycol, ioff), REC_PR_X);
		}
	}
}


/*
 * Reconstruct two data columns using PR parity
 * @rec_method	REC_PR_BLOCK()
 *
 * @rm		RAIDZ map
 * @tgtidx	array of missing data indexes
 */
static raidz_inline int
raidz_reconstruct_pr_impl(raidz_map_t *rm, const int *tgtidx)
{
	const int x = tgtidx[TARGET_X];
	const int y = tgtidx[TARGET_Y];
	const int ncols = raidz_ncols(rm);
	const int nbigcols = raidz_nbigcols(rm);
	const size_t xsize = raidz_col_size(rm, x);
	const size_t ysize = raidz_col_size(rm, y);
	const size_t short_size = raidz_short_size(rm);
	unsigned coeff[MUL_CNT];

	raidz_rec_pr_coeff(rm, tgtidx, coeff);

	raidz_math_begin();

	/* 0 - short_size */
	REC_PR_BLOCK(rm, 0, short_size, x, y, coeff, ncols, ncols, B_TRUE);

	/* short_size - xsize */
	REC_PR_BLOCK(rm, short_size, xsize, x, y, coeff, ncols, nbigcols,
	    xsize == ysize);

	raidz_math_end();

	return ((1 << CODE_P) | (1 << CODE_R));
}


/*
 * Reconstruct using QR parity
 */

#define	REC_QR_SYN_UPDATE()			\
{						\
	MUL2(REC_QR_X);				\
	MUL4(REC_QR_Y);				\
}

#define	REC_QR_INNER_LOOP(c)			\
{						\
	col = &rm->rm_col[c];			\
	LOAD(COL_OFF(col, ioff), REC_QR_D);	\
	REC_QR_SYN_UPDATE();			\
	XOR(REC_QR_D, REC_QR_X);		\
	XOR(REC_QR_D, REC_QR_Y);		\
}

/*
 * Reconstruction using QR parity
 * @rm		RAIDZ map
 * @off		starting offset
 * @end		ending offset
 * @x		missing data column
 * @y		missing data column
 * @coeff	multiplication coefficients
 * @ncols	number of column
 * @nbigcols	number of big columns
 * @calcy	calculate second data column
 */
static raidz_inline void
REC_QR_BLOCK(raidz_map_t * const rm, const size_t off, const size_t end,
    const int x, const int y, const unsigned *coeff, const int ncols,
    const int nbigcols, const boolean_t calcy)
{
	int c;
	size_t ioff;
	const size_t firstdc = raidz_parity(rm);
	raidz_col_t * const qcol = raidz_col_p(rm, CODE_Q);
	raidz_col_t * const rcol = raidz_col_p(rm, CODE_R);
	raidz_col_t * const xcol = raidz_col_p(rm, x);
	raidz_col_t * const ycol = raidz_col_p(rm, y);
	raidz_col_t *col;

	REC_QR_DEFINE();

	for (ioff = off; ioff < end; ioff += (REC_QR_STRIDE * sizeof (v_t))) {
		MUL2_SETUP();
		ZERO(REC_QR_X);
		ZERO(REC_QR_Y);

		if (ncols == nbigcols) {
			for (c = firstdc; c < x; c++)
				REC_QR_INNER_LOOP(c);

			REC_QR_SYN_UPDATE();
			for (c++; c < y; c++)
				REC_QR_INNER_LOOP(c);

			REC_QR_SYN_UPDATE();
			for (c++; c < nbigcols; c++)
				REC_QR_INNER_LOOP(c);
		} else {
			for (c = firstdc; c < nbigcols; c++) {
				REC_QR_SYN_UPDATE();
				if (c != x && c != y) {
					col = &rm->rm_col[c];
					LOAD(COL_OFF(col, ioff), REC_QR_D);
					XOR(REC_QR_D, REC_QR_X);
					XOR(REC_QR_D, REC_QR_Y);
				}
			}
			for (; c < ncols; c++)
				REC_QR_SYN_UPDATE();
		}

		XOR_ACC(COL_OFF(qcol, ioff), REC_QR_X);
		XOR_ACC(COL_OFF(rcol, ioff), REC_QR_Y);

		/* Save Qxy */
		COPY(REC_QR_X,  REC_QR_D);

		/* Calc X */
		MUL(coeff[MUL_QR_XQ], REC_QR_X);	/* X = Q * xqm */
		XOR(REC_QR_Y, REC_QR_X);		/* X = R ^ X   */
		MUL(coeff[MUL_QR_X], REC_QR_X);		/* X = X * xm  */
		STORE(COL_OFF(xcol, ioff), REC_QR_X);

		if (calcy) {
			/* Calc Y */
			MUL(coeff[MUL_QR_YQ], REC_QR_D); /* X = Q * xqm */
			XOR(REC_QR_Y, REC_QR_D);	 /* X = R ^ X   */
			MUL(coeff[MUL_QR_Y], REC_QR_D);	 /* X = X * xm  */
			STORE(COL_OFF(ycol, ioff), REC_QR_D);
		}
	}
}

/*
 * Reconstruct two data columns using QR parity
 * @rec_method	REC_QR_BLOCK()
 *
 * @rm		RAIDZ map
 * @tgtidx	array of missing data indexes
 */
static raidz_inline int
raidz_reconstruct_qr_impl(raidz_map_t *rm, const int *tgtidx)
{
	const int x = tgtidx[TARGET_X];
	const int y = tgtidx[TARGET_Y];
	const int ncols = raidz_ncols(rm);
	const int nbigcols = raidz_nbigcols(rm);
	const size_t xsize = raidz_col_size(rm, x);
	const size_t ysize = raidz_col_size(rm, y);
	const size_t short_size = raidz_short_size(rm);
	unsigned coeff[MUL_CNT];

	raidz_rec_qr_coeff(rm, tgtidx, coeff);

	raidz_math_begin();

	/* 0 - short_size */
	REC_QR_BLOCK(rm, 0, short_size, x, y, coeff, ncols, ncols, B_TRUE);

	/* short_size - xsize */
	REC_QR_BLOCK(rm, short_size, xsize, x, y, coeff, ncols, nbigcols,
	    xsize == ysize);

	raidz_math_end();

	return ((1 << CODE_Q) | (1 << CODE_R));
}

/*
 * Reconstruct using PQR parity
 */

#define	REC_PQR_SYN_UPDATE()			\
{						\
	MUL2(REC_PQR_Y);			\
	MUL4(REC_PQR_Z);			\
}

#define	REC_PQR_INNER_LOOP(c)			\
{						\
	col = &rm->rm_col[(c)];			\
	LOAD(COL_OFF(col, ioff), REC_PQR_D);	\
	REC_PQR_SYN_UPDATE();			\
	XOR(REC_PQR_D, REC_PQR_X);		\
	XOR(REC_PQR_D, REC_PQR_Y);		\
	XOR(REC_PQR_D, REC_PQR_Z);		\
}

/*
 * Reconstruction using PQR parity
 * @rm		RAIDZ map
 * @off		starting offset
 * @end		ending offset
 * @x		missing data column
 * @y		missing data column
 * @z		missing data column
 * @coeff	multiplication coefficients
 * @ncols	number of column
 * @nbigcols	number of big columns
 * @calcy	calculate second data column
 * @calcz	calculate third data column
 */
static raidz_inline void
REC_PQR_BLOCK(raidz_map_t * const rm, const size_t off, const size_t end,
    const int x, const int y, const int z, const unsigned *coeff,
    const int ncols, const int nbigcols, const boolean_t calcy,
    const boolean_t calcz)
{
	int c;
	size_t ioff;
	const size_t firstdc = raidz_parity(rm);
	raidz_col_t * const pcol = raidz_col_p(rm, CODE_P);
	raidz_col_t * const qcol = raidz_col_p(rm, CODE_Q);
	raidz_col_t * const rcol = raidz_col_p(rm, CODE_R);
	raidz_col_t * const xcol = raidz_col_p(rm, x);
	raidz_col_t * const ycol = raidz_col_p(rm, y);
	raidz_col_t * const zcol = raidz_col_p(rm, z);
	raidz_col_t *col;

	REC_PQR_DEFINE();

	for (ioff = off; ioff < end; ioff += (REC_PQR_STRIDE * sizeof (v_t))) {
		MUL2_SETUP();
		LOAD(COL_OFF(pcol, ioff), REC_PQR_X);
		ZERO(REC_PQR_Y);
		ZERO(REC_PQR_Z);

		if (ncols == nbigcols) {
			for (c = firstdc; c < x; c++)
				REC_PQR_INNER_LOOP(c);

			REC_PQR_SYN_UPDATE();
			for (c++; c < y; c++)
				REC_PQR_INNER_LOOP(c);

			REC_PQR_SYN_UPDATE();
			for (c++; c < z; c++)
				REC_PQR_INNER_LOOP(c);

			REC_PQR_SYN_UPDATE();
			for (c++; c < nbigcols; c++)
				REC_PQR_INNER_LOOP(c);
		} else {
			for (c = firstdc; c < nbigcols; c++) {
				REC_PQR_SYN_UPDATE();
				if (c != x && c != y && c != z) {
					col = &rm->rm_col[c];
					LOAD(COL_OFF(col, ioff), REC_PQR_D);
					XOR(REC_PQR_D, REC_PQR_X);
					XOR(REC_PQR_D, REC_PQR_Y);
					XOR(REC_PQR_D, REC_PQR_Z);
				}
			}
			for (; c < ncols; c++)
				REC_PQR_SYN_UPDATE();
		}

		XOR_ACC(COL_OFF(qcol, ioff), REC_PQR_Y);
		XOR_ACC(COL_OFF(rcol, ioff), REC_PQR_Z);

		/* Save Pxyz and Qxyz */
		COPY(REC_PQR_X, REC_PQR_XS);
		COPY(REC_PQR_Y, REC_PQR_YS);

		/* Calc X */
		MUL(coeff[MUL_PQR_XP], REC_PQR_X);	/* Xp = Pxyz * xp   */
		MUL(coeff[MUL_PQR_XQ], REC_PQR_Y);	/* Xq = Qxyz * xq   */
		XOR(REC_PQR_Y, REC_PQR_X);
		MUL(coeff[MUL_PQR_XR], REC_PQR_Z);	/* Xr = Rxyz * xr   */
		XOR(REC_PQR_Z, REC_PQR_X);		/* X = Xp + Xq + Xr */
		STORE(COL_OFF(xcol, ioff), REC_PQR_X);

		if (calcy) {
			/* Calc Y */
			XOR(REC_PQR_X, REC_PQR_XS);	   /* Pyz = Pxyz + X */
			MUL(coeff[MUL_PQR_YU], REC_PQR_X); /* Xq = X * upd_q */
			XOR(REC_PQR_X, REC_PQR_YS);	   /* Qyz = Qxyz + Xq */
			COPY(REC_PQR_XS, REC_PQR_X);	   /* restore Pyz */
			MUL(coeff[MUL_PQR_YP], REC_PQR_X); /* Yp = Pyz * yp */
			MUL(coeff[MUL_PQR_YQ], REC_PQR_YS); /* Yq = Qyz * yq */
			XOR(REC_PQR_X, REC_PQR_YS);	    /* Y = Yp + Yq */
			STORE(COL_OFF(ycol, ioff), REC_PQR_YS);
		}

		if (calcz) {
			/* Calc Z */
			XOR(REC_PQR_XS, REC_PQR_YS);	/* Z = Pz = Pyz + Y */
			STORE(COL_OFF(zcol, ioff), REC_PQR_YS);
		}
	}
}

/*
 * Reconstruct three data columns using PQR parity
 * @rec_method	REC_PQR_BLOCK()
 *
 * @rm		RAIDZ map
 * @tgtidx	array of missing data indexes
 */
static raidz_inline int
raidz_reconstruct_pqr_impl(raidz_map_t *rm, const int *tgtidx)
{
	const int x = tgtidx[TARGET_X];
	const int y = tgtidx[TARGET_Y];
	const int z = tgtidx[TARGET_Z];
	const int ncols = raidz_ncols(rm);
	const int nbigcols = raidz_nbigcols(rm);
	const size_t xsize = raidz_col_size(rm, x);
	const size_t ysize = raidz_col_size(rm, y);
	const size_t zsize = raidz_col_size(rm, z);
	const size_t short_size = raidz_short_size(rm);
	unsigned coeff[MUL_CNT];

	raidz_rec_pqr_coeff(rm, tgtidx, coeff);

	raidz_math_begin();

	/* 0 - short_size */
	REC_PQR_BLOCK(rm, 0, short_size, x, y, z, coeff, ncols, ncols,
	    B_TRUE, B_TRUE);

	/* short_size - xsize */
	REC_PQR_BLOCK(rm, short_size, xsize, x, y, z, coeff, ncols, nbigcols,
	    xsize == ysize, xsize == zsize);

	raidz_math_end();

	return ((1 << CODE_P) | (1 << CODE_Q) | (1 << CODE_R));
}

#endif /* _VDEV_RAIDZ_MATH_IMPL_H */
