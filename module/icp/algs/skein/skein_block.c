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
 * Implementation of the Skein 512-bit hash function, based
 * on the public domain implementation by Doug Whiting.
 *
 * Copyright (c) 2008,2013 Doug Whiting
 * Copyright (c) 2023 Tino Reichardt <milky-zfs@mcmilk.de>
 */

#include <sys/zfs_context.h>
#include <sys/skein.h>

#include "skein_impl.h"

#define	rotl64(x, n)	(((x) << (n)) | ((x) >> (64 - (n))))

#ifndef	SKEIN_LOOP
#if defined(_ILP32) || defined(__powerpc)	/* Assume small stack */
/*
 * The low-level checksum routines use a lot of stack space. On systems where
 * small stacks frame are enforced (like 32-bit kernel builds), do not unroll
 * checksum calculations to save stack space.
 *
 * Due to the ways the calculations on SKEIN_LOOP are done in
 * Skein_*_Process_Block(), a value of 111 disables unrolling loops
 * in any of those functions.
 */
#define	SKEIN_LOOP 111
#else
/* We're compiling with large stacks, so with unroll */
#define	SKEIN_LOOP 001
#endif
#endif

/* some useful definitions for code here */
#define	BLK_BITS	(WCNT*64)
#define	KW_TWK_BASE	(0)
#define	KW_KEY_BASE	(3)
#define	ks		(kw + KW_KEY_BASE)
#define	ts		(kw + KW_TWK_BASE)

/* Skein_512 */
void
Skein_512_Process_Block(SKEIN_CTX *ctx, const uint8_t *blkPtr,
    size_t blkCnt, size_t byteCntAdd)
{
	enum {
		WCNT = SKEIN_512_STATE_WORDS
	};
#undef  RCNT
#define	RCNT  (SKEIN_512_ROUNDS_TOTAL / 8)

#ifdef	SKEIN_LOOP		/* configure how much to unroll the loop */
#define	SKEIN_UNROLL_512 (((SKEIN_LOOP) / 10) % 10)
#else
#define	SKEIN_UNROLL_512 (0)
#endif

#if	SKEIN_UNROLL_512
#if	(RCNT % SKEIN_UNROLL_512)
#error "Invalid SKEIN_UNROLL_512"	/* sanity check on unroll count */
#endif
	size_t r;
	/* key schedule words : chaining vars + tweak + "rotation" */
	uint64_t kw[WCNT + 4 + RCNT * 2];
#else
	uint64_t kw[WCNT + 4];	/* key schedule words : chaining vars + tweak */
#endif
	/* local copy of vars, for speed */
	uint64_t X0, X1, X2, X3, X4, X5, X6, X7;
	uint64_t w[WCNT];	/* local copy of input block */
	ts[0] = ctx->h.T[0];
	ts[1] = ctx->h.T[1];
	do {
		/*
		 * this implementation only supports 2**64 input bytes
		 * (no carry out here)
		 */
		ts[0] += byteCntAdd;	/* update processed length */

		/* precompute the key schedule for this block */
		ks[0] = ctx->X[0];
		ks[1] = ctx->X[1];
		ks[2] = ctx->X[2];
		ks[3] = ctx->X[3];
		ks[4] = ctx->X[4];
		ks[5] = ctx->X[5];
		ks[6] = ctx->X[6];
		ks[7] = ctx->X[7];
		ks[8] = ks[0] ^ ks[1] ^ ks[2] ^ ks[3] ^
		    ks[4] ^ ks[5] ^ ks[6] ^ ks[7] ^ SKEIN_KS_PARITY;

		ts[2] = ts[0] ^ ts[1];

		/* get input block in little-endian format */
		Skein_Get64_LSB_First(w, blkPtr, WCNT);

		X0 = w[0] + ks[0];	/* do the first full key injection */
		X1 = w[1] + ks[1];
		X2 = w[2] + ks[2];
		X3 = w[3] + ks[3];
		X4 = w[4] + ks[4];
		X5 = w[5] + ks[5] + ts[0];
		X6 = w[6] + ks[6] + ts[1];
		X7 = w[7] + ks[7];

		blkPtr += SKEIN_512_BLOCK_BYTES;

		/* run the rounds */
#define	Round512(p0, p1, p2, p3, p4, p5, p6, p7, ROT, rNum)		\
	X##p0 += X##p1; X##p1 = rotl64(X##p1, ROT##_0); X##p1 ^= X##p0;\
	X##p2 += X##p3; X##p3 = rotl64(X##p3, ROT##_1); X##p3 ^= X##p2;\
	X##p4 += X##p5; X##p5 = rotl64(X##p5, ROT##_2); X##p5 ^= X##p4;\
	X##p6 += X##p7; X##p7 = rotl64(X##p7, ROT##_3); X##p7 ^= X##p6;

#if	SKEIN_UNROLL_512 == 0
#define	R512(p0, p1, p2, p3, p4, p5, p6, p7, ROT, rNum)	/* unrolled */	\
	Round512(p0, p1, p2, p3, p4, p5, p6, p7, ROT, rNum)

#define	I512(R)								\
	X0 += ks[((R) + 1) % 9];	/* inject the key schedule value */\
	X1 += ks[((R) + 2) % 9];					\
	X2 += ks[((R) + 3) % 9];					\
	X3 += ks[((R) + 4) % 9];					\
	X4 += ks[((R) + 5) % 9];					\
	X5 += ks[((R) + 6) % 9] + ts[((R) + 1) % 3];			\
	X6 += ks[((R) + 7) % 9] + ts[((R) + 2) % 3];			\
	X7 += ks[((R) + 8) % 9] + (R) + 1;
#else				/* looping version */
#define	R512(p0, p1, p2, p3, p4, p5, p6, p7, ROT, rNum)			\
	Round512(p0, p1, p2, p3, p4, p5, p6, p7, ROT, rNum)

#define	I512(R)								\
	X0 += ks[r + (R) + 0];	/* inject the key schedule value */	\
	X1 += ks[r + (R) + 1];						\
	X2 += ks[r + (R) + 2];						\
	X3 += ks[r + (R) + 3];						\
	X4 += ks[r + (R) + 4];						\
	X5 += ks[r + (R) + 5] + ts[r + (R) + 0];			\
	X6 += ks[r + (R) + 6] + ts[r + (R) + 1];			\
	X7 += ks[r + (R) + 7] + r + (R);				\
	ks[r + (R)+8] = ks[r + (R) - 1];	/* rotate key schedule */\
	ts[r + (R)+2] = ts[r + (R) - 1];

		/* loop through it */
		for (r = 1; r < 2 * RCNT; r += 2 * SKEIN_UNROLL_512)
#endif				/* end of looped code definitions */
		{
#define	R512_8_rounds(R)	/* do 8 full rounds */			\
	R512(0, 1, 2, 3, 4, 5, 6, 7, R_512_0, 8 * (R) + 1);		\
	R512(2, 1, 4, 7, 6, 5, 0, 3, R_512_1, 8 * (R) + 2);		\
	R512(4, 1, 6, 3, 0, 5, 2, 7, R_512_2, 8 * (R) + 3);		\
	R512(6, 1, 0, 7, 2, 5, 4, 3, R_512_3, 8 * (R) + 4);		\
	I512(2 * (R));							\
	R512(0, 1, 2, 3, 4, 5, 6, 7, R_512_4, 8 * (R) + 5);		\
	R512(2, 1, 4, 7, 6, 5, 0, 3, R_512_5, 8 * (R) + 6);		\
	R512(4, 1, 6, 3, 0, 5, 2, 7, R_512_6, 8 * (R) + 7);		\
	R512(6, 1, 0, 7, 2, 5, 4, 3, R_512_7, 8 * (R) + 8);		\
	I512(2*(R) + 1);	/* and key injection */

	R512_8_rounds(0);

#define	R512_Unroll_R(NN) \
	((SKEIN_UNROLL_512 == 0 && SKEIN_512_ROUNDS_TOTAL / 8 > (NN)) || \
	(SKEIN_UNROLL_512 > (NN)))

#if	R512_Unroll_R(1)
			R512_8_rounds(1);
#endif
#if	R512_Unroll_R(2)
			R512_8_rounds(2);
#endif
#if	R512_Unroll_R(3)
			R512_8_rounds(3);
#endif
#if	R512_Unroll_R(4)
			R512_8_rounds(4);
#endif
#if	R512_Unroll_R(5)
			R512_8_rounds(5);
#endif
#if	R512_Unroll_R(6)
			R512_8_rounds(6);
#endif
#if	R512_Unroll_R(7)
			R512_8_rounds(7);
#endif
#if	R512_Unroll_R(8)
			R512_8_rounds(8);
#endif
#if	R512_Unroll_R(9)
			R512_8_rounds(9);
#endif
#if	R512_Unroll_R(10)
			R512_8_rounds(10);
#endif
#if	R512_Unroll_R(11)
			R512_8_rounds(11);
#endif
#if	R512_Unroll_R(12)
			R512_8_rounds(12);
#endif
#if	R512_Unroll_R(13)
			R512_8_rounds(13);
#endif
#if	R512_Unroll_R(14)
			R512_8_rounds(14);
#endif
#if	(SKEIN_UNROLL_512 > 14)
#error "need more unrolling in Skein_512_Process_Block"
#endif
		}

		/*
		 * do the final "feedforward" xor, update context chaining vars
		 */
		ctx->X[0] = X0 ^ w[0];
		ctx->X[1] = X1 ^ w[1];
		ctx->X[2] = X2 ^ w[2];
		ctx->X[3] = X3 ^ w[3];
		ctx->X[4] = X4 ^ w[4];
		ctx->X[5] = X5 ^ w[5];
		ctx->X[6] = X6 ^ w[6];
		ctx->X[7] = X7 ^ w[7];

		ts[1] &= ~SKEIN_T1_FLAG_FIRST;
	} while (--blkCnt);

	ctx->h.T[0] = ts[0];
	ctx->h.T[1] = ts[1];
}
