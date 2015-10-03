/*
 * LZ4 - Fast LZ compression algorithm
 * Header File
 * Copyright (C) 2011-2015, Yann Collet.
 * BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You can contact the author at :
 * - LZ4 source repository : https://github.com/Cyan4973/lz4
 * - LZ4 public forum : https://groups.google.com/forum/#!forum/lz4c
 */

#ifndef _SYS_LZ4_IMPL_H
#define	_SYS_LZ4_IMPL_H

#include <sys/zfs_context.h>

#ifdef	__cplusplus
extern "C" {
#endif

/* CPU Feature Detection */
/*
 * LZ4_FORCE_SW_BITCOUNT
 * Define this parameter if your target system or compiler does not support
 * hardware bit count
 *
 * Illumos : we can't use GCC's __builtin_ctz family of builtins in the
 * kernel
 * Linux : we can use GCC's __builtin_ctz family of builtins in the
 * kernel
 */
#undef	LZ4_FORCE_SW_BITCOUNT
#if defined(__sparc)
#define	LZ4_FORCE_SW_BITCOUNT
#endif

/* Compiler Options */
#define	LZ4_GCC_VERSION (__GNUC__ * 100 + __GNUC_MINOR__)

#if (LZ4_GCC_VERSION >= 302) || (__INTEL_COMPILER >= 800) || defined(__clang__)
#define	expect(expr, value)	(__builtin_expect((expr), (value)))
#else
#define	expect(expr, value)	(expr)
#endif

#ifndef likely
#define	likely(expr)	expect((expr) != 0, 1)
#endif
#ifndef unlikely
#define	unlikely(expr)	expect((expr) != 0, 0)
#endif

/* Basic Types */
typedef uint8_t BYTE;
typedef uint16_t U16;
typedef uint32_t U32;
typedef int32_t S32;
typedef uint64_t U64;

/* Common Constants */
#define	MINMATCH 4

#define	COPYLENGTH 8
#define	LASTLITERALS 5
#define	MFLIMIT (COPYLENGTH+MINMATCH)

#define	MAXD_LOG 16
#define	MAX_DISTANCE ((1 << MAXD_LOG) - 1)

#define	ML_BITS  4
#define	ML_MASK  ((1U<<ML_BITS)-1)
#define	RUN_BITS (8-ML_BITS)
#define	RUN_MASK ((1U<<RUN_BITS)-1)

/* Reading and writing into memory */
#define	STEPSIZE sizeof (size_t)

static unsigned
LZ4_64bits(void)
{
	return (sizeof (void *) == 8);
}

static unsigned
LZ4_isLittleEndian(void)
{
	/* don't use static : performance detrimental */
	const union {
		U32 i;
		BYTE c[4];
	} one = { 1 };

	return (one.c[0]);
}

/* Common functions */
static unsigned LZ4_NbCommonBytes(register size_t val)
{
	if (LZ4_isLittleEndian()) {
		if (LZ4_64bits()) {
#if (defined(__clang__) || (LZ4_GCC_VERSION >= 304)) && \
	!defined(LZ4_FORCE_SW_BITCOUNT)
			return (__builtin_ctzll((U64)val) >> 3);
#else
			static const int DeBruijnBytePos[64] =
			    { 0, 0, 0, 0, 0, 1, 1, 2, 0, 3, 1, 3, 1, 4, 2, 7,
				0, 2, 3, 6, 1, 5, 3, 5, 1, 3, 4, 4, 2, 5, 6, 7,
				7, 0, 1, 2, 3, 3, 4, 6, 2, 6, 5, 5, 3, 4, 5, 6,
				7, 1, 2, 4, 6, 4, 4, 5, 7, 2, 6, 5, 7, 6, 7, 7
			};
			return (DeBruijnBytePos[
			    ((U64)((val & -(long long)val) *
			    0x0218A392CDABBD3FULL)) >> 58]);
#endif
		} else {	/* 32 bits */

#if (defined(__clang__) || (LZ4_GCC_VERSION >= 304)) && \
	!defined(LZ4_FORCE_SW_BITCOUNT)
			return (__builtin_ctz((U32)val) >> 3);
#else
			static const int DeBruijnBytePos[32] =
			    { 0, 0, 3, 0, 3, 1, 3, 0, 3, 2, 2, 1, 3, 2, 0, 1,
				3, 3, 1, 2, 2, 2, 2, 0, 3, 1, 2, 0, 1, 0, 1, 1
			};
			return (DeBruijnBytePos[((U32)((val & -(S32)val) *
			    0x077CB531U)) >> 27]);
#endif
		}
	} else {	/* Big Endian CPU */

		if (LZ4_64bits()) {
#if (defined(__clang__) || (LZ4_GCC_VERSION >= 304)) && \
	!defined(LZ4_FORCE_SW_BITCOUNT)
			return (__builtin_clzll((U64)val) >> 3);
#else
			unsigned r;
			if (!(val >> 32)) {
				r = 4;
			} else {
				r = 0;
				val >>= 32;
			}
			if (!(val >> 16)) {
				r += 2;
				val >>= 8;
			} else {
				val >>= 24;
			}
			r += (!val);
			return (r);
#endif
		} else {	/* 32 bits */

#if (defined(__clang__) || (LZ4_GCC_VERSION >= 304)) && \
	!defined(LZ4_FORCE_SW_BITCOUNT)
			return (__builtin_clz((U32)val) >> 3);
#else
			unsigned r;
			if (!(val >> 16)) {
				r = 2;
				val >>= 8;
			} else {
				r = 0;
				val >>= 24;
			}
			r += (!val);
			return (r);
#endif
		}
	}
}

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_LZ4_IMPL_H */
