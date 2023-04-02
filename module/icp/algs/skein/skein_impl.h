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

#ifndef	_SKEIN_IMPL_H_
#define	_SKEIN_IMPL_H_

#include <sys/string.h>
#include <sys/skein.h>

#include "skein_impl.h"

#if defined(_ZFS_BIG_ENDIAN)
#define	SKEIN_NEED_SWAP		(1)
#define	Skein_Swap64(w64)	(__builtin_bswap64(w64))
#else
#define	SKEIN_NEED_SWAP		(0)
#define	Skein_Swap64(w64)	(w64)
#define	Skein_Put64_LSB_First(dst, src, bCnt) memcpy(dst, src, bCnt)
#define	Skein_Get64_LSB_First(dst, src, wCnt) memcpy(dst, src, 8 * (wCnt))
#endif

#ifndef	Skein_Put64_LSB_First
/* this is fully portable for all endian, but slow */
static inline void
Skein_Put64_LSB_First(uint8_t *dst, const uint64_t *src, size_t bCnt)
{
	size_t n;
	for (n = 0; n < bCnt; n++)
		dst[n] = (uint8_t)(src[n >> 3] >> (8 * (n & 7)));
}
#endif				/* ifndef Skein_Put64_LSB_First */

#ifndef	Skein_Get64_LSB_First
/* this is fully portable for all endian, but slow */
static inline void
Skein_Get64_LSB_First(uint64_t *dst, const uint8_t *src, size_t wCnt)
{
	size_t n;
	for (n = 0; n < 8 * wCnt; n += 8)
		dst[n / 8] = (((uint64_t)src[n])) +
		    (((uint64_t)src[n + 1]) << 8) +
		    (((uint64_t)src[n + 2]) << 16) +
		    (((uint64_t)src[n + 3]) << 24) +
		    (((uint64_t)src[n + 4]) << 32) +
		    (((uint64_t)src[n + 5]) << 40) +
		    (((uint64_t)src[n + 6]) << 48) +
		    (((uint64_t)src[n + 7]) << 56);
}
#endif				/* ifndef Skein_Get64_LSB_First */

/* tweak word T[1]: bit field starting positions */
/* offset 64 because it's the second word  */
#define	SKEIN_T1_BIT(BIT)	((BIT) - 64)

/* bits 112..118: level in hash tree */
#define	SKEIN_T1_POS_TREE_LVL	SKEIN_T1_BIT(112)
/* bit  119: partial final input byte */
#define	SKEIN_T1_POS_BIT_PAD	SKEIN_T1_BIT(119)
/* bits 120..125: type field */
#define	SKEIN_T1_POS_BLK_TYPE	SKEIN_T1_BIT(120)
/* bits 126: first block flag */
#define	SKEIN_T1_POS_FIRST	SKEIN_T1_BIT(126)
/* bit  127: final block flag */
#define	SKEIN_T1_POS_FINAL	SKEIN_T1_BIT(127)

/* tweak word T[1]: flag bit definition(s) */
#define	SKEIN_T1_FLAG_FIRST	(((uint64_t)1) << SKEIN_T1_POS_FIRST)
#define	SKEIN_T1_FLAG_FINAL	(((uint64_t)1) << SKEIN_T1_POS_FINAL)
#define	SKEIN_T1_FLAG_BIT_PAD	(((uint64_t)1) << SKEIN_T1_POS_BIT_PAD)

/* tweak word T[1]: tree level bit field mask */
#define	SKEIN_T1_TREE_LVL_MASK	(((uint64_t)0x7F) << SKEIN_T1_POS_TREE_LVL)
#define	SKEIN_T1_TREE_LEVEL(n)	(((uint64_t)(n)) << SKEIN_T1_POS_TREE_LVL)

/* tweak word T[1]: block type field */
#define	SKEIN_BLK_TYPE_KEY	(0)	/* key, for MAC and KDF */
#define	SKEIN_BLK_TYPE_CFG	(4)	/* configuration block */
#define	SKEIN_BLK_TYPE_PERS	(8)	/* personalization string */
#define	SKEIN_BLK_TYPE_PK	(12)	/* public key (for hashing) */
#define	SKEIN_BLK_TYPE_KDF	(16)	/* key identifier for KDF */
#define	SKEIN_BLK_TYPE_NONCE	(20)	/* nonce for PRNG */
#define	SKEIN_BLK_TYPE_MSG	(48)	/* message processing */
#define	SKEIN_BLK_TYPE_OUT	(63)	/* output stage */
#define	SKEIN_BLK_TYPE_MASK	(63)	/* bit field mask */

#define	SKEIN_T1_BLK_TYPE(T)	\
	(((uint64_t)(SKEIN_BLK_TYPE_##T)) << SKEIN_T1_POS_BLK_TYPE)
/* key, for MAC and KDF */
#define	SKEIN_T1_BLK_TYPE_KEY	SKEIN_T1_BLK_TYPE(KEY)
/* configuration block */
#define	SKEIN_T1_BLK_TYPE_CFG	SKEIN_T1_BLK_TYPE(CFG)
/* personalization string */
#define	SKEIN_T1_BLK_TYPE_PERS	SKEIN_T1_BLK_TYPE(PERS)
/* public key (for digital signature hashing) */
#define	SKEIN_T1_BLK_TYPE_PK	SKEIN_T1_BLK_TYPE(PK)
/* key identifier for KDF */
#define	SKEIN_T1_BLK_TYPE_KDF	SKEIN_T1_BLK_TYPE(KDF)
/* nonce for PRNG */
#define	SKEIN_T1_BLK_TYPE_NONCE	SKEIN_T1_BLK_TYPE(NONCE)
/* message processing */
#define	SKEIN_T1_BLK_TYPE_MSG	SKEIN_T1_BLK_TYPE(MSG)
/* output stage */
#define	SKEIN_T1_BLK_TYPE_OUT	SKEIN_T1_BLK_TYPE(OUT)
/* field bit mask */
#define	SKEIN_T1_BLK_TYPE_MASK	SKEIN_T1_BLK_TYPE(MASK)

#define	SKEIN_T1_BLK_TYPE_CFG_FINAL	\
	(SKEIN_T1_BLK_TYPE_CFG | SKEIN_T1_FLAG_FINAL)
#define	SKEIN_T1_BLK_TYPE_OUT_FINAL	\
	(SKEIN_T1_BLK_TYPE_OUT | SKEIN_T1_FLAG_FINAL)

#define	SKEIN_VERSION		(1)

#ifndef	SKEIN_ID_STRING_LE	/* allow compile-time personalization */
#define	SKEIN_ID_STRING_LE	(0x33414853)	/* "SHA3" (little-endian) */
#endif

#define	SKEIN_MK_64(hi32, lo32)	((lo32) + (((uint64_t)(hi32)) << 32))
#define	SKEIN_SCHEMA_VER	SKEIN_MK_64(SKEIN_VERSION, SKEIN_ID_STRING_LE)
#define	SKEIN_KS_PARITY		SKEIN_MK_64(0x1BD11BDA, 0xA9FC1A22)

#define	SKEIN_CFG_STR_LEN	(4*8)

/* bit field definitions in config block treeInfo word */
#define	SKEIN_CFG_TREE_LEAF_SIZE_POS	(0)
#define	SKEIN_CFG_TREE_NODE_SIZE_POS	(8)
#define	SKEIN_CFG_TREE_MAX_LEVEL_POS	(16)

#define	SKEIN_CFG_TREE_LEAF_SIZE_MSK	\
	(((uint64_t)0xFF) << SKEIN_CFG_TREE_LEAF_SIZE_POS)
#define	SKEIN_CFG_TREE_NODE_SIZE_MSK	\
	(((uint64_t)0xFF) << SKEIN_CFG_TREE_NODE_SIZE_POS)
#define	SKEIN_CFG_TREE_MAX_LEVEL_MSK	\
	(((uint64_t)0xFF) << SKEIN_CFG_TREE_MAX_LEVEL_POS)

#define	SKEIN_CFG_TREE_INFO(leaf, node, maxLvl)			\
	((((uint64_t)(leaf)) << SKEIN_CFG_TREE_LEAF_SIZE_POS) |	\
	(((uint64_t)(node)) << SKEIN_CFG_TREE_NODE_SIZE_POS) |	\
	(((uint64_t)(maxLvl)) << SKEIN_CFG_TREE_MAX_LEVEL_POS))

/* use as treeInfo in InitExt() call for sequential processing */
#define	SKEIN_CFG_TREE_INFO_SEQUENTIAL	SKEIN_CFG_TREE_INFO(0, 0, 0)

/*
 * Skein macros for getting/setting tweak words, etc.
 * These are useful for partial input bytes, hash tree init/update, etc.
 */
#define	Skein_Get_Tweak(ctxPtr, TWK_NUM)	((ctxPtr)->h.T[TWK_NUM])
#define	Skein_Set_Tweak(ctxPtr, TWK_NUM, tVal)		\
	do {						\
		(ctxPtr)->h.T[TWK_NUM] = (tVal);	\
	} while (0)

#define	Skein_Get_T0(ctxPtr)		Skein_Get_Tweak(ctxPtr, 0)
#define	Skein_Get_T1(ctxPtr)		Skein_Get_Tweak(ctxPtr, 1)
#define	Skein_Set_T0(ctxPtr, T0)	Skein_Set_Tweak(ctxPtr, 0, T0)
#define	Skein_Set_T1(ctxPtr, T1)	Skein_Set_Tweak(ctxPtr, 1, T1)

/* set both tweak words at once */
#define	Skein_Set_T0_T1(ctxPtr, T0, T1)		\
	do {					\
		Skein_Set_T0(ctxPtr, (T0));	\
		Skein_Set_T1(ctxPtr, (T1));	\
	} while (0)

#define	Skein_Set_Type(ctxPtr, BLK_TYPE)	\
	Skein_Set_T1(ctxPtr, SKEIN_T1_BLK_TYPE_##BLK_TYPE)

/*
 * set up for starting with a new type: h.T[0]=0; h.T[1] = NEW_TYPE; h.bCnt=0;
 */
#define	Skein_Start_New_Type(ctxPtr, BLK_TYPE)				\
	do {								\
		Skein_Set_T0_T1(ctxPtr, 0, SKEIN_T1_FLAG_FIRST |	\
		    SKEIN_T1_BLK_TYPE_ ## BLK_TYPE);			\
		(ctxPtr)->h.bCnt = 0;	\
	} while (0)

#define	Skein_Clear_First_Flag(hdr)					\
	do {								\
		(hdr).T[1] &= ~SKEIN_T1_FLAG_FIRST;			\
	} while (0)
#define	Skein_Set_Bit_Pad_Flag(hdr)					\
	do {								\
		(hdr).T[1] |=  SKEIN_T1_FLAG_BIT_PAD;			\
	} while (0)

#define	Skein_Set_Tree_Level(hdr, height)				\
	do {								\
		(hdr).T[1] |= SKEIN_T1_TREE_LEVEL(height);		\
	} while (0)

/*
 * Skein block function constants (shared across Ref and Opt code)
 */
enum {
	/* Skein_512 round rotation constants */
	R_512_0_0 = 46, R_512_0_1 = 36, R_512_0_2 = 19, R_512_0_3 = 37,
	R_512_1_0 = 33, R_512_1_1 = 27, R_512_1_2 = 14, R_512_1_3 = 42,
	R_512_2_0 = 17, R_512_2_1 = 49, R_512_2_2 = 36, R_512_2_3 = 39,
	R_512_3_0 = 44, R_512_3_1 = 9,  R_512_3_2 = 54, R_512_3_3 = 56,
	R_512_4_0 = 39, R_512_4_1 = 30, R_512_4_2 = 34, R_512_4_3 = 24,
	R_512_5_0 = 13, R_512_5_1 = 50, R_512_5_2 = 10, R_512_5_3 = 17,
	R_512_6_0 = 25, R_512_6_1 = 29, R_512_6_2 = 39, R_512_6_3 = 43,
	R_512_7_0 = 8,  R_512_7_1 = 35, R_512_7_2 = 56, R_512_7_3 = 22,
};

/* number of rounds for the different block sizes */
#define	SKEIN_512_ROUNDS_TOTAL	(72)

/* Functions to process blkCnt (nonzero) full block(s) of data. */
extern void
Skein_512_Process_Block(SKEIN_CTX *ctx, const uint8_t *blkPtr,
    size_t blkCnt, size_t byteCntAdd);

#endif				/* _SKEIN_IMPL_H_ */
