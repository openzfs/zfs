
/*
 * LZ4 - Fast LZ compression algorithm
 * Copyright (C) 2011 - 2016, Yann Collet.
 * BSD 2 - Clause License (http://www.opensource.org/licenses/bsd - license.php)
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *	* Redistributions of source code must retain the above copyright
 *	  notice, this list of conditions and the following disclaimer.
 *	* Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
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
 * You can contact the author at :
 *	- LZ4 homepage : http://www.lz4.org
 *	- LZ4 source repository : https://github.com/lz4/lz4
 *
 *	Changed for kernel usage by:
 *	Sven Schmidt <4sschmid@informatik.uni-hamburg.de>
 */

/*
 * Dependencies
 */

#include <sys/zfs_context.h>
#include "sys/lz4.h"
#include "sys/lz4defs.h"

static kmem_cache_t *lz4_cache;


void
lz4_init(void)
{
	lz4_cache = kmem_cache_create("lz4_cache",
	    sizeof (LZ4_stream_t), 0, NULL, NULL, NULL, NULL, NULL, 0);
}

void
lz4_fini(void)
{
	if (lz4_cache) {
		kmem_cache_destroy(lz4_cache);
		lz4_cache = NULL;
	}
}

/*ARGSUSED*/
size_t
lz4_compress_zfs(void *s_start, void *d_start, size_t s_len,
    size_t d_len, int n)
{
	uint32_t bufsiz;
	char *dest = d_start;
	void* wrkmem;

	ASSERT(d_len >= sizeof (bufsiz));

	ASSERT(lz4_cache != NULL);
	wrkmem = kmem_cache_alloc(lz4_cache, KM_SLEEP);
	/*
	 * out of kernel memory, gently fall through - this will disable
	 * compression in zio_compress_data
	 */
	if (wrkmem == NULL)
		return (0);

	bufsiz = LZ4_compress_fast(s_start, &dest[sizeof (bufsiz)], s_len,
	    d_len - sizeof (bufsiz), n, wrkmem);

	kmem_cache_free(lz4_cache, wrkmem);

	/* Signal an error if the compression routine returned zero. */
	if (bufsiz == 0)
		return (s_len);

	/*
	 * Encode the compresed buffer size at the start. We'll need this in
	 * decompression to counter the effects of padding which might be
	 * added to the compressed buffer and which, if unhandled, would
	 * confuse the hell out of our decompression function.
	 */
	*(uint32_t *)dest = BE_32(bufsiz);

	return (bufsiz + sizeof (bufsiz));
}

/*ARGSUSED*/
int
lz4_decompress_zfs(void *s_start, void *d_start, size_t s_len,
    size_t d_len, int n)
{
	const char *src = s_start;
	uint32_t bufsiz = BE_IN32(src);

	/* invalid compressed buffer size encoded at start */
	if (bufsiz + sizeof (bufsiz) > s_len)
		return (1);

	/*
	 * Returns 0 on success (decompression function returned non-negative)
	 * and non-zero on failure (decompression function returned negative.
	 */
	return (LZ4_decompress_safe(&src[sizeof (bufsiz)],
	    d_start, bufsiz, d_len) < 0);
}




static const int LZ4_minLength = (MFLIMIT + 1);
static const int LZ4_64Klimit = ((64 * KB) + (MFLIMIT - 1));

/*
 * Compression functions
 */

static FORCE_INLINE U32 LZ4_hash4(
	U32 sequence,
	tableType_t const tableType)
{
	if (tableType == byU16)
		return ((sequence * 2654435761U)
		    >> ((MINMATCH * 8) - (LZ4_HASHLOG + 1)));
	else
		return ((sequence * 2654435761U)
		    >> ((MINMATCH * 8) - LZ4_HASHLOG));
}

static FORCE_INLINE MAYBE_UNUSED U32 LZ4_hash5(
	U64 sequence,
	tableType_t const tableType)
{
	const U32 hashLog = (tableType == byU16)
	    ? LZ4_HASHLOG + 1
	    : LZ4_HASHLOG;

#if LZ4_LITTLE_ENDIAN
	static const U64 prime5bytes = 889523592379ULL;

	return (U32)(((sequence << 24) * prime5bytes) >> (64 - hashLog));
#else
	static const U64 prime8bytes = 11400714785074694791ULL;

	return (U32)(((sequence >> 24) * prime8bytes) >> (64 - hashLog));
#endif
}

static FORCE_INLINE U32 LZ4_hashPosition(
	const void *p,
	tableType_t const tableType)
{
#if LZ4_ARCH64
	if (tableType == byU32)
		return (LZ4_hash5(LZ4_read_ARCH(p), tableType));
#endif

	return (LZ4_hash4(LZ4_read32(p), tableType));
}

static void LZ4_putPositionOnHash(
	const BYTE *p,
	U32 h,
	void *tableBase,
	tableType_t const tableType,
	const BYTE *srcBase)
{
	switch (tableType) {
	case byPtr:
	{
		const BYTE **hashTable = (const BYTE **)tableBase;

		hashTable[h] = p;
		return;
	}
	case byU32:
	{
		U32 *hashTable = (U32 *) tableBase;

		hashTable[h] = (U32)(p - srcBase);
		return;
	}
	case byU16:
	{
		U16 *hashTable = (U16 *) tableBase;

		hashTable[h] = (U16)(p - srcBase);
		return;
	}
	}
}

static FORCE_INLINE void LZ4_putPosition(
	const BYTE *p,
	void *tableBase,
	tableType_t tableType,
	const BYTE *srcBase)
{
	U32 const h = LZ4_hashPosition(p, tableType);

	LZ4_putPositionOnHash(p, h, tableBase, tableType, srcBase);
}

static const BYTE *LZ4_getPositionOnHash(
	U32 h,
	void *tableBase,
	tableType_t tableType,
	const BYTE *srcBase)
{
	if (tableType == byPtr) {
		const BYTE **hashTable = (const BYTE **) tableBase;

		return (hashTable[h]);
	}

	if (tableType == byU32) {
		const U32 * const hashTable = (U32 *) tableBase;

		return (hashTable[h] + srcBase);
	}

	{
		/* default, to ensure a return */
		const U16 * const hashTable = (U16 *) tableBase;

		return (hashTable[h] + srcBase);
	}
}

static FORCE_INLINE const BYTE *LZ4_getPosition(
	const BYTE *p,
	void *tableBase,
	tableType_t tableType,
	const BYTE *srcBase)
{
	U32 const h = LZ4_hashPosition(p, tableType);

	return (LZ4_getPositionOnHash(h, tableBase, tableType, srcBase));
}


/*
 * LZ4_compress_generic() :
 * inlined, to ensure branches are decided at compilation time
 */
static FORCE_INLINE int LZ4_compress_generic(
	LZ4_stream_t_internal * const dictPtr,
	const char * const source,
	char * const dest,
	const int inputSize,
	const int maxOutputSize,
	const limitedOutput_directive outputLimited,
	const tableType_t tableType,
	const dict_directive dict,
	const dictIssue_directive dictIssue,
	const U32 acceleration)
{
	const BYTE *ip = (const BYTE *) source;
	const BYTE *base;
	const BYTE *lowLimit;
	const BYTE * const lowRefLimit = ip - dictPtr->dictSize;
	const BYTE * const dictionary = dictPtr->dictionary;
	const BYTE * const dictEnd = dictionary + dictPtr->dictSize;
	const size_t dictDelta = dictEnd - (const BYTE *)source;
	const BYTE *anchor = (const BYTE *) source;
	const BYTE * const iend = ip + inputSize;
	const BYTE * const mflimit = iend - MFLIMIT;
	const BYTE * const matchlimit = iend - LASTLITERALS;

	BYTE *op = (BYTE *) dest;
	BYTE * const olimit = op + maxOutputSize;

	U32 forwardH;
	size_t refDelta = 0;

	/* Init conditions */
	if ((U32)inputSize > (U32)LZ4_MAX_INPUT_SIZE) {
		/* Unsupported inputSize, too large (or negative) */
		return (0);
	}

	switch (dict) {
	case noDict:
	default:
		base = (const BYTE *)source;
		lowLimit = (const BYTE *)source;
		break;
	case withPrefix64k:
		base = (const BYTE *)source - dictPtr->currentOffset;
		lowLimit = (const BYTE *)source - dictPtr->dictSize;
		break;
	case usingExtDict:
		base = (const BYTE *)source - dictPtr->currentOffset;
		lowLimit = (const BYTE *)source;
		break;
	}

	if ((tableType == byU16) && (inputSize >= LZ4_64Klimit)) {
		/* Size too large (not within 64K limit) */
		return (0);
	}

	if (inputSize < LZ4_minLength) {
		/* Input too small, no compression (all literals) */
		goto _last_literals;
	}

	/* First Byte */
	LZ4_putPosition(ip, dictPtr->hashTable, tableType, base);
	ip++;
	forwardH = LZ4_hashPosition(ip, tableType);

	/* Main Loop */
	for (;;) {
		const BYTE *match;
		BYTE *token;

		/* Find a match */
		{
			const BYTE *forwardIp = ip;
			unsigned int step = 1;
			unsigned int searchMatchNb =
			    acceleration << LZ4_SKIPTRIGGER;

			do {
				U32 const h = forwardH;

				ip = forwardIp;
				forwardIp += step;
				step = (searchMatchNb++ >> LZ4_SKIPTRIGGER);

				if (unlikely(forwardIp > mflimit))
					goto _last_literals;

				match = LZ4_getPositionOnHash(h,
				    dictPtr->hashTable,
				    tableType, base);

				if (dict == usingExtDict) {
					if (match < (const BYTE *)source) {
						refDelta = dictDelta;
						lowLimit = dictionary;
					} else {
						refDelta = 0;
						lowLimit = (const BYTE *)source;
				}	 }

				forwardH = LZ4_hashPosition(forwardIp,
				    tableType);

				LZ4_putPositionOnHash(ip, h, dictPtr->hashTable,
				    tableType, base);
			} while (((dictIssue == dictSmall)
			    ? (match < lowRefLimit)
			    : 0) ||
			    ((tableType == byU16)
			    ? 0
			    : (match + MAX_DISTANCE < ip)) ||
			    (LZ4_read32(match + refDelta)
			    != LZ4_read32(ip)));
		}

		/* Catch up */
		while (((ip > anchor) & (match + refDelta > lowLimit)) &&
		    (unlikely(ip[-1] == match[refDelta - 1]))) {
			ip--;
			match--;
		}

		/* Encode Literals */
		{
			unsigned const int litLength =
			    (unsigned int)(ip - anchor);

			token = op++;

			if ((outputLimited) &&
				/* Check output buffer overflow */
			    (unlikely(op + litLength +
			    (2 + 1 + LASTLITERALS) +
			    (litLength / 255) > olimit)))
				return (0);

			if (litLength >= RUN_MASK) {
				int len = (int)litLength - RUN_MASK;

				*token = (RUN_MASK << ML_BITS);

				for (; len >= 255; len -= 255)
					*op++ = 255;
				*op++ = (BYTE)len;
			} else
				*token = (BYTE)(litLength << ML_BITS);

			/* Copy Literals */
			LZ4_wildCopy(op, anchor, op + litLength);
			op += litLength;
		}

_next_match:
		/* Encode Offset */
		LZ4_writeLE16(op, (U16)(ip - match));
		op += 2;

		/* Encode MatchLength */
		{
			unsigned int matchCode;

			if ((dict == usingExtDict) &&
			    (lowLimit == dictionary)) {
				const BYTE *limit;

				match += refDelta;
				limit = ip + (dictEnd - match);

				if (limit > matchlimit)
					limit = matchlimit;

				matchCode = LZ4_count(ip + MINMATCH,
				    match + MINMATCH, limit);

				ip += MINMATCH + matchCode;

				if (ip == limit) {
					unsigned const int more = LZ4_count(ip,
					    (const BYTE *)source, matchlimit);

					matchCode += more;
					ip += more;
				}
			} else {
				matchCode = LZ4_count(ip + MINMATCH,
				    match + MINMATCH, matchlimit);
				ip += MINMATCH + matchCode;
			}

			if (outputLimited &&
				/* Check output buffer overflow */
			    (unlikely(op + (1 + LASTLITERALS) +
			    (matchCode >> 8) > olimit)))
				return (0);

			if (matchCode >= ML_MASK) {
				*token += ML_MASK;
				matchCode -= ML_MASK;
				LZ4_write32(op, 0xFFFFFFFF);

				while (matchCode >= 4 * 255) {
					op += 4;
					LZ4_write32(op, 0xFFFFFFFF);
					matchCode -= 4 * 255;
				}

				op += matchCode / 255;
				*op++ = (BYTE)(matchCode % 255);
			} else
				*token += (BYTE)(matchCode);
		}

		anchor = ip;

		/* Test end of chunk */
		if (ip > mflimit)
			break;

		/* Fill table */
		LZ4_putPosition(ip - 2, dictPtr->hashTable, tableType, base);

		/* Test next position */
		match = LZ4_getPosition(ip, dictPtr->hashTable,
		    tableType, base);

		if (dict == usingExtDict) {
			if (match < (const BYTE *)source) {
				refDelta = dictDelta;
				lowLimit = dictionary;
			} else {
				refDelta = 0;
				lowLimit = (const BYTE *)source;
			}
		}

		LZ4_putPosition(ip, dictPtr->hashTable, tableType, base);

		if (((dictIssue == dictSmall) ? (match >= lowRefLimit) : 1) &&
		    (match + MAX_DISTANCE >= ip) &&
		    (LZ4_read32(match + refDelta) == LZ4_read32(ip))) {
			token = op++;
			*token = 0;
			goto _next_match;
		}

		/* Prepare next loop */
		forwardH = LZ4_hashPosition(++ip, tableType);
	}

_last_literals:
	/* Encode Last Literals */
	{
		size_t const lastRun = (size_t)(iend - anchor);

		if ((outputLimited) &&
			/* Check output buffer overflow */
		    ((op - (BYTE *)dest) + lastRun + 1 +
		    ((lastRun + 255 - RUN_MASK) / 255) > (U32)maxOutputSize))
			return (0);

		if (lastRun >= RUN_MASK) {
			size_t accumulator = lastRun - RUN_MASK;
			*op++ = RUN_MASK << ML_BITS;
			for (; accumulator >= 255; accumulator -= 255)
				*op++ = 255;
			*op++ = (BYTE) accumulator;
		} else {
			*op++ = (BYTE)(lastRun << ML_BITS);
		}

		memcpy(op, anchor, lastRun);

		op += lastRun;
	}

	/* End */
	return ((int)(((char *)op) - dest));
}

static int LZ4_compress_fast_extState(
	void *state,
	const char *source,
	char *dest,
	int inputSize,
	int maxOutputSize,
	int acceleration)
{
	LZ4_stream_t_internal *ctx =
	    &((LZ4_stream_t *)state)->internal_donotuse;
#if LZ4_ARCH64
	const tableType_t tableType = byU32;
#else
	const tableType_t tableType = byPtr;
#endif

	LZ4_resetStream((LZ4_stream_t *)state);

	if (acceleration < 1)
		acceleration = LZ4_ACCELERATION_DEFAULT;

	if (maxOutputSize >= LZ4_COMPRESSBOUND(inputSize)) {
		if (inputSize < LZ4_64Klimit)
			return (LZ4_compress_generic(ctx, source,
			    dest, inputSize, 0,
			    noLimit, byU16, noDict,
			    noDictIssue, acceleration));
		else
			return (LZ4_compress_generic(ctx, source,
			    dest, inputSize, 0,
			    noLimit, tableType, noDict,
			    noDictIssue, acceleration));
	} else {
		if (inputSize < LZ4_64Klimit)
			return (LZ4_compress_generic(ctx, source,
			    dest, inputSize,
			    maxOutputSize, limitedOutput, byU16, noDict,
			    noDictIssue, acceleration));
		else
			return (LZ4_compress_generic(ctx, source,
			    dest, inputSize,
			    maxOutputSize, limitedOutput, tableType, noDict,
			    noDictIssue, acceleration));
	}
}

int LZ4_compress_fast(const char *source, char *dest, int inputSize,
	int maxOutputSize, int acceleration, void *wrkmem)
{
	return (LZ4_compress_fast_extState(wrkmem, source, dest, inputSize,
	    maxOutputSize, acceleration));
}

int LZ4_compress_default(const char *source, char *dest, int inputSize,
	int maxOutputSize, void *wrkmem)
{
	return (LZ4_compress_fast(source, dest, inputSize,
	    maxOutputSize, LZ4_ACCELERATION_DEFAULT, wrkmem));
}

/*
 * _destSize() variant
 */
static int LZ4_compress_destSize_generic(
	LZ4_stream_t_internal * const ctx,
	const char * const src,
	char * const dst,
	int * const srcSizePtr,
	const int targetDstSize,
	const tableType_t tableType)
{
	const BYTE *ip = (const BYTE *) src;
	const BYTE *base = (const BYTE *) src;
	const BYTE *lowLimit = (const BYTE *) src;
	const BYTE *anchor = ip;
	const BYTE * const iend = ip + *srcSizePtr;
	const BYTE * const mflimit = iend - MFLIMIT;
	const BYTE * const matchlimit = iend - LASTLITERALS;

	BYTE *op = (BYTE *) dst;
	BYTE * const oend = op + targetDstSize;
	BYTE * const oMaxLit = op + targetDstSize - 2 /* offset */
	    - 8 /* because 8 + MINMATCH == MFLIMIT */ - 1 /* token */;
	BYTE * const oMaxMatch = op + targetDstSize
	    - (LASTLITERALS + 1 /* token */);
	BYTE * const oMaxSeq = oMaxLit - 1 /* token */;

	U32 forwardH;

	/* Init conditions */
	/* Impossible to store anything */
	if (targetDstSize < 1)
		return (0);
	/* Unsupported input size, too large (or negative) */
	if ((U32)*srcSizePtr > (U32)LZ4_MAX_INPUT_SIZE)
		return (0);
	/* Size too large (not within 64K limit) */
	if ((tableType == byU16) && (*srcSizePtr >= LZ4_64Klimit))
		return (0);
	/* Input too small, no compression (all literals) */
	if (*srcSizePtr < LZ4_minLength)
		goto _last_literals;

	/* First Byte */
	*srcSizePtr = 0;
	LZ4_putPosition(ip, ctx->hashTable, tableType, base);
	ip++; forwardH = LZ4_hashPosition(ip, tableType);

	/* Main Loop */
	for (;;) {
		const BYTE *match;
		BYTE *token;

		/* Find a match */
		{
			const BYTE *forwardIp = ip;
			unsigned int step = 1;
			unsigned int searchMatchNb = 1 << LZ4_SKIPTRIGGER;

			do {
				U32 h = forwardH;

				ip = forwardIp;
				forwardIp += step;
				step = (searchMatchNb++ >> LZ4_SKIPTRIGGER);

				if (unlikely(forwardIp > mflimit))
					goto _last_literals;

				match = LZ4_getPositionOnHash(h, ctx->hashTable,
				    tableType, base);
				forwardH = LZ4_hashPosition(forwardIp,
				    tableType);
				LZ4_putPositionOnHash(ip, h, ctx->hashTable,
				    tableType, base);

			} while (((tableType == byU16)
			    ? 0
			    : (match + MAX_DISTANCE < ip)) ||
			    (LZ4_read32(match) != LZ4_read32(ip)));
		}

		/* Catch up */
		while ((ip > anchor) &&
		    (match > lowLimit) &&
		    (unlikely(ip[-1] == match[-1]))) {
			ip--;
			match--;
		}

		/* Encode Literal length */
		{
			unsigned int litLength = (unsigned int)(ip - anchor);

			token = op++;
			if (op + ((litLength + 240)/255)
			    + litLength > oMaxLit) {
				/* Not enough space for a last match */
				op--;
				goto _last_literals;
			}
			if (litLength >= RUN_MASK) {
				unsigned int len = litLength - RUN_MASK;
				*token = (RUN_MASK<<ML_BITS);
				for (; len >= 255; len -= 255)
					*op++ = 255;
				*op++ = (BYTE)len;
			} else
				*token = (BYTE)(litLength<<ML_BITS);

			/* Copy Literals */
			LZ4_wildCopy(op, anchor, op + litLength);
			op += litLength;
		}

_next_match:
		/* Encode Offset */
		LZ4_writeLE16(op, (U16)(ip - match)); op += 2;

		/* Encode MatchLength */
		{
			size_t matchLength = LZ4_count(ip + MINMATCH,
			    match + MINMATCH, matchlimit);

			if (op + ((matchLength + 240)/255) > oMaxMatch) {
				/* Match description too long : reduce it */
				matchLength = (15 - 1) + (oMaxMatch - op) * 255;
			}
			ip += MINMATCH + matchLength;

			if (matchLength >= ML_MASK) {
				*token += ML_MASK;
				matchLength -= ML_MASK;
				while (matchLength >= 255) {
					matchLength -= 255; *op++ = 255;
				}
				*op++ = (BYTE)matchLength;
			} else
				*token += (BYTE)(matchLength);
		}

		anchor = ip;

		/* Test end of block */
		if (ip > mflimit)
			break;
		if (op > oMaxSeq)
			break;

		/* Fill table */
		LZ4_putPosition(ip - 2, ctx->hashTable, tableType, base);

		/* Test next position */
		match = LZ4_getPosition(ip, ctx->hashTable, tableType, base);
		LZ4_putPosition(ip, ctx->hashTable, tableType, base);

		if ((match + MAX_DISTANCE >= ip) &&
		    (LZ4_read32(match) == LZ4_read32(ip))) {
			token = op++; *token = 0;
			goto _next_match;
		}

		/* Prepare next loop */
		forwardH = LZ4_hashPosition(++ip, tableType);
	}

_last_literals:
	/* Encode Last Literals */
	{
		size_t lastRunSize = (size_t)(iend - anchor);

		if (op + 1 /* token */
		    + ((lastRunSize + 240) / 255) /* litLength */
		    + lastRunSize /* literals */ > oend) {
			/* adapt lastRunSize to fill 'dst' */
			lastRunSize  = (oend - op) - 1;
			lastRunSize -= (lastRunSize + 240) / 255;
		}
		ip = anchor + lastRunSize;

		if (lastRunSize >= RUN_MASK) {
			size_t accumulator = lastRunSize - RUN_MASK;

			*op++ = RUN_MASK << ML_BITS;
			for (; accumulator >= 255; accumulator -= 255)
				*op++ = 255;
			*op++ = (BYTE) accumulator;
		} else {
			*op++ = (BYTE)(lastRunSize<<ML_BITS);
		}
		memcpy(op, anchor, lastRunSize);
		op += lastRunSize;
	}

	/* End */
	*srcSizePtr = (int)(((const char *)ip) - src);
	return ((int)(((char *)op) - dst));
}

static int LZ4_compress_destSize_extState(LZ4_stream_t *state, const char *src,
	char *dst, int *srcSizePtr, int targetDstSize)
{
#if LZ4_ARCH64
	const tableType_t tableType = byU32;
#else
	const tableType_t tableType = byPtr;
#endif

	LZ4_resetStream(state);

	if (targetDstSize >= LZ4_COMPRESSBOUND(*srcSizePtr)) {
		/* compression success is guaranteed */
		return LZ4_compress_fast_extState(state, src, dst, *srcSizePtr,
		    targetDstSize, 1);
	} else {
		if (*srcSizePtr < LZ4_64Klimit)
			return (LZ4_compress_destSize_generic(
			    &state->internal_donotuse,
			    src, dst, srcSizePtr,
			    targetDstSize, byU16));
		else
			return (LZ4_compress_destSize_generic(
			    &state->internal_donotuse,
			    src, dst, srcSizePtr,
			    targetDstSize, tableType));
	}
}


int LZ4_compress_destSize(const char *src, char *dst, int *srcSizePtr,
	int targetDstSize, void *wrkmem)
{
	return (LZ4_compress_destSize_extState(wrkmem, src, dst, srcSizePtr,
	    targetDstSize));
}

/*
 * Streaming functions
 */

void
LZ4_resetStream(LZ4_stream_t *LZ4_stream)
{
	memset(LZ4_stream, 0, sizeof (LZ4_stream_t));
}

#define	HASH_UNIT sizeof (size_t)
int LZ4_loadDict(LZ4_stream_t *LZ4_dict,
	const char *dictionary, int dictSize)
{
	LZ4_stream_t_internal *dict = &LZ4_dict->internal_donotuse;
	const BYTE *p = (const BYTE *)dictionary;
	const BYTE * const dictEnd = p + dictSize;
	const BYTE *base;

	if ((dict->initCheck) ||
	    (dict->currentOffset > 1 * GB)) {
		/* Uninitialized structure, or reuse overflow */
		LZ4_resetStream(LZ4_dict);
	}

	if (dictSize < (int)HASH_UNIT) {
		dict->dictionary = NULL;
		dict->dictSize = 0;
		return (0);
	}

	if ((dictEnd - p) > 64 * KB)
		p = dictEnd - 64 * KB;
	dict->currentOffset += 64 * KB;
	base = p - dict->currentOffset;
	dict->dictionary = p;
	dict->dictSize = (U32)(dictEnd - p);
	dict->currentOffset += dict->dictSize;

	while (p <= dictEnd - HASH_UNIT) {
		LZ4_putPosition(p, dict->hashTable, byU32, base);
		p += 3;
	}

	return (dict->dictSize);
}

static void LZ4_renormDictT(LZ4_stream_t_internal *LZ4_dict,
	const BYTE *src)
{
	if ((LZ4_dict->currentOffset > 0x80000000) ||
	    ((uptrval)LZ4_dict->currentOffset > (uptrval)src)) {
		/* address space overflow */
		/* rescale hash table */
		U32 const delta = LZ4_dict->currentOffset - 64 * KB;
		const BYTE *dictEnd = LZ4_dict->dictionary + LZ4_dict->dictSize;
		int i;

		for (i = 0; i < LZ4_HASH_SIZE_U32; i++) {
			if (LZ4_dict->hashTable[i] < delta)
				LZ4_dict->hashTable[i] = 0;
			else
				LZ4_dict->hashTable[i] -= delta;
		}
		LZ4_dict->currentOffset = 64 * KB;
		if (LZ4_dict->dictSize > 64 * KB)
			LZ4_dict->dictSize = 64 * KB;
		LZ4_dict->dictionary = dictEnd - LZ4_dict->dictSize;
	}
}

int
LZ4_saveDict(LZ4_stream_t *LZ4_dict, char *safeBuffer, int dictSize)
{
	LZ4_stream_t_internal * const dict = &LZ4_dict->internal_donotuse;
	const BYTE * const previousDictEnd = dict->dictionary + dict->dictSize;

	if ((U32)dictSize > 64 * KB) {
		/* useless to define a dictionary > 64 * KB */
		dictSize = 64 * KB;
	}
	if ((U32)dictSize > dict->dictSize)
		dictSize = dict->dictSize;

	memmove(safeBuffer, previousDictEnd - dictSize, dictSize);

	dict->dictionary = (const BYTE *)safeBuffer;
	dict->dictSize = (U32)dictSize;

	return (dictSize);
}

int LZ4_compress_fast_continue(LZ4_stream_t *LZ4_stream, const char *source,
	char *dest, int inputSize, int maxOutputSize, int acceleration)
{
	LZ4_stream_t_internal *streamPtr = &LZ4_stream->internal_donotuse;
	const BYTE * const dictEnd =
	    streamPtr->dictionary + streamPtr->dictSize;

	const BYTE *smallest = (const BYTE *) source;

	if (streamPtr->initCheck) {
		/* Uninitialized structure detected */
		return (0);
	}

	if ((streamPtr->dictSize > 0) && (smallest > dictEnd))
		smallest = dictEnd;

	LZ4_renormDictT(streamPtr, smallest);

	if (acceleration < 1)
		acceleration = LZ4_ACCELERATION_DEFAULT;

	/* Check overlapping input/dictionary space */
	{
		const BYTE *sourceEnd = (const BYTE *) source + inputSize;

		if ((sourceEnd > streamPtr->dictionary) &&
		    (sourceEnd < dictEnd)) {
			streamPtr->dictSize = (U32)(dictEnd - sourceEnd);
			if (streamPtr->dictSize > 64 * KB)
				streamPtr->dictSize = 64 * KB;
			if (streamPtr->dictSize < 4)
				streamPtr->dictSize = 0;
			streamPtr->dictionary = dictEnd - streamPtr->dictSize;
		}
	}

	/* prefix mode : source data follows dictionary */
	if (dictEnd == (const BYTE *)source) {
		int result;

		if ((streamPtr->dictSize < 64 * KB) &&
		    (streamPtr->dictSize < streamPtr->currentOffset)) {
			result = LZ4_compress_generic(
			    streamPtr, source, dest, inputSize,
			    maxOutputSize, limitedOutput, byU32,
			    withPrefix64k, dictSmall, acceleration);
		} else {
			result = LZ4_compress_generic(
			    streamPtr, source, dest, inputSize,
			    maxOutputSize, limitedOutput, byU32,
			    withPrefix64k, noDictIssue, acceleration);
		}
		streamPtr->dictSize += (U32)inputSize;
		streamPtr->currentOffset += (U32)inputSize;
		return (result);
	}

	/* external dictionary mode */
	{
		int result;

		if ((streamPtr->dictSize < 64 * KB) &&
		    (streamPtr->dictSize < streamPtr->currentOffset)) {
			result = LZ4_compress_generic(
			    streamPtr, source, dest, inputSize,
			    maxOutputSize, limitedOutput, byU32,
			    usingExtDict, dictSmall, acceleration);
		} else {
			result = LZ4_compress_generic(
			    streamPtr, source, dest, inputSize,
			    maxOutputSize, limitedOutput, byU32,
			    usingExtDict, noDictIssue, acceleration);
		}
		streamPtr->dictionary = (const BYTE *)source;
		streamPtr->dictSize = (U32)inputSize;
		streamPtr->currentOffset += (U32)inputSize;
		return (result);
	}
}



/*
 * Decompression functions
 */

/*
 * LZ4_decompress_generic() :
 * This generic decompression function cover all use cases.
 * It shall be instantiated several times, using different sets of directives
 * Note that it is important this generic function is really inlined,
 * in order to remove useless branches during compilation optimization.
 */
static FORCE_INLINE int LZ4_decompress_generic(
    const char * const source,
    char * const dest,
    int inputSize,
	/*
	 * If endOnInput == endOnInputSize,
	 * this value is the max size of Output Buffer.
	 */
    int outputSize,
	/* endOnOutputSize, endOnInputSize */
    int endOnInput,
	/* full, partial */
    int partialDecoding,
	/* only used if partialDecoding == partial */
    int targetOutputSize,
	/* noDict, withPrefix64k, usingExtDict */
    int dict,
	/* == dest when no prefix */
    const BYTE * const lowPrefix,
	/* only if dict == usingExtDict */
    const BYTE * const dictStart,
	/* note : = 0 if noDict */
    const size_t dictSize)
{
	/* Local Variables */
	const BYTE *ip = (const BYTE *) source;
	const BYTE * const iend = ip + inputSize;

	BYTE *op = (BYTE *) dest;
	BYTE * const oend = op + outputSize;
	BYTE *cpy;
	BYTE *oexit = op + targetOutputSize;
	const BYTE * const lowLimit = lowPrefix - dictSize;

	const BYTE * const dictEnd = (const BYTE *)dictStart + dictSize;
	const unsigned int dec32table[] = {0, 1, 2, 1, 4, 4, 4, 4};
	const int dec64table[] = {0, 0, 0, -1, 0, 1, 2, 3};

	const int safeDecode = (endOnInput == endOnInputSize);
	const int checkOffset = ((safeDecode) && (dictSize < (int)(64 * KB)));

	/* Special cases */
	/* targetOutputSize too high => decode everything */
	if ((partialDecoding) && (oexit > oend - MFLIMIT))
		oexit = oend - MFLIMIT;

	/* Empty output buffer */
	if ((endOnInput) && (unlikely(outputSize == 0)))
		return (((inputSize == 1) && (*ip == 0)) ? 0 : -1);

	if ((!endOnInput) && (unlikely(outputSize == 0)))
		return (*ip == 0 ? 1 : -1);

	/* Main Loop : decode sequences */
	while (1) {
		size_t length;
		const BYTE *match;
		size_t offset;

		/* get literal length */
		unsigned int const token = *ip++;

		length = token>>ML_BITS;

		if (length == RUN_MASK) {
			unsigned int s;

			do {
				s = *ip++;
				length += s;
			} while (likely(endOnInput
			    ? ip < iend - RUN_MASK
			    : 1) & (s == 255));

			if ((safeDecode) && unlikely(
			    (size_t)(op + length) < (size_t)(op))) {
				/* overflow detection */
				goto _output_error;
			}
			if ((safeDecode) && unlikely(
			    (size_t)(ip + length) < (size_t)(ip))) {
				/* overflow detection */
				goto _output_error;
			}
		}

		/* copy literals */
		cpy = op + length;
		if (((endOnInput) &&
		    ((cpy > (partialDecoding ? oexit : oend - MFLIMIT)) ||
		    (ip + length > iend - (2 + 1 + LASTLITERALS)))) ||
		    ((!endOnInput) && (cpy > oend - WILDCOPYLENGTH))) {
			if (partialDecoding) {
				if (cpy > oend) {
					/*
					 * Error :
					 * write attempt beyond end
					 * of output buffer
					 */
					goto _output_error;
				}
				if ((endOnInput) && (ip + length > iend)) {
					/*
					 * Error :
					 * read attempt beyond
					 * end of input buffer
					 */
					goto _output_error;
				}
			} else {
				if ((!endOnInput) && (cpy != oend)) {
					/*
					 * Error :
					 * block decoding must
					 * stop exactly there
					 */
					goto _output_error;
				}
				if ((endOnInput) && ((ip + length != iend) ||
				    (cpy > oend))) {
					/*
					 * Error :
					 * input must be consumed
					 */
					goto _output_error;
				}
			}

			memcpy(op, ip, length);
			ip += length;
			op += length;
			/* Necessarily EOF, due to parsing restrictions */
			break;
		}

		LZ4_wildCopy(op, ip, cpy);
		ip += length;
		op = cpy;

		/* get offset */
		offset = LZ4_readLE16(ip);
		ip += 2;
		match = op - offset;

		if ((checkOffset) && (unlikely(match < lowLimit))) {
			/* Error : offset outside buffers */
			goto _output_error;
		}

		/* costs ~1%; silence an msan warning when offset == 0 */
		LZ4_write32(op, (U32)offset);

		/* get matchlength */
		length = token & ML_MASK;
		if (length == ML_MASK) {
			unsigned int s;

			do {
				s = *ip++;

				if ((endOnInput) && (ip > iend - LASTLITERALS))
					goto _output_error;

				length += s;
			} while (s == 255);

			if ((safeDecode) &&
			    unlikely((size_t)(op + length) < (size_t)op)) {
				/* overflow detection */
				goto _output_error;
			}
		}

		length += MINMATCH;

		/* check external dictionary */
		if ((dict == usingExtDict) && (match < lowPrefix)) {
			if (unlikely(op + length > oend - LASTLITERALS)) {
				/* doesn't respect parsing restriction */
				goto _output_error;
			}

			if (length <= (size_t)(lowPrefix - match)) {
				/*
				 * match can be copied as a single segment
				 * from external dictionary
				 */
				memmove(op, dictEnd - (lowPrefix - match),
				    length);
				op += length;
			} else {
				/*
				 * match encompass external
				 * dictionary and current block
				 */
				size_t const copySize =
				    (size_t)(lowPrefix - match);
				size_t const restSize = length - copySize;

				memcpy(op, dictEnd - copySize, copySize);
				op += copySize;

				if (restSize > (size_t)(op - lowPrefix)) {
					/* overlap copy */
					BYTE * const endOfMatch = op + restSize;
					const BYTE *copyFrom = lowPrefix;

					while (op < endOfMatch)
						*op++ = *copyFrom++;
				} else {
					memcpy(op, lowPrefix, restSize);
					op += restSize;
				}
			}

			continue;
		}

		/* copy match within block */
		cpy = op + length;

		if (unlikely(offset < 8)) {
			const int dec64 = dec64table[offset];

			op[0] = match[0];
			op[1] = match[1];
			op[2] = match[2];
			op[3] = match[3];
			match += dec32table[offset];
			memcpy(op + 4, match, 4);
			match -= dec64;
		} else {
			LZ4_copy8(op, match);
			match += 8;
		}

		op += 8;

		if (unlikely(cpy > oend - 12)) {
			BYTE * const oCopyLimit = oend - (WILDCOPYLENGTH - 1);

			if (cpy > oend - LASTLITERALS) {
				/*
				 * Error : last LASTLITERALS bytes
				 * must be literals (uncompressed)
				 */
				goto _output_error;
			}

			if (op < oCopyLimit) {
				LZ4_wildCopy(op, match, oCopyLimit);
				match += oCopyLimit - op;
				op = oCopyLimit;
			}

			while (op < cpy)
				*op++ = *match++;
		} else {
			LZ4_copy8(op, match);

			if (length > 16)
				LZ4_wildCopy(op + 8, match + 8, cpy);
		}

		op = cpy; /* correction */
	}

	/* end of decoding */
	if (endOnInput) {
		/* Nb of output bytes decoded */
		return ((int)(((char *)op) - dest));
	} else {
		/* Nb of input bytes read */
		return ((int)(((const char *)ip) - source));
	}

	/* Overflow error detected */
_output_error:
	return (-1);
}

int LZ4_decompress_safe(const char *source, char *dest,
	int compressedSize, int maxDecompressedSize)
{
	return (LZ4_decompress_generic(source, dest, compressedSize,
	    maxDecompressedSize, endOnInputSize, full, 0,
	    noDict, (BYTE *)dest, NULL, 0));
}

int LZ4_decompress_safe_partial(const char *source, char *dest,
	int compressedSize, int targetOutputSize, int maxDecompressedSize)
{
	return (LZ4_decompress_generic(source, dest, compressedSize,
	    maxDecompressedSize, endOnInputSize, partial,
	    targetOutputSize, noDict, (BYTE *)dest, NULL, 0));
}

int
LZ4_decompress_fast(const char *source, char *dest, int originalSize)
{
	return (LZ4_decompress_generic(source, dest, 0, originalSize,
	    endOnOutputSize, full, 0, withPrefix64k,
	    (BYTE *)(dest - 64 * KB), NULL, 64 * KB));
}

int LZ4_setStreamDecode(LZ4_streamDecode_t *LZ4_streamDecode,
	const char *dictionary, int dictSize)
{
	LZ4_streamDecode_t_internal *lz4sd =
	    (LZ4_streamDecode_t_internal *) LZ4_streamDecode;

	lz4sd->prefixSize = (size_t)dictSize;
	lz4sd->prefixEnd = (const BYTE *) dictionary + dictSize;
	lz4sd->externalDict = NULL;
	lz4sd->extDictSize	= 0;
	return (1);
}

/*
 * *_continue() :
 * These decoding functions allow decompression of multiple blocks
 * in "streaming" mode.
 * Previously decoded blocks must still be available at the memory
 * position where they were decoded.
 * If it's not possible, save the relevant part of
 * decoded data into a safe buffer,
 * and indicate where it stands using LZ4_setStreamDecode()
 */
int LZ4_decompress_safe_continue(LZ4_streamDecode_t *LZ4_streamDecode,
	const char *source, char *dest, int compressedSize, int maxOutputSize)
{
	LZ4_streamDecode_t_internal *lz4sd =
	    &LZ4_streamDecode->internal_donotuse;
	int result;

	if (lz4sd->prefixEnd == (BYTE *)dest) {
		result = LZ4_decompress_generic(source, dest,
		    compressedSize,
		    maxOutputSize,
		    endOnInputSize, full, 0,
		    usingExtDict, lz4sd->prefixEnd - lz4sd->prefixSize,
		    lz4sd->externalDict,
		    lz4sd->extDictSize);

		if (result <= 0)
			return (result);

		lz4sd->prefixSize += result;
		lz4sd->prefixEnd	+= result;
	} else {
		lz4sd->extDictSize = lz4sd->prefixSize;
		lz4sd->externalDict = lz4sd->prefixEnd - lz4sd->extDictSize;
		result = LZ4_decompress_generic(source, dest,
		    compressedSize, maxOutputSize,
		    endOnInputSize, full, 0,
		    usingExtDict, (BYTE *)dest,
		    lz4sd->externalDict, lz4sd->extDictSize);
		if (result <= 0)
			return (result);
		lz4sd->prefixSize = result;
		lz4sd->prefixEnd  = (BYTE *)dest + result;
	}

	return (result);
}

int LZ4_decompress_fast_continue(LZ4_streamDecode_t *LZ4_streamDecode,
	const char *source, char *dest, int originalSize)
{
	LZ4_streamDecode_t_internal *lz4sd =
	    &LZ4_streamDecode->internal_donotuse;
	int result;

	if (lz4sd->prefixEnd == (BYTE *)dest) {
		result = LZ4_decompress_generic(source, dest, 0, originalSize,
		    endOnOutputSize, full, 0,
		    usingExtDict,
		    lz4sd->prefixEnd - lz4sd->prefixSize,
		    lz4sd->externalDict, lz4sd->extDictSize);

		if (result <= 0)
			return (result);

		lz4sd->prefixSize += originalSize;
		lz4sd->prefixEnd  += originalSize;
	} else {
		lz4sd->extDictSize = lz4sd->prefixSize;
		lz4sd->externalDict = lz4sd->prefixEnd - lz4sd->extDictSize;
		result = LZ4_decompress_generic(source, dest, 0, originalSize,
		    endOnOutputSize, full, 0,
		    usingExtDict, (BYTE *)dest,
		    lz4sd->externalDict, lz4sd->extDictSize);
		if (result <= 0)
			return (result);
		lz4sd->prefixSize = originalSize;
		lz4sd->prefixEnd  = (BYTE *)dest + originalSize;
	}

	return (result);
}

/*
 * Advanced decoding functions :
 * *_usingDict() :
 * These decoding functions work the same as "_continue" ones,
 * the dictionary must be explicitly provided within parameters
 */
static FORCE_INLINE int LZ4_decompress_usingDict_generic(const char *source,
	char *dest, int compressedSize, int maxOutputSize, int safe,
	const char *dictStart, int dictSize)
{
	if (dictSize == 0)
		return (LZ4_decompress_generic(source, dest,
		    compressedSize, maxOutputSize, safe, full, 0,
		    noDict, (BYTE *)dest, NULL, 0));
	if (dictStart + dictSize == dest) {
		if (dictSize >= (int)(64 * KB - 1))
			return (LZ4_decompress_generic(source, dest,
			    compressedSize, maxOutputSize, safe, full, 0,
			    withPrefix64k, (BYTE *)dest - 64 * KB, NULL, 0));
		return (LZ4_decompress_generic(source, dest, compressedSize,
		    maxOutputSize, safe, full, 0, noDict,
		    (BYTE *)dest - dictSize, NULL, 0));
	}
	return (LZ4_decompress_generic(source, dest, compressedSize,
	    maxOutputSize, safe, full, 0, usingExtDict,
	    (BYTE *)dest, (const BYTE *)dictStart, dictSize));
}

int LZ4_decompress_safe_usingDict(const char *source, char *dest,
	int compressedSize, int maxOutputSize,
	const char *dictStart, int dictSize)
{
	return (LZ4_decompress_usingDict_generic(source, dest,
	    compressedSize, maxOutputSize, 1, dictStart, dictSize));
}

int LZ4_decompress_fast_usingDict(const char *source, char *dest,
	int originalSize, const char *dictStart, int dictSize)
{
	return (LZ4_decompress_usingDict_generic(source, dest, 0,
	    originalSize, 0, dictStart, dictSize));
}
