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

/* 512-bit Skein */

/* blkSize =  512 bits. hashSize =  256 bits */
const uint64_t SKEIN_512_IV_256[] = {
	SKEIN_MK_64(0xCCD044A1, 0x2FDB3E13),
	SKEIN_MK_64(0xE8359030, 0x1A79A9EB),
	SKEIN_MK_64(0x55AEA061, 0x4F816E6F),
	SKEIN_MK_64(0x2A2767A4, 0xAE9B94DB),
	SKEIN_MK_64(0xEC06025E, 0x74DD7683),
	SKEIN_MK_64(0xE7A436CD, 0xC4746251),
	SKEIN_MK_64(0xC36FBAF9, 0x393AD185),
	SKEIN_MK_64(0x3EEDBA18, 0x33EDFC13)
};

/* blkSize =  512 bits. hashSize =  512 bits */
const uint64_t SKEIN_512_IV_512[] = {
	SKEIN_MK_64(0x4903ADFF, 0x749C51CE),
	SKEIN_MK_64(0x0D95DE39, 0x9746DF03),
	SKEIN_MK_64(0x8FD19341, 0x27C79BCE),
	SKEIN_MK_64(0x9A255629, 0xFF352CB1),
	SKEIN_MK_64(0x5DB62599, 0xDF6CA7B0),
	SKEIN_MK_64(0xEABE394C, 0xA9D5C3F4),
	SKEIN_MK_64(0x991112C7, 0x1A75B523),
	SKEIN_MK_64(0xAE18A40B, 0x660FCC33)
};

/* init the context for a straight hashing operation  */
void
Skein_512_Init(SKEIN_CTX * ctx, size_t hashBitLen)
{
	/* output hash bit count */
	ctx->h.hashBitLen = hashBitLen;

	switch (hashBitLen) {	/* use pre-computed values, where available */
	case 512:
		memcpy(ctx->X, SKEIN_512_IV_512, sizeof (ctx->X));
		break;
	case 256:
		memcpy(ctx->X, SKEIN_512_IV_256, sizeof (ctx->X));
		break;
	}

	/*
	 * The chaining vars ctx->X are now initialized for the given
	 * hashBitLen. Set up to process the data message portion of the
	 * hash (default)
	 */
	Skein_Start_New_Type(ctx, MSG);	/* T0=0, T1= MSG type */
}

/* init the context for a MAC and/or tree hash operation */
void
Skein_512_InitExt(SKEIN_CTX *ctx, size_t hashBitLen, uint64_t treeInfo,
    const uint8_t *key, size_t keyBytes)
{
	union {
		uint8_t b[SKEIN_512_STATE_BYTES];
		uint64_t w[SKEIN_512_STATE_WORDS];
	} cfg;			/* config block */

	/* compute the initial chaining values ctx->X[], based on key */
	if (keyBytes == 0) {	/* is there a key? */
		/* no key: use all zeroes as key for config block */
		memset(ctx->X, 0, sizeof (ctx->X));
	} else {		/* here to pre-process a key */
		/* do a mini-Init right here */
		/* set output hash bit count = state size */
		ctx->h.hashBitLen = 8 * sizeof (ctx->X);
		/* set tweaks: T0 = 0; T1 = KEY type */
		Skein_Start_New_Type(ctx, KEY);
		/* zero the initial chaining variables */
		memset(ctx->X, 0, sizeof (ctx->X));
		/* hash the key */
		Skein_512_Update(ctx, key, keyBytes);
		/* put result into cfg.b[] */
		Skein_512_Final_Pad(ctx, cfg.b);
		/* copy over into ctx->X[] */
		memcpy(ctx->X, cfg.b, sizeof (cfg.b));
#if	SKEIN_NEED_SWAP
		{
			uint_t i;
			/* convert key bytes to context words */
			for (i = 0; i < SKEIN_512_STATE_WORDS; i++)
				ctx->X[i] = Skein_Swap64(ctx->X[i]);
		}
#endif
	}
	/*
	 * build/process the config block, type == CONFIG (could be
	 * precomputed for each key)
	 */
	ctx->h.hashBitLen = hashBitLen;	/* output hash bit count */
	Skein_Start_New_Type(ctx, CFG_FINAL);

	/* pre-pad cfg.w[] with zeroes */
	memset(&cfg.w, 0, sizeof (cfg.w));

	cfg.w[0] = Skein_Swap64(SKEIN_SCHEMA_VER);
	cfg.w[1] = Skein_Swap64(hashBitLen);	/* hash result length in bits */
	/* tree hash config info (or SKEIN_CFG_TREE_INFO_SEQUENTIAL) */
	cfg.w[2] = Skein_Swap64(treeInfo);

	/* compute the initial chaining values from config block */
	Skein_512_Process_Block(ctx, cfg.b, 1, SKEIN_CFG_STR_LEN);

	/* The chaining vars ctx->X are now initialized */
	/* Set up to process the data message portion of the hash (default) */
	ctx->h.bCnt = 0;	/* buffer b[] starts out empty */
	Skein_Start_New_Type(ctx, MSG);
}

/* process the input bytes */
void
Skein_512_Update(SKEIN_CTX *ctx, const uint8_t *msg, size_t msgByteCnt)
{
	size_t n;

	/* process full blocks, if any */
	if (msgByteCnt + ctx->h.bCnt > SKEIN_512_BLOCK_BYTES) {
		/* finish up any buffered message data */
		if (ctx->h.bCnt) {
			/* # bytes free in buffer b[] */
			n = SKEIN_512_BLOCK_BYTES - ctx->h.bCnt;
			if (n) {
				/* check on our logic here */
				memcpy(&ctx->b[ctx->h.bCnt], msg, n);
				msgByteCnt -= n;
				msg += n;
				ctx->h.bCnt += n;
			}
			Skein_512_Process_Block(ctx, ctx->b, 1,
			    SKEIN_512_BLOCK_BYTES);
			ctx->h.bCnt = 0;
		}
		/*
		 * now process any remaining full blocks, directly from input
		 * message data
		 */
		if (msgByteCnt > SKEIN_512_BLOCK_BYTES) {
			/* number of full blocks to process */
			n = (msgByteCnt - 1) / SKEIN_512_BLOCK_BYTES;
			Skein_512_Process_Block(ctx, msg, n,
			    SKEIN_512_BLOCK_BYTES);
			msgByteCnt -= n * SKEIN_512_BLOCK_BYTES;
			msg += n * SKEIN_512_BLOCK_BYTES;
		}
	}

	/* copy any remaining source message data bytes into b[] */
	if (msgByteCnt) {
		memcpy(&ctx->b[ctx->h.bCnt], msg, msgByteCnt);
		ctx->h.bCnt += msgByteCnt;
	}
}

/* finalize the hash computation and output the result */
void
Skein_512_Final(SKEIN_CTX *ctx, uint8_t *hashVal)
{
	size_t i, n, byteCnt;
	uint64_t X[SKEIN_512_STATE_WORDS];

	ctx->h.T[1] |= SKEIN_T1_FLAG_FINAL;	/* tag as the final block */

	/* zero pad b[] if necessary */
	if (ctx->h.bCnt < SKEIN_512_BLOCK_BYTES)
		memset(&ctx->b[ctx->h.bCnt], 0,
		    SKEIN_512_BLOCK_BYTES - ctx->h.bCnt);

	/* process the final block */
	Skein_512_Process_Block(ctx, ctx->b, 1, ctx->h.bCnt);

	/* now output the result */
	/* total number of output bytes */
	byteCnt = (ctx->h.hashBitLen + 7) >> 3;

	/* run Threefish in "counter mode" to generate output */
	/* zero out b[], so it can hold the counter */
	memset(ctx->b, 0, sizeof (ctx->b));

	/* keep a local copy of counter mode "key" */
	memcpy(X, ctx->X, sizeof (X));

	for (i = 0; i * SKEIN_512_BLOCK_BYTES < byteCnt; i++) {
		/* build the counter block */
		*(uint64_t *)ctx->b = Skein_Swap64((uint64_t)i);
		Skein_Start_New_Type(ctx, OUT_FINAL);
		/* run "counter mode" */
		Skein_512_Process_Block(ctx, ctx->b, 1, sizeof (uint64_t));
		/* number of output bytes left to go */
		n = byteCnt - i * SKEIN_512_BLOCK_BYTES;
		if (n >= SKEIN_512_BLOCK_BYTES)
			n = SKEIN_512_BLOCK_BYTES;
		/* "output" the ctr mode bytes */
		Skein_Put64_LSB_First(hashVal + i * SKEIN_512_BLOCK_BYTES,
		    ctx->X, n);
		/* restore the counter mode key for next time */
		memcpy(ctx->X, X, sizeof (X));
	}
}

/* finalize the hash computation and output the block, no OUTPUT stage */
void
Skein_512_Final_Pad(SKEIN_CTX *ctx, uint8_t *hashVal)
{
	ctx->h.T[1] |= SKEIN_T1_FLAG_FINAL;	/* tag as the final block */

	/* zero pad b[] if necessary */
	if (ctx->h.bCnt < SKEIN_512_BLOCK_BYTES)
		memset(&ctx->b[ctx->h.bCnt], 0,
		    SKEIN_512_BLOCK_BYTES - ctx->h.bCnt);

	/* process the final block */
	Skein_512_Process_Block(ctx, ctx->b, 1, ctx->h.bCnt);

	/* "output" the state bytes */
	Skein_Put64_LSB_First(hashVal, ctx->X, SKEIN_512_BLOCK_BYTES);
}

#ifdef _KERNEL
EXPORT_SYMBOL(Skein_512_Init);
EXPORT_SYMBOL(Skein_512_InitExt);
EXPORT_SYMBOL(Skein_512_Update);
EXPORT_SYMBOL(Skein_512_Final);
#endif
