/*
 * LZ4 HC - High Compression Mode of LZ4
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

#include <sys/lz4_impl.h>

/* Header */

/*
 * ZFS changes:
 *	- stream compression removed
 *	- LZ4HC_DEBUG removed
 *	- MSVC-specific code removed
 */

/*
 * LZ4_compress_HC :
 *	Destination buffer 'dst' must be already allocated.
 *	Compression completion is guaranteed if 'dst' buffer is sized
 *	to handle worst circumstances (data not compressible)
 *	Worst size evaluation is provided by function LZ4_compressBound()
 *	    srcSize : Max supported value is LZ4_MAX_INPUT_SIZE
 *	    compressionLevel : Recommended values are between 4 and 9,
 *				although any value between 1 and 16 will work.
 *	    return : the number of bytes written into buffer 'dst'
 *		    or 0 if compression fails.
 *	ZFS changes : compression level checking replaced with an assertion;
 *			default value is set in module/zcommon/zfs_prop.c
 */
static int
LZ4_compress_HC(const char *src, char *dst, int srcSize, int maxDstSize,
    int compressionLevel);

/*
 * Note :
 *   Decompression functions are provided within LZ4 source code (BSD license)
 */

/*
 * LZ4_compress_HC_extStateHC() :
 *	Use this function if you prefer to manually allocate memory
 *	for compression tables.  To know how much memory must be
 *	allocated for the compression tables, use:
 *		int LZ4_sizeofStateHC();
 *
 *	Allocated memory must be aligned on 8-bytes boundaries
 *	(which kmem_cache_alloc() will do properly).
 *
 *	The allocated memory can then be provided to the compression functions
 *	using 'void* state' parameter.
 *	LZ4_compress_HC_extStateHC() is equivalent to previously
 *	described function.  It just uses externally allocated memory
 *	for stateHC.
 *
 *	ZFS Changes : state alignment checking replaced with an assertion
 */
static int
LZ4_compress_HC_extStateHC(void *state, const char *src, char *dst,
    int srcSize, int maxDstSize, int compressionLevel);

#define	LZ4_MAX_INPUT_SIZE	0x7E000000	/* 2 113 929 216 bytes */
#define	LZ4_COMPRESSBOUND(isize) \
	((unsigned)(isize) > (unsigned)LZ4_MAX_INPUT_SIZE ? \
	0 : (isize) + ((isize)/255) + 16)

/*
 * LZ4_compressBound() :
 *	Provides the maximum size that LZ4 compression may output in a
 *	"worst case" scenario (input data not compressible)
 *	This function is primarily useful for memory allocation
 *	purposes (destination buffer size).
 *	Macro LZ4_COMPRESSBOUND() is also provided for
 *	compilation-time evaluation (stack memory allocation for example).
 *	Note that LZ4_compress_default() compress faster when dest
 *	buffer size is >= LZ4_compressBound(srcSize)
 *		inputSize  : max supported value is LZ4_MAX_INPUT_SIZE
 *		return : maximum output size in a "worst case" scenario
 *			or 0, if input size is too large ( > LZ4_MAX_INPUT_SIZE)
 */
static int
LZ4_compressBound(int inputSize);


static kmem_cache_t *lz4hc_cache;

size_t
lz4hc_compress_zfs(void *src, void *dst, size_t s_len, size_t d_len, int level)
{
	uint32_t bufsiz;
	char *dest = dst;

	ASSERT(d_len >= sizeof (bufsiz));

	bufsiz = LZ4_compress_HC(src, &dest[sizeof (bufsiz)], s_len,
	    d_len - sizeof (bufsiz), level);

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

/* Common LZ4 definition */

/* Compiler Options */
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)	/* C99 */
#if defined(__GNUC__) || defined(__clang__)
#define	FORCE_INLINE static inline __attribute__((always_inline))
#else
#define	FORCE_INLINE static inline
#endif
#else
#define	FORCE_INLINE static
#endif	/* __STDC_VERSION__ */

/* Memory routines */
#define	MEM_INIT	memset

static U16
LZ4_read16(const void *memPtr)
{
	U16 val16;
	(void) memcpy(&val16, memPtr, 2);
	return (val16);
}

static void
LZ4_writeLE16(void *memPtr, U16 value)
{
	if (LZ4_isLittleEndian()) {
		(void) memcpy(memPtr, &value, 2);
	} else {
		BYTE *p = (BYTE *)memPtr;
		p[0] = (BYTE)value;
		p[1] = (BYTE)(value >> 8);
	}
}

static U32
LZ4_read32(const void *memPtr)
{
	U32 val32;
	(void) memcpy(&val32, memPtr, 4);
	return (val32);
}

static U64
LZ4_read64(const void *memPtr)
{
	U64 val64;
	(void) memcpy(&val64, memPtr, 8);
	return (val64);
}

static size_t
LZ4_read_ARCH(const void *p)
{
	if (LZ4_64bits())
		return ((size_t)LZ4_read64(p));
	else
		return ((size_t)LZ4_read32(p));
}

static void
LZ4_copy8(void *dstPtr, const void *srcPtr)
{
	(void) memcpy(dstPtr, srcPtr, 8);
}

/*
 * customized version of memcpy,
 * which may overwrite up to 7 bytes beyond dstEnd
 */
static void
LZ4_wildCopy(void *dstPtr, const void *srcPtr, void *dstEnd)
{
	BYTE *d = (BYTE *)dstPtr;
	const BYTE *s = (const BYTE *)srcPtr;
	BYTE *e = (BYTE *)dstEnd;
	do {
		LZ4_copy8(d, s);
		d += 8;
		s += 8;
	} while (d < e);
}

/* Common Constants */
#define	KB *(1 <<10)
#define	MB *(1 <<20)
#define	GB *(1U<<30)

/* Common functions */
static unsigned
LZ4_count(const BYTE *pIn, const BYTE *pMatch,
    const BYTE *pInLimit)
{
	const BYTE *const pStart = pIn;

	while (likely(pIn < pInLimit - (STEPSIZE - 1))) {
		size_t diff = LZ4_read_ARCH(pMatch) ^ LZ4_read_ARCH(pIn);
		if (!diff) {
			pIn += STEPSIZE;
			pMatch += STEPSIZE;
			continue;
		}
		pIn += LZ4_NbCommonBytes(diff);
		return ((unsigned)(pIn - pStart));
	}

	if (LZ4_64bits())
		if ((pIn < (pInLimit - 3)) &&
		    (LZ4_read32(pMatch) == LZ4_read32(pIn))) {
			pIn += 4;
			pMatch += 4;
		}
	if ((pIn < (pInLimit - 1)) && (LZ4_read16(pMatch) == LZ4_read16(pIn))) {
		pIn += 2;
		pMatch += 2;
	}
	if ((pIn < pInLimit) && (*pMatch == *pIn))
		pIn++;
	return ((unsigned)(pIn - pStart));
}

static int
LZ4_compressBound(int isize)
{
	return (LZ4_COMPRESSBOUND(isize));
}

/* END  Common LZ4 definition */

/* Local Constants */
#define	DICTIONARY_LOGSIZE 16
#define	MAXD (1<<DICTIONARY_LOGSIZE)
#define	MAXD_MASK (MAXD - 1)

#define	HASH_LOG (DICTIONARY_LOGSIZE-1)
#define	HASHTABLESIZE (1 << HASH_LOG)
#define	HASH_MASK (HASHTABLESIZE - 1)

#define	OPTIMAL_ML (int)((ML_MASK-1)+MINMATCH)

static const int g_maxCompressionLevel = 16;

/* Local Types */
typedef struct {
	U32 hashTable[HASHTABLESIZE];
	U16 chainTable[MAXD];
	const BYTE *end;	/* next block here to continue */
				/*   on current prefix */
	const BYTE *base;	/* All index relative to this position */
	const BYTE *dictBase;	/* alternate base for extDict */
	BYTE *inputBuffer;	/* deprecated */
	U32 dictLimit;		/* below that point, need extDict */
	U32 lowLimit;		/* below that point, no more dict */
	U32 nextToUpdate;	/* index from which to continue */
				/*   dictionary update */
	U32 compressionLevel;
} LZ4HC_Data_Structure;

/* Local Macros */
#define	HASH_FUNCTION(i)	(((i) * 2654435761U) >> \
	((MINMATCH*8)-HASH_LOG))
/* flexible, MAXD dependent */
/* #define	DELTANEXTU16(p)	chainTable[(p) & MAXD_MASK] */
#define	DELTANEXTU16(p)	chainTable[(U16)(p)]	/* faster */

static U32
LZ4HC_hashPtr(const void *ptr)
{
	return (HASH_FUNCTION(LZ4_read32(ptr)));
}

/* HC Compression */
static void
LZ4HC_init(LZ4HC_Data_Structure *hc4, const BYTE *start)
{
	(void) MEM_INIT((void *)hc4->hashTable, 0, sizeof (hc4->hashTable));
	(void) MEM_INIT(hc4->chainTable, 0xFF, sizeof (hc4->chainTable));
	hc4->nextToUpdate = 64 KB;
	hc4->base = start - 64 KB;
	hc4->end = start;
	hc4->dictBase = start - 64 KB;
	hc4->dictLimit = 64 KB;
	hc4->lowLimit = 64 KB;
}

/* Update chains up to ip (excluded) */
FORCE_INLINE void
LZ4HC_Insert(LZ4HC_Data_Structure *hc4, const BYTE *ip)
{
	U16 *chainTable = hc4->chainTable;
	U32 *HashTable = hc4->hashTable;
	const BYTE *const base = hc4->base;
	const U32 target = (U32)(ip - base);
	U32 idx = hc4->nextToUpdate;

	while (idx < target) {
		U32 h = LZ4HC_hashPtr(base + idx);
		size_t delta = idx - HashTable[h];
		if (delta > MAX_DISTANCE)
			delta = MAX_DISTANCE;
		DELTANEXTU16(idx) = (U16)delta;
		HashTable[h] = idx;
		idx++;
	}

	hc4->nextToUpdate = target;
}

FORCE_INLINE int
LZ4HC_InsertAndFindBestMatch(
	LZ4HC_Data_Structure *hc4,	/* Index table will be updated */
	const BYTE *ip,
	const BYTE *const iLimit,
	const BYTE **matchpos,
	const int maxNbAttempts)
{
	U16 *const chainTable = hc4->chainTable;
	U32 *const HashTable = hc4->hashTable;
	const BYTE *const base = hc4->base;
	const BYTE *const dictBase = hc4->dictBase;
	const U32 dictLimit = hc4->dictLimit;
	const U32 lowLimit = (hc4->lowLimit + 64 KB > (U32)(ip - base)) ?
	    hc4->lowLimit : (U32)(ip - base) - (64 KB - 1);
	U32 matchIndex;
	const BYTE *match;
	int nbAttempts = maxNbAttempts;
	size_t ml = 0;

	/* HC4 match finder */
	LZ4HC_Insert(hc4, ip);
	matchIndex = HashTable[LZ4HC_hashPtr(ip)];

	while ((matchIndex >= lowLimit) && (nbAttempts)) {
		nbAttempts--;
		if (matchIndex >= dictLimit) {
			match = base + matchIndex;
			if (*(match + ml) == *(ip + ml) &&
			    (LZ4_read32(match) == LZ4_read32(ip))) {
				size_t mlt = LZ4_count(
				    ip + MINMATCH, match + MINMATCH, iLimit) +
				    MINMATCH;
				if (mlt > ml) {
					ml = mlt;
					*matchpos = match;
				}
			}
		} else {
			match = dictBase + matchIndex;
			if (LZ4_read32(match) == LZ4_read32(ip)) {
				size_t mlt;
				const BYTE *vLimit =
				    ip + (dictLimit - matchIndex);
				if (vLimit > iLimit)
					vLimit = iLimit;
				mlt = LZ4_count(
				    ip + MINMATCH, match + MINMATCH, vLimit) +
				    MINMATCH;
				if ((ip + mlt == vLimit) && (vLimit < iLimit))
					mlt += LZ4_count(
					    ip + mlt, base + dictLimit, iLimit);
				if (mlt > ml) {
					ml = mlt;
					*matchpos = base + matchIndex;
				}	/* virtual matchpos */
			}
		}
		matchIndex -= DELTANEXTU16(matchIndex);
	}

	return ((int)ml);
}

FORCE_INLINE int
LZ4HC_InsertAndGetWiderMatch(LZ4HC_Data_Structure *hc4,
	const BYTE *const ip,
	const BYTE *const iLowLimit,
	const BYTE *const iHighLimit,
	int longest,
	const BYTE **matchpos,
	const BYTE **startpos,
	const int maxNbAttempts)
{
	U16 *const chainTable = hc4->chainTable;
	U32 *const HashTable = hc4->hashTable;
	const BYTE *const base = hc4->base;
	const U32 dictLimit = hc4->dictLimit;
	const BYTE *const lowPrefixPtr = base + dictLimit;
	const U32 lowLimit = (hc4->lowLimit + 64 KB > (U32)(ip - base)) ?
	    hc4->lowLimit : (U32)(ip - base) - (64 KB - 1);
	const BYTE *const dictBase = hc4->dictBase;
	U32 matchIndex;
	int nbAttempts = maxNbAttempts;
	int delta = (int)(ip - iLowLimit);

	/* First Match */
	LZ4HC_Insert(hc4, ip);
	matchIndex = HashTable[LZ4HC_hashPtr(ip)];

	while ((matchIndex >= lowLimit) && (nbAttempts)) {
		nbAttempts--;
		if (matchIndex >= dictLimit) {
			const BYTE *matchPtr = base + matchIndex;
			if (*(iLowLimit + longest) ==
			    *(matchPtr - delta + longest))
				if (LZ4_read32(matchPtr) == LZ4_read32(ip)) {
					int mlt = MINMATCH + LZ4_count(
					    ip + MINMATCH,
					    matchPtr + MINMATCH,
					    iHighLimit);
					int back = 0;

					while ((ip + back > iLowLimit) &&
					    (matchPtr + back > lowPrefixPtr) &&
					    (ip[back - 1] ==
					    matchPtr[back - 1]))
						back--;

					mlt -= back;

					if (mlt > longest) {
						longest = (int)mlt;
						*matchpos = matchPtr + back;
						*startpos = ip + back;
					}
				}
		} else {
			const BYTE *matchPtr = dictBase + matchIndex;
			if (LZ4_read32(matchPtr) == LZ4_read32(ip)) {
				size_t mlt;
				int back = 0;
				const BYTE *vLimit =
				    ip + (dictLimit - matchIndex);
				if (vLimit > iHighLimit)
					vLimit = iHighLimit;
				mlt = LZ4_count(ip + MINMATCH,
				    matchPtr + MINMATCH, vLimit) + MINMATCH;
				if ((ip + mlt == vLimit) &&
				    (vLimit < iHighLimit))
					mlt += LZ4_count(ip + mlt,
					    base + dictLimit,
					    iHighLimit);
				while ((ip + back > iLowLimit) &&
				    (matchIndex + back > lowLimit) &&
				    (ip[back - 1] == matchPtr[back - 1]))
					back--;
				mlt -= back;
				if ((int)mlt > longest) {
					longest = (int)mlt;
					*matchpos = base + matchIndex + back;
					*startpos = ip + back;
				}
			}
		}
		matchIndex -= DELTANEXTU16(matchIndex);
	}

	return (longest);
}

typedef enum { noLimit = 0, limitedOutput = 1 } limitedOutput_directive;

FORCE_INLINE int
LZ4HC_encodeSequence(
	const BYTE **ip,
	BYTE **op,
	const BYTE **anchor,
	int matchLength,
	const BYTE *const match,
	limitedOutput_directive
	limitedOutputBuffer, BYTE *oend)
{
	int length;
	BYTE *token;

	/* Encode Literal length */
	length = (int)(*ip - *anchor);
	token = (*op)++;
	/* Check output limit */
	if ((limitedOutputBuffer) &&
	    ((*op + (length >> 8) + length + (2 + 1 + LASTLITERALS)) > oend))
		return (1);
	if (length >= (int)RUN_MASK) {
		int len;
		*token = (RUN_MASK << ML_BITS);
		len = length - RUN_MASK;
		for (; len > 254; len -= 255)
			*(*op)++ = 255;
		*(*op)++ = (BYTE)len;
	} else {
		*token = (BYTE)(length << ML_BITS);
	}

	/* Copy Literals */
	LZ4_wildCopy(*op, *anchor, (*op) + length);
	*op += length;

	/* Encode Offset */
	LZ4_writeLE16(*op, (U16)(*ip - match));
	*op += 2;

	/* Encode MatchLength */
	length = (int)(matchLength - MINMATCH);
	if ((limitedOutputBuffer) &&
	    (*op + (length >> 8) + (1 + LASTLITERALS) > oend))
		return (1);	/* Check output limit */
	if (length >= (int)ML_MASK) {
		*token += ML_MASK;
		length -= ML_MASK;
		for (; length > 509; length -= 510) {
			*(*op)++ = 255;
			*(*op)++ = 255;
		}
		if (length > 254) {
			length -= 255;
			*(*op)++ = 255;
		}
		*(*op)++ = (BYTE)length;
	} else {
		*token += (BYTE)(length);
	}

	/* Prepare next loop */
	*ip += matchLength;
	*anchor = *ip;

	return (0);
}

static int
LZ4HC_compress_generic(
	void *ctxvoid,
	const char *source,
	char *dest,
	int inputSize,
	int maxOutputSize,
	int compressionLevel,
	limitedOutput_directive limit)
{
	LZ4HC_Data_Structure *ctx = (LZ4HC_Data_Structure *)ctxvoid;
	const BYTE *ip = (const BYTE *)source;
	const BYTE *anchor = ip;
	const BYTE *const iend = ip + inputSize;
	const BYTE *const mflimit = iend - MFLIMIT;
	const BYTE *const matchlimit = (iend - LASTLITERALS);

	BYTE *op = (BYTE *)dest;
	BYTE *const oend = op + maxOutputSize;

	unsigned maxNbAttempts;
	int ml, ml2, ml3, ml0;
	const BYTE *ref = NULL;
	const BYTE *start2 = NULL;
	const BYTE *ref2 = NULL;
	const BYTE *start3 = NULL;
	const BYTE *ref3 = NULL;
	const BYTE *start0;
	const BYTE *ref0;

	/* init */
	ASSERT((compressionLevel >= 1) &&
	    (compressionLevel <= g_maxCompressionLevel));
	maxNbAttempts = 1 << (compressionLevel - 1);
	ctx->end += inputSize;

	ip++;

	/* Main Loop */
	while (ip < mflimit) {
		ml = LZ4HC_InsertAndFindBestMatch(
		    ctx, ip, matchlimit, (&ref), maxNbAttempts);
		if (!ml) {
			ip++;
			continue;
		}

		/* saved, in case we would skip too much */
		start0 = ip;
		ref0 = ref;
		ml0 = ml;

_Search2:
		if (ip + ml < mflimit)
			ml2 = LZ4HC_InsertAndGetWiderMatch(ctx, ip + ml - 2,
			    ip + 1, matchlimit, ml, &ref2, &start2,
			    maxNbAttempts);
		else
			ml2 = ml;

		if (ml2 == ml) {	/* No better match */
			if (LZ4HC_encodeSequence(
			    &ip, &op, &anchor, ml, ref, limit, oend))
				return (0);
			continue;
		}

		if (start0 < ip) {
			if (start2 < ip + ml0) {	/* empirical */
				ip = start0;
				ref = ref0;
				ml = ml0;
			}
		}

		/* Here, start0==ip */
		if ((start2 - ip) < 3) { /* First Match too small : removed */
			ml = ml2;
			ip = start2;
			ref = ref2;
			goto _Search2;
		}

_Search3:
		/*
		 * Currently we have :
		 * ml2 > ml1, and
		 * ip1+3 <= ip2 (usually < ip1+ml1)
		 */
		if ((start2 - ip) < OPTIMAL_ML) {
			int correction;
			int new_ml = ml;
			if (new_ml > OPTIMAL_ML)
				new_ml = OPTIMAL_ML;
			if (ip + new_ml > start2 + ml2 - MINMATCH)
				new_ml = (int)(start2 - ip) + ml2 - MINMATCH;
			correction = new_ml - (int)(start2 - ip);
			if (correction > 0) {
				start2 += correction;
				ref2 += correction;
				ml2 -= correction;
			}
		}
		/*
		 * Now, we have start2 = ip+new_ml,
		 * with new_ml = min(ml, OPTIMAL_ML=18)
		 */

		if (start2 + ml2 < mflimit)
			ml3 = LZ4HC_InsertAndGetWiderMatch(ctx,
			    start2 + ml2 - 3, start2, matchlimit, ml2,
			    &ref3, &start3, maxNbAttempts);
		else
			ml3 = ml2;

		if (ml3 == ml2) { /* No better match : 2 sequences to encode */
			/* ip & ref are known; Now for ml */
			if (start2 < ip + ml)
				ml = (int)(start2 - ip);
			/* Now, encode 2 sequences */
			if (LZ4HC_encodeSequence(
			    &ip, &op, &anchor, ml, ref, limit, oend))
				return (0);
			ip = start2;
			if (LZ4HC_encodeSequence(
			    &ip, &op, &anchor, ml2, ref2, limit, oend))
				return (0);
			continue;
		}

		if (start3 < ip + ml + 3) {
			/* Not enough space for match 2 : remove it */
			if (start3 >= (ip + ml)) {
				/*
				 * can write Seq1 immediately ==>
				 * Seq2 is removed, so Seq3 becomes Seq1
				 */
				if (start2 < ip + ml) {
					int correction =
					    (int)(ip + ml - start2);
					start2 += correction;
					ref2 += correction;
					ml2 -= correction;
					if (ml2 < MINMATCH) {
						start2 = start3;
						ref2 = ref3;
						ml2 = ml3;
					}
				}

				if (LZ4HC_encodeSequence(
				    &ip, &op, &anchor, ml, ref, limit, oend))
					return (0);
				ip = start3;
				ref = ref3;
				ml = ml3;

				start0 = start2;
				ref0 = ref2;
				ml0 = ml2;
				goto _Search2;
			}

			start2 = start3;
			ref2 = ref3;
			ml2 = ml3;
			goto _Search3;
		}

		/*
		 * OK, now we have 3 ascending matches;
		 * let's write at least the first one
		 *
		 * ip & ref are known; Now for ml
		 */
		if (start2 < ip + ml) {
			if ((start2 - ip) < (int)ML_MASK) {
				int correction;
				if (ml > OPTIMAL_ML)
					ml = OPTIMAL_ML;
				if (ip + ml > start2 + ml2 - MINMATCH)
					ml = (int)(start2 - ip) + ml2 -
					    MINMATCH;
				correction = ml - (int)(start2 - ip);
				if (correction > 0) {
					start2 += correction;
					ref2 += correction;
					ml2 -= correction;
				}
			} else {
				ml = (int)(start2 - ip);
			}
		}
		if (LZ4HC_encodeSequence(
		    &ip, &op, &anchor, ml, ref, limit, oend))
			return (0);

		ip = start2;
		ref = ref2;
		ml = ml2;

		start2 = start3;
		ref2 = ref3;
		ml2 = ml3;

		goto _Search3;
	}

	/* Encode Last Literals */
	{
		int lastRun = (int)(iend - anchor);
		/* Check output limit */
		if ((limit) &&
		    (((char *)op - dest) + lastRun + 1 +
		    ((lastRun + 255 - RUN_MASK) / 255) >
		    (U32)maxOutputSize))
			return (0);
		if (lastRun >= (int)RUN_MASK) {
			*op++ = (RUN_MASK << ML_BITS);
			lastRun -= RUN_MASK;
			for (; lastRun > 254; lastRun -= 255)
				*op++ = 255;
			*op++ = (BYTE)lastRun;
		} else {
			*op++ = (BYTE)(lastRun << ML_BITS);
		}
		(void) memcpy(op, anchor, iend - anchor);
		op += iend - anchor;
	}

	/* End */
	return ((int)(((char *)op) - dest));
}

static int
LZ4_sizeofStateHC(void)
{
	return (sizeof (LZ4HC_Data_Structure));
}

static int
LZ4_compress_HC_extStateHC(void *state, const char *src, char *dst,
    int srcSize, int maxDstSize, int compressionLevel)
{
	/* state should be aligned for pointers (32 or 64 bits) */
	ASSERT(((size_t)(state) & (sizeof (void *) - 1)) == 0);

	LZ4HC_init((LZ4HC_Data_Structure *)state, (const BYTE *)src);
	if (maxDstSize < LZ4_compressBound(srcSize))
		return (LZ4HC_compress_generic(state, src, dst, srcSize,
		    maxDstSize, compressionLevel, limitedOutput));
	else
		return (LZ4HC_compress_generic(state, src, dst, srcSize,
		    maxDstSize, compressionLevel, noLimit));
}

static int
LZ4_compress_HC(const char *src, char *dst,
    int srcSize, int maxDstSize, int compressionLevel)
{
	LZ4HC_Data_Structure *state = kmem_cache_alloc(lz4hc_cache, KM_SLEEP);
	int result;

	/* out of kernel memory, fail */
	if (state == NULL)
		return (0);

	result = LZ4_compress_HC_extStateHC(state, src, dst,
	    srcSize, maxDstSize, compressionLevel);

	kmem_cache_free(lz4hc_cache, state);

	return (result);
}

void
lz4hc_init(void)
{
	lz4hc_cache = kmem_cache_create("lz4hc_cache",
	    LZ4_sizeofStateHC(), sizeof (void *),
	    NULL, NULL, NULL, NULL, NULL, 0);
}

void
lz4hc_fini(void)
{
	if (lz4hc_cache) {
		kmem_cache_destroy(lz4hc_cache);
		lz4hc_cache = NULL;
	}
}
