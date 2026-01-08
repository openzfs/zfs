// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2017-2019, Loup Vaillant
 * All rights reserved.
 *
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Monocypher 4.0.2 (Poly1305, Chacha20, and supporting utilities)
 * adapted for OpenZFS by Rob Norris <robn@despairlabs.com>
 */

/*
 * Note: this follows the Monocypher style rather than the OpenZFS style to
 * keep the diff to the bare minimum. This is important for making it easy to
 * compare the two and confirm that they are in fact the same. The diff should
 * be almost entirely in deleted lines.
 */

#include "monocypher.h"

/////////////////
/// Utilities ///
/////////////////
#define FOR_T(type, i, start, end) for (type i = (start); i < (end); i++)
#define FOR(i, start, end)         FOR_T(size_t, i, start, end)
#define ZERO(buf, size)            FOR(_i_, 0, size) (buf)[_i_] = 0
#define WIPE_CTX(ctx)              crypto_wipe(ctx   , sizeof(*(ctx)))
#define WIPE_BUFFER(buffer)        crypto_wipe(buffer, sizeof(buffer))

/*
 * OpenZFS: userspace libicp build on Linux will already have MIN/MAX defined
 * through sys/types.h -> sys/param.h. Undefine them and let Monocypher use its
 * own, in case they change in some important way in the future.
 */
#undef MIN
#undef MAX
#define MIN(a, b)                  ((a) <= (b) ? (a) : (b))
#define MAX(a, b)                  ((a) >= (b) ? (a) : (b))

typedef int8_t   i8;
typedef uint8_t  u8;
typedef int16_t  i16;
typedef uint32_t u32;
typedef int32_t  i32;
typedef int64_t  i64;
typedef uint64_t u64;

static const u8 zero[128] = {0};

// returns the smallest positive integer y such that
// (x + y) % pow_2  == 0
// Basically, y is the "gap" missing to align x.
// Only works when pow_2 is a power of 2.
// Note: we use ~x+1 instead of -x to avoid compiler warnings
static size_t gap(size_t x, size_t pow_2)
{
	return (~x + 1) & (pow_2 - 1);
}

static u32 load32_le(const u8 s[4])
{
	return
		((u32)s[0] <<  0) |
		((u32)s[1] <<  8) |
		((u32)s[2] << 16) |
		((u32)s[3] << 24);
}

static u64 load64_le(const u8 s[8])
{
	return load32_le(s) | ((u64)load32_le(s+4) << 32);
}

static void store32_le(u8 out[4], u32 in)
{
	out[0] =  in        & 0xff;
	out[1] = (in >>  8) & 0xff;
	out[2] = (in >> 16) & 0xff;
	out[3] = (in >> 24) & 0xff;
}

static void load32_le_buf (u32 *dst, const u8 *src, size_t size) {
	FOR(i, 0, size) { dst[i] = load32_le(src + i*4); }
}

static u32 rotl32(u32 x, u32 n) { return (x << n) ^ (x >> (32 - n)); }

static int neq0(u64 diff)
{
	// constant time comparison to zero
	// return diff != 0 ? -1 : 0
	u64 half = (diff >> 32) | ((u32)diff);
	return (1 & ((half - 1) >> 32)) - 1;
}

static u64 x16(const u8 a[16], const u8 b[16])
{
	return (load64_le(a + 0) ^ load64_le(b + 0))
		|  (load64_le(a + 8) ^ load64_le(b + 8));
}
int crypto_verify16(const u8 a[16], const u8 b[16]){ return neq0(x16(a, b)); }

void crypto_wipe(void *secret, size_t size)
{
	volatile u8 *v_secret = (u8*)secret;
	ZERO(v_secret, size);
}

/////////////////
/// Chacha 20 ///
/////////////////
#define QUARTERROUND(a, b, c, d)	\
	a += b;  d = rotl32(d ^ a, 16); \
	c += d;  b = rotl32(b ^ c, 12); \
	a += b;  d = rotl32(d ^ a,  8); \
	c += d;  b = rotl32(b ^ c,  7)

static void chacha20_rounds(u32 out[16], const u32 in[16])
{
	// The temporary variables make Chacha20 10% faster.
	u32 t0  = in[ 0];  u32 t1  = in[ 1];  u32 t2  = in[ 2];  u32 t3  = in[ 3];
	u32 t4  = in[ 4];  u32 t5  = in[ 5];  u32 t6  = in[ 6];  u32 t7  = in[ 7];
	u32 t8  = in[ 8];  u32 t9  = in[ 9];  u32 t10 = in[10];  u32 t11 = in[11];
	u32 t12 = in[12];  u32 t13 = in[13];  u32 t14 = in[14];  u32 t15 = in[15];

	FOR (i, 0, 10) { // 20 rounds, 2 rounds per loop.
		QUARTERROUND(t0, t4, t8 , t12); // column 0
		QUARTERROUND(t1, t5, t9 , t13); // column 1
		QUARTERROUND(t2, t6, t10, t14); // column 2
		QUARTERROUND(t3, t7, t11, t15); // column 3
		QUARTERROUND(t0, t5, t10, t15); // diagonal 0
		QUARTERROUND(t1, t6, t11, t12); // diagonal 1
		QUARTERROUND(t2, t7, t8 , t13); // diagonal 2
		QUARTERROUND(t3, t4, t9 , t14); // diagonal 3
	}
	out[ 0] = t0;   out[ 1] = t1;   out[ 2] = t2;   out[ 3] = t3;
	out[ 4] = t4;   out[ 5] = t5;   out[ 6] = t6;   out[ 7] = t7;
	out[ 8] = t8;   out[ 9] = t9;   out[10] = t10;  out[11] = t11;
	out[12] = t12;  out[13] = t13;  out[14] = t14;  out[15] = t15;
}

static const u8 *chacha20_constant = (const u8*)"expand 32-byte k"; // 16 bytes

static u64 crypto_chacha20_djb(u8 *cipher_text, const u8 *plain_text,
                        size_t text_size, const u8 key[32], const u8 nonce[8],
                        u64 ctr)
{
	u32 input[16];
	load32_le_buf(input     , chacha20_constant, 4);
	load32_le_buf(input +  4, key              , 8);
	load32_le_buf(input + 14, nonce            , 2);
	input[12] = (u32) ctr;
	input[13] = (u32)(ctr >> 32);

	// Whole blocks
	u32    pool[16];
	size_t nb_blocks = text_size >> 6;
	FOR (i, 0, nb_blocks) {
		chacha20_rounds(pool, input);
		if (plain_text != 0) {
			FOR (j, 0, 16) {
				u32 p = pool[j] + input[j];
				store32_le(cipher_text, p ^ load32_le(plain_text));
				cipher_text += 4;
				plain_text  += 4;
			}
		} else {
			FOR (j, 0, 16) {
				u32 p = pool[j] + input[j];
				store32_le(cipher_text, p);
				cipher_text += 4;
			}
		}
		input[12]++;
		if (input[12] == 0) {
			input[13]++;
		}
	}
	text_size &= 63;

	// Last (incomplete) block
	if (text_size > 0) {
		if (plain_text == 0) {
			plain_text = zero;
		}
		chacha20_rounds(pool, input);
		u8 tmp[64];
		FOR (i, 0, 16) {
			store32_le(tmp + i*4, pool[i] + input[i]);
		}
		FOR (i, 0, text_size) {
			cipher_text[i] = tmp[i] ^ plain_text[i];
		}
		WIPE_BUFFER(tmp);
	}
	ctr = input[12] + ((u64)input[13] << 32) + (text_size > 0);

	WIPE_BUFFER(pool);
	WIPE_BUFFER(input);
	return ctr;
}

u32 crypto_chacha20_ietf(u8 *cipher_text, const u8 *plain_text,
                         size_t text_size,
                         const u8 key[32], const u8 nonce[12], u32 ctr)
{
	u64 big_ctr = ctr + ((u64)load32_le(nonce) << 32);
	return (u32)crypto_chacha20_djb(cipher_text, plain_text, text_size,
	                                key, nonce + 4, big_ctr);
}

/////////////////
/// Poly 1305 ///
/////////////////

// h = (h + c) * r
// preconditions:
//   ctx->h <= 4_ffffffff_ffffffff_ffffffff_ffffffff
//   ctx->r <=   0ffffffc_0ffffffc_0ffffffc_0fffffff
//   end    <= 1
// Postcondition:
//   ctx->h <= 4_ffffffff_ffffffff_ffffffff_ffffffff
static void poly_blocks(crypto_poly1305_ctx *ctx, const u8 *in,
                        size_t nb_blocks, unsigned end)
{
	// Local all the things!
	const u32 r0 = ctx->r[0];
	const u32 r1 = ctx->r[1];
	const u32 r2 = ctx->r[2];
	const u32 r3 = ctx->r[3];
	const u32 rr0 = (r0 >> 2) * 5;  // lose 2 bits...
	const u32 rr1 = (r1 >> 2) + r1; // rr1 == (r1 >> 2) * 5
	const u32 rr2 = (r2 >> 2) + r2; // rr1 == (r2 >> 2) * 5
	const u32 rr3 = (r3 >> 2) + r3; // rr1 == (r3 >> 2) * 5
	const u32 rr4 = r0 & 3;         // ...recover 2 bits
	u32 h0 = ctx->h[0];
	u32 h1 = ctx->h[1];
	u32 h2 = ctx->h[2];
	u32 h3 = ctx->h[3];
	u32 h4 = ctx->h[4];

	FOR (i, 0, nb_blocks) {
		// h + c, without carry propagation
		const u64 s0 = (u64)h0 + load32_le(in);  in += 4;
		const u64 s1 = (u64)h1 + load32_le(in);  in += 4;
		const u64 s2 = (u64)h2 + load32_le(in);  in += 4;
		const u64 s3 = (u64)h3 + load32_le(in);  in += 4;
		const u32 s4 =      h4 + end;

		// (h + c) * r, without carry propagation
		const u64 x0 = s0*r0+ s1*rr3+ s2*rr2+ s3*rr1+ s4*rr0;
		const u64 x1 = s0*r1+ s1*r0 + s2*rr3+ s3*rr2+ s4*rr1;
		const u64 x2 = s0*r2+ s1*r1 + s2*r0 + s3*rr3+ s4*rr2;
		const u64 x3 = s0*r3+ s1*r2 + s2*r1 + s3*r0 + s4*rr3;
		const u32 x4 =                                s4*rr4;

		// partial reduction modulo 2^130 - 5
		const u32 u5 = x4 + (x3 >> 32); // u5 <= 7ffffff5
		const u64 u0 = (u5 >>  2) * 5 + (x0 & 0xffffffff);
		const u64 u1 = (u0 >> 32)     + (x1 & 0xffffffff) + (x0 >> 32);
		const u64 u2 = (u1 >> 32)     + (x2 & 0xffffffff) + (x1 >> 32);
		const u64 u3 = (u2 >> 32)     + (x3 & 0xffffffff) + (x2 >> 32);
		const u32 u4 = (u3 >> 32)     + (u5 & 3); // u4 <= 4

		// Update the hash
		h0 = u0 & 0xffffffff;
		h1 = u1 & 0xffffffff;
		h2 = u2 & 0xffffffff;
		h3 = u3 & 0xffffffff;
		h4 = u4;
	}
	ctx->h[0] = h0;
	ctx->h[1] = h1;
	ctx->h[2] = h2;
	ctx->h[3] = h3;
	ctx->h[4] = h4;
}

void crypto_poly1305_init(crypto_poly1305_ctx *ctx, const u8 key[32])
{
	ZERO(ctx->h, 5); // Initial hash is zero
	ctx->c_idx = 0;
	// load r and pad (r has some of its bits cleared)
	load32_le_buf(ctx->r  , key   , 4);
	load32_le_buf(ctx->pad, key+16, 4);
	FOR (i, 0, 1) { ctx->r[i] &= 0x0fffffff; }
	FOR (i, 1, 4) { ctx->r[i] &= 0x0ffffffc; }
}

void crypto_poly1305_update(crypto_poly1305_ctx *ctx,
                            const u8 *message, size_t message_size)
{
	// Avoid undefined NULL pointer increments with empty messages
	if (message_size == 0) {
		return;
	}

	// Align ourselves with block boundaries
	size_t aligned = MIN(gap(ctx->c_idx, 16), message_size);
	FOR (i, 0, aligned) {
		ctx->c[ctx->c_idx] = *message;
		ctx->c_idx++;
		message++;
		message_size--;
	}

	// If block is complete, process it
	if (ctx->c_idx == 16) {
		poly_blocks(ctx, ctx->c, 1, 1);
		ctx->c_idx = 0;
	}

	// Process the message block by block
	size_t nb_blocks = message_size >> 4;
	poly_blocks(ctx, message, nb_blocks, 1);
	message      += nb_blocks << 4;
	message_size &= 15;

	// remaining bytes (we never complete a block here)
	FOR (i, 0, message_size) {
		ctx->c[ctx->c_idx] = message[i];
		ctx->c_idx++;
	}
}

void crypto_poly1305_final(crypto_poly1305_ctx *ctx, u8 mac[16])
{
	// Process the last block (if any)
	// We move the final 1 according to remaining input length
	// (this will add less than 2^130 to the last input block)
	if (ctx->c_idx != 0) {
		ZERO(ctx->c + ctx->c_idx, 16 - ctx->c_idx);
		ctx->c[ctx->c_idx] = 1;
		poly_blocks(ctx, ctx->c, 1, 0);
	}

	// check if we should subtract 2^130-5 by performing the
	// corresponding carry propagation.
	u64 c = 5;
	FOR (i, 0, 4) {
		c  += ctx->h[i];
		c >>= 32;
	}
	c += ctx->h[4];
	c  = (c >> 2) * 5; // shift the carry back to the beginning
	// c now indicates how many times we should subtract 2^130-5 (0 or 1)
	FOR (i, 0, 4) {
		c += (u64)ctx->h[i] + ctx->pad[i];
		store32_le(mac + i*4, (u32)c);
		c = c >> 32;
	}
	WIPE_CTX(ctx);
}
