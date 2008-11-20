/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright (c) 1998 by Sun Microsystems, Inc.
 * All rights reserved.
 */



/*
 * NOTE: this file is compiled into the kernel, cprboot, and savecore.
 * Therefore it must compile in kernel, boot, and userland source context;
 * so if you ever change this code, avoid references to external symbols.
 *
 * This compression algorithm is a derivative of LZRW1, which I'll call
 * LZJB in the classic LZ* spirit.  All LZ* (Lempel-Ziv) algorithms are
 * based on the same basic principle: when a "phrase" (sequences of bytes)
 * is repeated in a data stream, we can save space by storing a reference to
 * the previous instance of that phrase (a "copy item") rather than storing
 * the phrase itself (a "literal item").  The compressor remembers phrases
 * in a simple hash table (the "Lempel history") that maps three-character
 * sequences (the minimum match) to the addresses where they were last seen.
 *
 * A copy item must encode both the length and the location of the matching
 * phrase so that decompress() can reconstruct the original data stream.
 * For example, here's how we'd encode "yadda yadda yadda, blah blah blah"
 * (with "_" replacing spaces for readability):
 *
 * Original:
 *
 * y a d d a _ y a d d a _ y a d d a , _ b l a h _ b l a h _ b l a h
 *
 * Compressed:
 *
 * y a d d a _ 6 11 , _ b l a h 5 10
 *
 * In the compressed output, the "6 11" simply means "to get the original
 * data, execute memmove(ptr, ptr - 6, 11)".  Note that in this example,
 * the match at "6 11" actually extends beyond the current location and
 * overlaps it.  That's OK; like memmove(), decompress() handles overlap.
 *
 * There's still one more thing decompress() needs to know, which is how to
 * distinguish literal items from copy items.  We encode this information
 * in an 8-bit bitmap that precedes each 8 items of output; if the Nth bit
 * is set, then the Nth item is a copy item.  Thus the full encoding for
 * the example above would be:
 *
 * 0x40 y a d d a _ 6 11 , 0x20 _ b l a h 5 10
 *
 * Finally, the "6 11" isn't really encoded as the two byte values 6 and 11
 * in the output stream because, empirically, we get better compression by
 * dedicating more bits to offset, fewer to match length.  LZJB uses 6 bits
 * to encode the match length, 10 bits to encode the offset.  Since copy-item
 * encoding consumes 2 bytes, we don't generate copy items unless the match
 * length is at least 3; therefore, we can store (length - 3) in the 6-bit
 * match length field, which extends the maximum match from 63 to 66 bytes.
 * Thus the 2-byte encoding for a copy item is as follows:
 *
 *	byte[0] = ((length - 3) << 2) | (offset >> 8);
 *	byte[1] = (uint8_t)offset;
 *
 * In our example above, an offset of 6 with length 11 would be encoded as:
 *
 *	byte[0] = ((11 - 3) << 2) | (6 >> 8) = 0x20
 *	byte[1] = (uint8_t)6 = 0x6
 *
 * Similarly, an offset of 5 with length 10 would be encoded as:
 *
 *	byte[0] = ((10 - 3) << 2) | (5 >> 8) = 0x1c
 *	byte[1] = (uint8_t)5 = 0x5
 *
 * Putting it all together, the actual LZJB output for our example is:
 *
 * 0x40 y a d d a _ 0x2006 , 0x20 _ b l a h 0x1c05
 *
 * The main differences between LZRW1 and LZJB are as follows:
 *
 * (1) LZRW1 is sloppy about buffer overruns.  LZJB never reads past the
 *     end of its input, and never writes past the end of its output.
 *
 * (2) LZJB allows a maximum match length of 66 (vs. 18 for LZRW1), with
 *     the trade-off being a shorter look-behind (1K vs. 4K for LZRW1).
 *
 * (3) LZJB records only the low-order 16 bits of pointers in the Lempel
 *     history (which is all we need since the maximum look-behind is 1K),
 *     and uses only 256 hash entries (vs. 4096 for LZRW1).  This makes
 *     the compression hash small enough to allocate on the stack, which
 *     solves two problems: (1) it saves 64K of kernel/cprboot memory,
 *     and (2) it makes the code MT-safe without any locking, since we
 *     don't have multiple threads sharing a common hash table.
 *
 * (4) LZJB is faster at both compression and decompression, has a
 *     better compression ratio, and is somewhat simpler than LZRW1.
 *
 * Finally, note that LZJB is non-deterministic: given the same input,
 * two calls to compress() may produce different output.  This is a
 * general characteristic of most Lempel-Ziv derivatives because there's
 * no need to initialize the Lempel history; not doing so saves time.
 */

#include <sys/types.h>

#define	MATCH_BITS	6
#define	MATCH_MIN	3
#define	MATCH_MAX	((1 << MATCH_BITS) + (MATCH_MIN - 1))
#define	OFFSET_MASK	((1 << (16 - MATCH_BITS)) - 1)
#define	LEMPEL_SIZE	256

size_t
compress(void *s_start, void *d_start, size_t s_len)
{
	uchar_t *src = s_start;
	uchar_t *dst = d_start;
	uchar_t *cpy, *copymap;
	int copymask = 1 << (NBBY - 1);
	int mlen, offset;
	uint16_t *hp;
	uint16_t lempel[LEMPEL_SIZE];	/* uninitialized; see above */

	while (src < (uchar_t *)s_start + s_len) {
		if ((copymask <<= 1) == (1 << NBBY)) {
			if (dst >= (uchar_t *)d_start + s_len - 1 - 2 * NBBY) {
				mlen = s_len;
				for (src = s_start, dst = d_start; mlen; mlen--)
					*dst++ = *src++;
				return (s_len);
			}
			copymask = 1;
			copymap = dst;
			*dst++ = 0;
		}
		if (src > (uchar_t *)s_start + s_len - MATCH_MAX) {
			*dst++ = *src++;
			continue;
		}
		hp = &lempel[((src[0] + 13) ^ (src[1] - 13) ^ src[2]) &
		    (LEMPEL_SIZE - 1)];
		offset = (intptr_t)(src - *hp) & OFFSET_MASK;
		*hp = (uint16_t)(uintptr_t)src;
		cpy = src - offset;
		if (cpy >= (uchar_t *)s_start && cpy != src &&
		    src[0] == cpy[0] && src[1] == cpy[1] && src[2] == cpy[2]) {
			*copymap |= copymask;
			for (mlen = MATCH_MIN; mlen < MATCH_MAX; mlen++)
				if (src[mlen] != cpy[mlen])
					break;
			*dst++ = ((mlen - MATCH_MIN) << (NBBY - MATCH_BITS)) |
			    (offset >> NBBY);
			*dst++ = (uchar_t)offset;
			src += mlen;
		} else {
			*dst++ = *src++;
		}
	}
	return (dst - (uchar_t *)d_start);
}

size_t
decompress(void *s_start, void *d_start, size_t s_len, size_t d_len)
{
	uchar_t *src = s_start;
	uchar_t *dst = d_start;
	uchar_t *s_end = (uchar_t *)s_start + s_len;
	uchar_t *d_end = (uchar_t *)d_start + d_len;
	uchar_t *cpy, copymap;
	int copymask = 1 << (NBBY - 1);

	if (s_len >= d_len) {
		size_t d_rem = d_len;
		while (d_rem-- != 0)
			*dst++ = *src++;
		return (d_len);
	}

	while (src < s_end && dst < d_end) {
		if ((copymask <<= 1) == (1 << NBBY)) {
			copymask = 1;
			copymap = *src++;
		}
		if (copymap & copymask) {
			int mlen = (src[0] >> (NBBY - MATCH_BITS)) + MATCH_MIN;
			int offset = ((src[0] << NBBY) | src[1]) & OFFSET_MASK;
			src += 2;
			if ((cpy = dst - offset) >= (uchar_t *)d_start)
				while (--mlen >= 0 && dst < d_end)
					*dst++ = *cpy++;
			else
				/*
				 * offset before start of destination buffer
				 * indicates corrupt source data
				 */
				return (dst - (uchar_t *)d_start);
		} else {
			*dst++ = *src++;
		}
	}
	return (dst - (uchar_t *)d_start);
}

uint32_t
checksum32(void *cp_arg, size_t length)
{
	uchar_t *cp, *ep;
	uint32_t sum = 0;

	for (cp = cp_arg, ep = cp + length; cp < ep; cp++)
		sum = ((sum >> 1) | (sum << 31)) + *cp;
	return (sum);
}
