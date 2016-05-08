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

#ifndef _VDEV_RAIDZ_H
#define	_VDEV_RAIDZ_H

#include <sys/types.h>
#include <sys/debug.h>
#include <sys/kstat.h>
#include <sys/abd.h>

#ifdef  __cplusplus
extern "C" {
#endif

#define	raidz_inline inline __attribute__((always_inline))

#define	CODE_P		(0U)
#define	CODE_Q		(1U)
#define	CODE_R		(2U)

#define	PARITY_P	(1U)
#define	PARITY_PQ	(2U)
#define	PARITY_PQR	(3U)

/*
 * Parity generation methods indexes
 */
enum raidz_math_gen_op {
	RAIDZ_GEN_P = 0,
	RAIDZ_GEN_PQ,
	RAIDZ_GEN_PQR,
	RAIDZ_GEN_NUM = 3
};
/*
 * Data reconstruction methods indexes
 */
enum raidz_rec_op {
	RAIDZ_REC_P = 0,
	RAIDZ_REC_Q,
	RAIDZ_REC_R,
	RAIDZ_REC_PQ,
	RAIDZ_REC_PR,
	RAIDZ_REC_QR,
	RAIDZ_REC_PQR,
	RAIDZ_REC_NUM = 7
};

extern const char *raidz_gen_name[RAIDZ_GEN_NUM];
extern const char *raidz_rec_name[RAIDZ_REC_NUM];
extern const char *raidz_impl_names[];

/*
 * Methods used to define raidz implementation
 *
 * @raidz_gen_f	Parity generation function
 *     @par1	pointer to raidz_map
 * @raidz_rec_f	Data reconstruction function
 *     @par1	pointer to raidz_map
 *     @par2	array of reconstruction targets
 * @will_work_f Function returns TRUE if impl. is supported on the system
 */
typedef void		(*raidz_gen_f)(void *);
typedef int		(*raidz_rec_f)(void *, const int *);
typedef boolean_t	(*will_work_f)(void);

typedef struct raidz_math_ops {
	raidz_gen_f gen[RAIDZ_GEN_NUM];	/* Parity generate functions */
	raidz_rec_f rec[RAIDZ_REC_NUM];	/* Data reconstruction functions */
	will_work_f is_supported;	/* Support check function */
	char *name;			/* Name of the implementation */
} raidz_math_ops_t;

typedef struct raidz_col {
	size_t rc_devidx;		/* child device index for I/O */
	size_t rc_offset;		/* device offset */
	size_t rc_size;			/* I/O size */
	abd_t *rc_data;			/* I/O data */
	abd_miter_t rc_iter;		/* Use to access the data */
	void *rc_gdata;			/* used to store the "good" version */
	int rc_error;			/* I/O error for this device */
	unsigned int rc_tried;		/* Did we attempt this I/O column? */
	unsigned int rc_skipped;	/* Did we skip this I/O column? */
} raidz_col_t;

typedef struct raidz_map {
	size_t rm_cols;			/* Regular column count */
	size_t rm_scols;		/* Count including skipped columns */
	size_t rm_bigcols;		/* Number of oversized columns */
	size_t rm_asize;		/* Actual total I/O size */
	size_t rm_missingdata;		/* Count of missing data devices */
	size_t rm_missingparity;	/* Count of missing parity devices */
	size_t rm_firstdatacol;		/* First data column/parity count */
	size_t rm_nskip;		/* Skipped sectors for padding */
	size_t rm_skipstart;		/* Column index of padding start */
	abd_t *rm_datacopy;		/* rm_asize-buffer of copied data */
	size_t rm_reports;		/* # of referencing checksum reports */
	unsigned int rm_freed;		/* map no longer has referencing ZIO */
	unsigned int rm_ecksuminjected;	/* checksum error was injected */
	raidz_math_ops_t *rm_ops;	/* RAIDZ math operations */
	raidz_col_t rm_col[1];		/* Flexible array of I/O columns */
} raidz_map_t;

/*
 * Commonly used raidz_map helpers
 *
 * raidz_parity		Returns parity of the RAIDZ block
 * raidz_ncols		Returns number of columns the block spans
 */
#define	raidz_parity(rm)	((rm)->rm_firstdatacol)
#define	raidz_ncols(rm)		((rm)->rm_cols)
#define	raidz_nbigcols(rm)	((rm)->rm_bigcols)


/*
 * Macro defines an RAIDZ parity generation method
 *
 * @code	parity the function produce
 * @impl	name of the implementation
 */
#define	_RAIDZ_GEN_WRAP(code, impl) 					\
static void								\
impl ## _gen_ ## code(void *rmp)					\
{									\
	raidz_map_t *rm = (raidz_map_t *) rmp;				\
	raidz_generate_## code ## _impl(rm); 				\
}

/*
 * Macro defines an RAIDZ data reconstruction method
 *
 * @code	parity the function produce
 * @impl	name of the implementation
 */
#define	_RAIDZ_REC_WRAP(code, impl) 					\
static int 								\
impl ## _rec_ ## code(void *rmp, const int *tgtidx)			\
{									\
	raidz_map_t *rm = (raidz_map_t *) rmp;				\
	return (raidz_reconstruct_## code ## _impl(rm, tgtidx));	\
}

/*
 * Define all gen methods for an implementation
 *
 * @impl	name of the implementation
 */
#define	DEFINE_GEN_METHODS(impl)					\
	_RAIDZ_GEN_WRAP(p, impl);					\
	_RAIDZ_GEN_WRAP(pq, impl);					\
	_RAIDZ_GEN_WRAP(pqr, impl)

/*
 * Define all rec functions for an implementation
 *
 * @impl	name of the implementation
 */
#define	DEFINE_REC_METHODS(impl)					\
	_RAIDZ_REC_WRAP(p, impl);					\
	_RAIDZ_REC_WRAP(q, impl);					\
	_RAIDZ_REC_WRAP(r, impl);					\
	_RAIDZ_REC_WRAP(pq, impl);					\
	_RAIDZ_REC_WRAP(pr, impl);					\
	_RAIDZ_REC_WRAP(qr, impl);					\
	_RAIDZ_REC_WRAP(pqr, impl)

#define	RAIDZ_GEN_METHODS(impl)						\
{									\
	[RAIDZ_GEN_P] = & impl ## _gen_p,				\
	[RAIDZ_GEN_PQ] = & impl ## _gen_pq,				\
	[RAIDZ_GEN_PQR] = & impl ## _gen_pqr				\
}

#define	RAIDZ_REC_METHODS(impl)						\
{									\
	[RAIDZ_REC_P] = & impl ## _rec_p,				\
	[RAIDZ_REC_Q] = & impl ## _rec_q,				\
	[RAIDZ_REC_R] = & impl ## _rec_r,				\
	[RAIDZ_REC_PQ] = & impl ## _rec_pq,				\
	[RAIDZ_REC_PR] = & impl ## _rec_pr,				\
	[RAIDZ_REC_QR] = & impl ## _rec_qr,				\
	[RAIDZ_REC_PQR] = & impl ## _rec_pqr				\
}


typedef struct raidz_math_ops_kstat {
	kstat_named_t gen_kstat[RAIDZ_GEN_NUM];	/* gen method speed kiB/s */
	kstat_named_t rec_kstat[RAIDZ_REC_NUM];	/* rec method speed kiB/s */
} raidz_math_ops_kstat_t;

/*
 * Enumerate various multiplication constants
 * used in reconstruction methods
 */
typedef enum raidz_mul_info {
	/* Reconstruct Q */
	MUL_Q_X		= 0,
	/* Reconstruct R */
	MUL_R_X		= 0,
	/* Reconstruct PQ */
	MUL_PQ_X	= 0,
	MUL_PQ_Y	= 1,
	/* Reconstruct PR */
	MUL_PR_X	= 0,
	MUL_PR_Y	= 1,
	/* Reconstruct QR */
	MUL_QR_XQ	= 0,
	MUL_QR_X	= 1,
	MUL_QR_YQ	= 2,
	MUL_QR_Y	= 3,
	/* Reconstruct PQR */
	MUL_PQR_XP	= 0,
	MUL_PQR_XQ	= 1,
	MUL_PQR_XR	= 2,
	MUL_PQR_YU	= 3,
	MUL_PQR_YP	= 4,
	MUL_PQR_YQ	= 5,

	MUL_CNT		= 6
} raidz_mul_info_t;


/*
 * Initialize the multiplication tables on init
 */
extern void raidz_init_scalar_mul_lt(void);

/*
 * Powers of 2 in the Galois field defined above.
 * Elements are repeated to speed up vdev_raidz_exp2 function
 * (used for scalar reconstruction).
 */
extern const uint8_t vdev_raidz_pow2[511] __attribute__((aligned(256)));
/* Logs of 2 in the Galois field defined above. */
extern const uint8_t vdev_raidz_log2[256] __attribute__((aligned(256)));

/*
 * Multiply a given number by 2 raised to the given power.
 */
static raidz_inline uint8_t
vdev_raidz_exp2(const uint8_t a, unsigned exp)
{
	ASSERT3U(exp, <=, 255);
	if (a == 0) {
		return (0);
	} else {
		exp += vdev_raidz_log2[a];
		ASSERT3U(exp, <=, 511);
		return (vdev_raidz_pow2[exp]);
	}
}

#ifdef  __cplusplus
}
#endif

#endif /* _VDEV_RAIDZ_H */
