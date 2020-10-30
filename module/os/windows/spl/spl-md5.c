/*
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Cleaned-up and optimized version of MD5, based on the reference
 * implementation provided in RFC 1321.  See RSA Copyright information
 * below.
 */

/*
 * MD5C.C - RSA Data Security, Inc., MD5 message-digest algorithm
 */

/*
 * Copyright (C) 1991-2, RSA Data Security, Inc. Created 1991. All
 * rights reserved.
 *
 * License to copy and use this software is granted provided that it
 * is identified as the "RSA Data Security, Inc. MD5 Message-Digest
 * Algorithm" in all material mentioning or referencing this software
 * or this function.
 *
 * License is also granted to make and use derivative works provided
 * that such works are identified as "derived from the RSA Data
 * Security, Inc. MD5 Message-Digest Algorithm" in all material
 * mentioning or referencing the derived work.
 *
 * RSA Data Security, Inc. makes no representations concerning either
 * the merchantability of this software or the suitability of this
 * software for any particular purpose. It is provided "as is"
 * without express or implied warranty of any kind.
 *
 * These notices must be retained in any copies of any part of this
 * documentation and/or software.
 */

#ifndef _KERNEL
#include <stdint.h>
#endif /* _KERNEL */

#include <sys/types.h>
#include <sys/md5.h>
#include <sys/md5_consts.h>	/* MD5_CONST() optimization */
//#include "md5_byteswap.h"
#if	!defined(_KERNEL) || defined(_BOOT)
#include <strings.h>
#endif /* !_KERNEL || _BOOT */

#ifdef _KERNEL
#include <sys/systm.h>
#endif /* _KERNEL */

static void Encode(uint8_t *, const uint32_t *, size_t);

static void MD5Transform(uint32_t, uint32_t, uint32_t, uint32_t, MD5_CTX *,
    const uint8_t [64]);

static uint8_t PADDING[64] = { 0x80, /* all zeros */ };

/*
 * F, G, H and I are the basic MD5 functions.
 */
#define	F(b, c, d)	(((b) & (c)) | ((~b) & (d)))
#define	G(b, c, d)	(((b) & (d)) | ((c) & (~d)))
#define	H(b, c, d)	((b) ^ (c) ^ (d))
#define	I(b, c, d)	((c) ^ ((b) | (~d)))

/*
 * ROTATE_LEFT rotates x left n bits.
 */
#define	ROTATE_LEFT(x, n)	\
	(((x) << (n)) | ((x) >> ((sizeof (x) << 3) - (n))))

/*
 * FF, GG, HH, and II transformations for rounds 1, 2, 3, and 4.
 * Rotation is separate from addition to prevent recomputation.
 */

#define	FF(a, b, c, d, x, s, ac) { \
	(a) += F((b), (c), (d)) + (x) + ((unsigned long long)(ac)); \
	(a) = ROTATE_LEFT((a), (s)); \
	(a) += (b); \
	}

#define	GG(a, b, c, d, x, s, ac) { \
	(a) += G((b), (c), (d)) + (x) + ((unsigned long long)(ac)); \
	(a) = ROTATE_LEFT((a), (s)); \
	(a) += (b); \
	}

#define	HH(a, b, c, d, x, s, ac) { \
	(a) += H((b), (c), (d)) + (x) + ((unsigned long long)(ac)); \
	(a) = ROTATE_LEFT((a), (s)); \
	(a) += (b); \
	}

#define	II(a, b, c, d, x, s, ac) { \
	(a) += I((b), (c), (d)) + (x) + ((unsigned long long)(ac)); \
	(a) = ROTATE_LEFT((a), (s)); \
	(a) += (b); \
	}

/*
 * Loading 32-bit constants on a RISC is expensive since it involves both a
 * `sethi' and an `or'.  thus, we instead have the compiler generate `ld's to
 * load the constants from an array called `md5_consts'.  however, on intel
 * (and other CISC processors), it is cheaper to load the constant
 * directly.  thus, the c code in MD5Transform() uses the macro MD5_CONST()
 * which either expands to a constant or an array reference, depending on the
 * architecture the code is being compiled for.
 *
 * Right now, i386 and amd64 are the CISC exceptions.
 * If we get another CISC ISA, we'll have to change the ifdef.
 */

#if defined(__i386) || defined(__amd64)

#define	MD5_CONST(x)		(MD5_CONST_ ## x)
#define	MD5_CONST_e(x)		MD5_CONST(x)
#define	MD5_CONST_o(x)		MD5_CONST(x)

#else
/*
 * sparc/RISC optimization:
 *
 * while it is somewhat counter-intuitive, on sparc (and presumably other RISC
 * machines), it is more efficient to place all the constants used in this
 * function in an array and load the values out of the array than to manually
 * load the constants.  this is because setting a register to a 32-bit value
 * takes two ops in most cases: a `sethi' and an `or', but loading a 32-bit
 * value from memory only takes one `ld' (or `lduw' on v9).  while this
 * increases memory usage, the compiler can find enough other things to do
 * while waiting to keep the pipeline does not stall.  additionally, it is
 * likely that many of these constants are cached so that later accesses do
 * not even go out to the bus.
 *
 * this array is declared `static' to keep the compiler from having to
 * bcopy() this array onto the stack frame of MD5Transform() each time it is
 * called -- which is unacceptably expensive.
 *
 * the `const' is to ensure that callers are good citizens and do not try to
 * munge the array.  since these routines are going to be called from inside
 * multithreaded kernelland, this is a good safety check. -- `constants' will
 * end up in .rodata.
 *
 * unfortunately, loading from an array in this manner hurts performance under
 * intel (and presumably other CISC machines).  so, there is a macro,
 * MD5_CONST(), used in MD5Transform(), that either expands to a reference to
 * this array, or to the actual constant, depending on what platform this code
 * is compiled for.
 */

#ifdef sun4v

/*
 * Going to load these consts in 8B chunks, so need to enforce 8B alignment
 */

/* CSTYLED */
#pragma align 64 (md5_consts)
#define	_MD5_CHECK_ALIGNMENT

#endif /* sun4v */

static const uint32_t md5_consts[] = {
	MD5_CONST_0,	MD5_CONST_1,	MD5_CONST_2,	MD5_CONST_3,
	MD5_CONST_4,	MD5_CONST_5,	MD5_CONST_6,	MD5_CONST_7,
	MD5_CONST_8,	MD5_CONST_9,	MD5_CONST_10,	MD5_CONST_11,
	MD5_CONST_12,	MD5_CONST_13,	MD5_CONST_14,	MD5_CONST_15,
	MD5_CONST_16,	MD5_CONST_17,	MD5_CONST_18,	MD5_CONST_19,
	MD5_CONST_20,	MD5_CONST_21,	MD5_CONST_22,	MD5_CONST_23,
	MD5_CONST_24,	MD5_CONST_25,	MD5_CONST_26,	MD5_CONST_27,
	MD5_CONST_28,	MD5_CONST_29,	MD5_CONST_30,	MD5_CONST_31,
	MD5_CONST_32,	MD5_CONST_33,	MD5_CONST_34,	MD5_CONST_35,
	MD5_CONST_36,	MD5_CONST_37,	MD5_CONST_38,	MD5_CONST_39,
	MD5_CONST_40,	MD5_CONST_41,	MD5_CONST_42,	MD5_CONST_43,
	MD5_CONST_44,	MD5_CONST_45,	MD5_CONST_46,	MD5_CONST_47,
	MD5_CONST_48,	MD5_CONST_49,	MD5_CONST_50,	MD5_CONST_51,
	MD5_CONST_52,	MD5_CONST_53,	MD5_CONST_54,	MD5_CONST_55,
	MD5_CONST_56,	MD5_CONST_57,	MD5_CONST_58,	MD5_CONST_59,
	MD5_CONST_60,	MD5_CONST_61,	MD5_CONST_62,	MD5_CONST_63
};


#ifdef sun4v
/*
 * To reduce the number of loads, load consts in 64-bit
 * chunks and then split.
 *
 * No need to mask upper 32-bits, as just interested in
 * low 32-bits (saves an & operation and means that this
 * optimization doesn't increases the icount.
 */
#define	MD5_CONST_e(x)		(md5_consts64[x/2] >> 32)
#define	MD5_CONST_o(x)		(md5_consts64[x/2])

#else

#define	MD5_CONST_e(x)		(md5_consts[x])
#define	MD5_CONST_o(x)		(md5_consts[x])

#endif /* sun4v */

#endif

/*
 * MD5Init()
 *
 * purpose: initializes the md5 context and begins and md5 digest operation
 *   input: MD5_CTX *	: the context to initialize.
 *  output: void
 */

void
MD5Init(MD5_CTX *ctx)
{
	ctx->count[0] = ctx->count[1] = 0;

	/* load magic initialization constants */
	ctx->state[0] = MD5_INIT_CONST_1;
	ctx->state[1] = MD5_INIT_CONST_2;
	ctx->state[2] = MD5_INIT_CONST_3;
	ctx->state[3] = MD5_INIT_CONST_4;
}

/*
 * MD5Update()
 *
 * purpose: continues an md5 digest operation, using the message block
 *          to update the context.
 *   input: MD5_CTX *	: the context to update
 *          uint8_t *	: the message block
 *          uint32_t    : the length of the message block in bytes
 *  output: void
 *
 * MD5 crunches in 64-byte blocks.  All numeric constants here are related to
 * that property of MD5.
 */

void
MD5Update(MD5_CTX *ctx, const void *inpp, unsigned int input_len)
{
	uint32_t		i, buf_index, buf_len;
#ifdef	sun4v
	uint32_t		old_asi;
#endif	/* sun4v */
#if defined(__amd64)
	// uint32_t		block_count;
#endif /* !defined(__amd64) */
	const unsigned char 	*input = (const unsigned char *)inpp;

	/* compute (number of bytes computed so far) mod 64 */
	buf_index = (ctx->count[0] >> 3) & 0x3F;

	/* update number of bits hashed into this MD5 computation so far */
	if ((ctx->count[0] += (input_len << 3)) < (input_len << 3))
		ctx->count[1]++;
	ctx->count[1] += (input_len >> 29);

	buf_len = 64 - buf_index;

	/* transform as many times as possible */
	i = 0;
	if (input_len >= buf_len) {

		/*
		 * general optimization:
		 *
		 * only do initial bcopy() and MD5Transform() if
		 * buf_index != 0.  if buf_index == 0, we're just
		 * wasting our time doing the bcopy() since there
		 * wasn't any data left over from a previous call to
		 * MD5Update().
		 */

#ifdef sun4v
		/*
		 * For N1 use %asi register. However, costly to repeatedly set
		 * in MD5Transform. Therefore, set once here.
		 * Should probably restore the old value afterwards...
		 */
		old_asi = get_little();
		set_little(0x88);
#endif /* sun4v */

		if (buf_index) {
			bcopy(input, &ctx->buf_un.buf8[buf_index], buf_len);

			MD5Transform(ctx->state[0], ctx->state[1],
			    ctx->state[2], ctx->state[3], ctx,
			    ctx->buf_un.buf8);

			i = buf_len;
		}

		for (; i + 63 < input_len; i += 64)
			MD5Transform(ctx->state[0], ctx->state[1],
			    ctx->state[2], ctx->state[3], ctx, &input[i]);


#ifdef sun4v
		/*
		 * Restore old %ASI value
		 */
		set_little(old_asi);
#endif /* sun4v */

		/*
		 * general optimization:
		 *
		 * if i and input_len are the same, return now instead
		 * of calling bcopy(), since the bcopy() in this
		 * case will be an expensive nop.
		 */

		if (input_len == i)
			return;

		buf_index = 0;
	}

	/* buffer remaining input */
	bcopy(&input[i], &ctx->buf_un.buf8[buf_index], input_len - i);
}

/*
 * MD5Final()
 *
 * purpose: ends an md5 digest operation, finalizing the message digest and
 *          zeroing the context.
 *   input: uchar_t *	: a buffer to store the digest in
 *			: The function actually uses void* because many
 *			: callers pass things other than uchar_t here.
 *          MD5_CTX *   : the context to finalize, save, and zero
 *  output: void
 */

void
MD5Final(void *digest, MD5_CTX *ctx)
{
	uint8_t		bitcount_le[sizeof (ctx->count)];
	uint32_t	index = (ctx->count[0] >> 3) & 0x3f;

	/* store bit count, little endian */
	Encode(bitcount_le, ctx->count, sizeof (bitcount_le));

	/* pad out to 56 mod 64 */
	MD5Update(ctx, PADDING, ((index < 56) ? 56 : 120) - index);

	/* append length (before padding) */
	MD5Update(ctx, bitcount_le, sizeof (bitcount_le));

	/* store state in digest */
	Encode(digest, ctx->state, sizeof (ctx->state));

	/* zeroize sensitive information */
	bzero(ctx, sizeof (*ctx));
}

#ifndef	_KERNEL

void
md5_calc(unsigned char *output, unsigned char *input, unsigned int inlen)
{
	MD5_CTX context;

	MD5Init(&context);
	MD5Update(&context, input, inlen);
	MD5Final(output, &context);
}

#endif	/* !_KERNEL */

/*
 * sparc register window optimization:
 *
 * `a', `b', `c', and `d' are passed into MD5Transform explicitly
 * since it increases the number of registers available to the
 * compiler.  under this scheme, these variables can be held in
 * %i0 - %i3, which leaves more local and out registers available.
 */

/*
 * MD5Transform()
 *
 * purpose: md5 transformation -- updates the digest based on `block'
 *   input: uint32_t	: bytes  1 -  4 of the digest
 *          uint32_t	: bytes  5 -  8 of the digest
 *          uint32_t	: bytes  9 - 12 of the digest
 *          uint32_t	: bytes 12 - 16 of the digest
 *          MD5_CTX *   : the context to update
 *          uint8_t [64]: the block to use to update the digest
 *  output: void
 */

static void
MD5Transform(uint32_t a, uint32_t b, uint32_t c, uint32_t d,
    MD5_CTX *ctx, const uint8_t block[64])
{
	/*
	 * general optimization:
	 *
	 * use individual integers instead of using an array.  this is a
	 * win, although the amount it wins by seems to vary quite a bit.
	 */

	register uint32_t	x_0, x_1, x_2,  x_3,  x_4,  x_5,  x_6,  x_7;
	register uint32_t	x_8, x_9, x_10, x_11, x_12, x_13, x_14, x_15;
#ifdef sun4v
	unsigned long long 	*md5_consts64;

		/* LINTED E_BAD_PTR_CAST_ALIGN */
	md5_consts64 = (unsigned long long *) md5_consts;
#endif	/* sun4v */

	/*
	 * general optimization:
	 *
	 * the compiler (at least SC4.2/5.x) generates better code if
	 * variable use is localized.  in this case, swapping the integers in
	 * this order allows `x_0 'to be swapped nearest to its first use in
	 * FF(), and likewise for `x_1' and up.  note that the compiler
	 * prefers this to doing each swap right before the FF() that
	 * uses it.
	 */

	/*
	 * sparc v9/v8plus optimization:
	 *
	 * if `block' is already aligned on a 4-byte boundary, use the
	 * optimized load_little_32() directly.  otherwise, bcopy()
	 * into a buffer that *is* aligned on a 4-byte boundary and
	 * then do the load_little_32() on that buffer.  benchmarks
	 * have shown that using the bcopy() is better than loading
	 * the bytes individually and doing the endian-swap by hand.
	 *
	 * even though it's quite tempting to assign to do:
	 *
	 * blk = bcopy(blk, ctx->buf_un.buf32, sizeof (ctx->buf_un.buf32));
	 *
	 * and only have one set of LOAD_LITTLE_32()'s, the compiler (at least
	 * SC4.2/5.x) *does not* like that, so please resist the urge.
	 */

#ifdef _MD5_CHECK_ALIGNMENT
	if ((uintptr_t)block & 0x3) {		/* not 4-byte aligned? */
		bcopy(block, ctx->buf_un.buf32, sizeof (ctx->buf_un.buf32));

#ifdef sun4v
		x_15 = LOAD_LITTLE_32_f(ctx->buf_un.buf32);
		x_14 = LOAD_LITTLE_32_e(ctx->buf_un.buf32);
		x_13 = LOAD_LITTLE_32_d(ctx->buf_un.buf32);
		x_12 = LOAD_LITTLE_32_c(ctx->buf_un.buf32);
		x_11 = LOAD_LITTLE_32_b(ctx->buf_un.buf32);
		x_10 = LOAD_LITTLE_32_a(ctx->buf_un.buf32);
		x_9  = LOAD_LITTLE_32_9(ctx->buf_un.buf32);
		x_8  = LOAD_LITTLE_32_8(ctx->buf_un.buf32);
		x_7  = LOAD_LITTLE_32_7(ctx->buf_un.buf32);
		x_6  = LOAD_LITTLE_32_6(ctx->buf_un.buf32);
		x_5  = LOAD_LITTLE_32_5(ctx->buf_un.buf32);
		x_4  = LOAD_LITTLE_32_4(ctx->buf_un.buf32);
		x_3  = LOAD_LITTLE_32_3(ctx->buf_un.buf32);
		x_2  = LOAD_LITTLE_32_2(ctx->buf_un.buf32);
		x_1  = LOAD_LITTLE_32_1(ctx->buf_un.buf32);
		x_0  = LOAD_LITTLE_32_0(ctx->buf_un.buf32);
#else
		x_15 = LOAD_LITTLE_32(ctx->buf_un.buf32 + 15);
		x_14 = LOAD_LITTLE_32(ctx->buf_un.buf32 + 14);
		x_13 = LOAD_LITTLE_32(ctx->buf_un.buf32 + 13);
		x_12 = LOAD_LITTLE_32(ctx->buf_un.buf32 + 12);
		x_11 = LOAD_LITTLE_32(ctx->buf_un.buf32 + 11);
		x_10 = LOAD_LITTLE_32(ctx->buf_un.buf32 + 10);
		x_9  = LOAD_LITTLE_32(ctx->buf_un.buf32 +  9);
		x_8  = LOAD_LITTLE_32(ctx->buf_un.buf32 +  8);
		x_7  = LOAD_LITTLE_32(ctx->buf_un.buf32 +  7);
		x_6  = LOAD_LITTLE_32(ctx->buf_un.buf32 +  6);
		x_5  = LOAD_LITTLE_32(ctx->buf_un.buf32 +  5);
		x_4  = LOAD_LITTLE_32(ctx->buf_un.buf32 +  4);
		x_3  = LOAD_LITTLE_32(ctx->buf_un.buf32 +  3);
		x_2  = LOAD_LITTLE_32(ctx->buf_un.buf32 +  2);
		x_1  = LOAD_LITTLE_32(ctx->buf_un.buf32 +  1);
		x_0  = LOAD_LITTLE_32(ctx->buf_un.buf32 +  0);
#endif /* sun4v */
	} else
#endif
	{

#ifdef sun4v
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		x_15 = LOAD_LITTLE_32_f(block);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		x_14 = LOAD_LITTLE_32_e(block);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		x_13 = LOAD_LITTLE_32_d(block);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		x_12 = LOAD_LITTLE_32_c(block);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		x_11 = LOAD_LITTLE_32_b(block);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		x_10 = LOAD_LITTLE_32_a(block);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		x_9  = LOAD_LITTLE_32_9(block);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		x_8  = LOAD_LITTLE_32_8(block);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		x_7  = LOAD_LITTLE_32_7(block);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		x_6  = LOAD_LITTLE_32_6(block);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		x_5  = LOAD_LITTLE_32_5(block);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		x_4  = LOAD_LITTLE_32_4(block);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		x_3  = LOAD_LITTLE_32_3(block);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		x_2  = LOAD_LITTLE_32_2(block);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		x_1  = LOAD_LITTLE_32_1(block);
		/* LINTED E_BAD_PTR_CAST_ALIGN */
		x_0  = LOAD_LITTLE_32_0(block);
#else
#define LOAD_LITTLE_32(addr)    (*(uint32_t *)(void *)(addr))
		x_15 = LOAD_LITTLE_32(block + 60);
		x_14 = LOAD_LITTLE_32(block + 56);
		x_13 = LOAD_LITTLE_32(block + 52);
		x_12 = LOAD_LITTLE_32(block + 48);
		x_11 = LOAD_LITTLE_32(block + 44);
		x_10 = LOAD_LITTLE_32(block + 40);
		x_9  = LOAD_LITTLE_32(block + 36);
		x_8  = LOAD_LITTLE_32(block + 32);
		x_7  = LOAD_LITTLE_32(block + 28);
		x_6  = LOAD_LITTLE_32(block + 24);
		x_5  = LOAD_LITTLE_32(block + 20);
		x_4  = LOAD_LITTLE_32(block + 16);
		x_3  = LOAD_LITTLE_32(block + 12);
		x_2  = LOAD_LITTLE_32(block +  8);
		x_1  = LOAD_LITTLE_32(block +  4);
		x_0  = LOAD_LITTLE_32(block +  0);
#endif /* sun4v */
	}

	/* round 1 */
	FF(a, b, c, d, 	x_0, MD5_SHIFT_11, MD5_CONST_e(0));  /* 1 */
	FF(d, a, b, c, 	x_1, MD5_SHIFT_12, MD5_CONST_o(1));  /* 2 */
	FF(c, d, a, b, 	x_2, MD5_SHIFT_13, MD5_CONST_e(2));  /* 3 */
	FF(b, c, d, a, 	x_3, MD5_SHIFT_14, MD5_CONST_o(3));  /* 4 */
	FF(a, b, c, d, 	x_4, MD5_SHIFT_11, MD5_CONST_e(4));  /* 5 */
	FF(d, a, b, c, 	x_5, MD5_SHIFT_12, MD5_CONST_o(5));  /* 6 */
	FF(c, d, a, b, 	x_6, MD5_SHIFT_13, MD5_CONST_e(6));  /* 7 */
	FF(b, c, d, a, 	x_7, MD5_SHIFT_14, MD5_CONST_o(7));  /* 8 */
	FF(a, b, c, d, 	x_8, MD5_SHIFT_11, MD5_CONST_e(8));  /* 9 */
	FF(d, a, b, c, 	x_9, MD5_SHIFT_12, MD5_CONST_o(9));  /* 10 */
	FF(c, d, a, b, x_10, MD5_SHIFT_13, MD5_CONST_e(10)); /* 11 */
	FF(b, c, d, a, x_11, MD5_SHIFT_14, MD5_CONST_o(11)); /* 12 */
	FF(a, b, c, d, x_12, MD5_SHIFT_11, MD5_CONST_e(12)); /* 13 */
	FF(d, a, b, c, x_13, MD5_SHIFT_12, MD5_CONST_o(13)); /* 14 */
	FF(c, d, a, b, x_14, MD5_SHIFT_13, MD5_CONST_e(14)); /* 15 */
	FF(b, c, d, a, x_15, MD5_SHIFT_14, MD5_CONST_o(15)); /* 16 */

	/* round 2 */
	GG(a, b, c, d,  x_1, MD5_SHIFT_21, MD5_CONST_e(16)); /* 17 */
	GG(d, a, b, c,  x_6, MD5_SHIFT_22, MD5_CONST_o(17)); /* 18 */
	GG(c, d, a, b, x_11, MD5_SHIFT_23, MD5_CONST_e(18)); /* 19 */
	GG(b, c, d, a,  x_0, MD5_SHIFT_24, MD5_CONST_o(19)); /* 20 */
	GG(a, b, c, d,  x_5, MD5_SHIFT_21, MD5_CONST_e(20)); /* 21 */
	GG(d, a, b, c, x_10, MD5_SHIFT_22, MD5_CONST_o(21)); /* 22 */
	GG(c, d, a, b, x_15, MD5_SHIFT_23, MD5_CONST_e(22)); /* 23 */
	GG(b, c, d, a,  x_4, MD5_SHIFT_24, MD5_CONST_o(23)); /* 24 */
	GG(a, b, c, d,  x_9, MD5_SHIFT_21, MD5_CONST_e(24)); /* 25 */
	GG(d, a, b, c, x_14, MD5_SHIFT_22, MD5_CONST_o(25)); /* 26 */
	GG(c, d, a, b,  x_3, MD5_SHIFT_23, MD5_CONST_e(26)); /* 27 */
	GG(b, c, d, a,  x_8, MD5_SHIFT_24, MD5_CONST_o(27)); /* 28 */
	GG(a, b, c, d, x_13, MD5_SHIFT_21, MD5_CONST_e(28)); /* 29 */
	GG(d, a, b, c,  x_2, MD5_SHIFT_22, MD5_CONST_o(29)); /* 30 */
	GG(c, d, a, b,  x_7, MD5_SHIFT_23, MD5_CONST_e(30)); /* 31 */
	GG(b, c, d, a, x_12, MD5_SHIFT_24, MD5_CONST_o(31)); /* 32 */

	/* round 3 */
	HH(a, b, c, d,  x_5, MD5_SHIFT_31, MD5_CONST_e(32)); /* 33 */
	HH(d, a, b, c,  x_8, MD5_SHIFT_32, MD5_CONST_o(33)); /* 34 */
	HH(c, d, a, b, x_11, MD5_SHIFT_33, MD5_CONST_e(34)); /* 35 */
	HH(b, c, d, a, x_14, MD5_SHIFT_34, MD5_CONST_o(35)); /* 36 */
	HH(a, b, c, d,  x_1, MD5_SHIFT_31, MD5_CONST_e(36)); /* 37 */
	HH(d, a, b, c,  x_4, MD5_SHIFT_32, MD5_CONST_o(37)); /* 38 */
	HH(c, d, a, b,  x_7, MD5_SHIFT_33, MD5_CONST_e(38)); /* 39 */
	HH(b, c, d, a, x_10, MD5_SHIFT_34, MD5_CONST_o(39)); /* 40 */
	HH(a, b, c, d, x_13, MD5_SHIFT_31, MD5_CONST_e(40)); /* 41 */
	HH(d, a, b, c,  x_0, MD5_SHIFT_32, MD5_CONST_o(41)); /* 42 */
	HH(c, d, a, b,  x_3, MD5_SHIFT_33, MD5_CONST_e(42)); /* 43 */
	HH(b, c, d, a,  x_6, MD5_SHIFT_34, MD5_CONST_o(43)); /* 44 */
	HH(a, b, c, d,  x_9, MD5_SHIFT_31, MD5_CONST_e(44)); /* 45 */
	HH(d, a, b, c, x_12, MD5_SHIFT_32, MD5_CONST_o(45)); /* 46 */
	HH(c, d, a, b, x_15, MD5_SHIFT_33, MD5_CONST_e(46)); /* 47 */
	HH(b, c, d, a,  x_2, MD5_SHIFT_34, MD5_CONST_o(47)); /* 48 */

	/* round 4 */
	II(a, b, c, d,  x_0, MD5_SHIFT_41, MD5_CONST_e(48)); /* 49 */
	II(d, a, b, c,  x_7, MD5_SHIFT_42, MD5_CONST_o(49)); /* 50 */
	II(c, d, a, b, x_14, MD5_SHIFT_43, MD5_CONST_e(50)); /* 51 */
	II(b, c, d, a,  x_5, MD5_SHIFT_44, MD5_CONST_o(51)); /* 52 */
	II(a, b, c, d, x_12, MD5_SHIFT_41, MD5_CONST_e(52)); /* 53 */
	II(d, a, b, c,  x_3, MD5_SHIFT_42, MD5_CONST_o(53)); /* 54 */
	II(c, d, a, b, x_10, MD5_SHIFT_43, MD5_CONST_e(54)); /* 55 */
	II(b, c, d, a,  x_1, MD5_SHIFT_44, MD5_CONST_o(55)); /* 56 */
	II(a, b, c, d,  x_8, MD5_SHIFT_41, MD5_CONST_e(56)); /* 57 */
	II(d, a, b, c, x_15, MD5_SHIFT_42, MD5_CONST_o(57)); /* 58 */
	II(c, d, a, b,  x_6, MD5_SHIFT_43, MD5_CONST_e(58)); /* 59 */
	II(b, c, d, a, x_13, MD5_SHIFT_44, MD5_CONST_o(59)); /* 60 */
	II(a, b, c, d,  x_4, MD5_SHIFT_41, MD5_CONST_e(60)); /* 61 */
	II(d, a, b, c, x_11, MD5_SHIFT_42, MD5_CONST_o(61)); /* 62 */
	II(c, d, a, b,  x_2, MD5_SHIFT_43, MD5_CONST_e(62)); /* 63 */
	II(b, c, d, a,  x_9, MD5_SHIFT_44, MD5_CONST_o(63)); /* 64 */

	ctx->state[0] += a;
	ctx->state[1] += b;
	ctx->state[2] += c;
	ctx->state[3] += d;

	/*
	 * zeroize sensitive information -- compiler will optimize
	 * this out if everything is kept in registers
	 */

	x_0 = x_1  = x_2  = x_3  = x_4  = x_5  = x_6  = x_7 = x_8 = 0;
	x_9 = x_10 = x_11 = x_12 = x_13 = x_14 = x_15 = 0;
}

/*
 * Encode()
 *
 * purpose: to convert a list of numbers from big endian to little endian
 *   input: uint8_t *	: place to store the converted little endian numbers
 *	    uint32_t *	: place to get numbers to convert from
 *          size_t	: the length of the input in bytes
 *  output: void
 */

static void
Encode(uint8_t * output, const uint32_t * input,
    size_t input_len)
{
	size_t		i, j;

	for (i = 0, j = 0; j < input_len; i++, j += sizeof (uint32_t)) {

#ifdef _LITTLE_ENDIAN

#ifdef _MD5_CHECK_ALIGNMENT
		if ((uintptr_t)output & 0x3)	/* Not 4-byte aligned */
			bcopy(input + i, output + j, 4);
		else *(uint32_t *)(output + j) = input[i];
#else
		/*LINTED E_BAD_PTR_CAST_ALIGN*/
		*(uint32_t *)(output + j) = input[i];
#endif /* _MD5_CHECK_ALIGNMENT */

#else	/* big endian -- will work on little endian, but slowly */

		output[j] = input[i] & 0xff;
		output[j + 1] = (input[i] >> 8)  & 0xff;
		output[j + 2] = (input[i] >> 16) & 0xff;
		output[j + 3] = (input[i] >> 24) & 0xff;
#endif
	}
}
