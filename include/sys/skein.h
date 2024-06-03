/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 */

#ifndef	_SYS_SKEIN_H
#define	_SYS_SKEIN_H

#ifdef  _KERNEL
#include <sys/types.h>
#else
#include <stdint.h>
#include <stdlib.h>
#endif

#ifdef	__cplusplus
extern "C" {
#endif

#define	SKEIN_512_STATE_WORDS	8
#define	SKEIN_512_STATE_BYTES	(8 * SKEIN_512_STATE_WORDS)
#define	SKEIN_512_BLOCK_BYTES	(8 * SKEIN_512_STATE_WORDS)

typedef struct {
	size_t hashBitLen;	/* size of hash result, in bits */
	size_t bCnt;		/* current byte count in buffer b[] */
	uint64_t T[2];		/* tweak words: T[0]=byte cnt, T[1]=flags */
} Skein_Ctxt_Hdr_t;

typedef struct {		/*  512-bit Skein hash context structure */
	Skein_Ctxt_Hdr_t h;	/* common header context variables */
	uint64_t X[SKEIN_512_STATE_WORDS];	/* chaining variables */
	/* partial block buffer (8-byte aligned) */
	uint8_t b[SKEIN_512_BLOCK_BYTES];
} SKEIN_CTX;

/* Skein APIs for (incremental) "straight hashing" */
extern void Skein_512_Init(SKEIN_CTX *ctx, size_t hashBitLen);
extern void Skein_512_Update(SKEIN_CTX *ctx, const uint8_t *msg, size_t cnt);
extern void Skein_512_Final(SKEIN_CTX *ctx, uint8_t *hashVal);

/*
 * Skein APIs for MAC and tree hash:
 *	Final_Pad: pad, do final block, but no OUTPUT type
 *	Output:    do just the output stage
 */
extern void Skein_512_Final_Pad(SKEIN_CTX *ctx, uint8_t *hashVal);

/*
 * Skein APIs for "extended" initialization: MAC keys, tree hashing.
 * After an InitExt() call, just use Update/Final calls as with Init().
 *
 * Notes: Same parameters as _Init() calls, plus treeInfo/key/keyBytes.
 *          When keyBytes == 0 and treeInfo == SKEIN_SEQUENTIAL,
 *              the results of InitExt() are identical to calling Init().
 *          The function Init() may be called once to "precompute" the IV for
 *              a given hashBitLen value, then by saving a copy of the context
 *              the IV computation may be avoided in later calls.
 *          Similarly, the function InitExt() may be called once per MAC key
 *              to precompute the MAC IV, then a copy of the context saved and
 *              reused for each new MAC computation.
 */
extern void Skein_512_InitExt(SKEIN_CTX *ctx, size_t hashBitLen,
    uint64_t treeInfo, const uint8_t *key, size_t keyBytes);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_SKEIN_H */
