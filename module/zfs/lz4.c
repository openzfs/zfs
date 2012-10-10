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
* Copyright (C) 2011-2012, Yann Collet.
* Copyright (C) 2012, Saso Kiselkov 
* Copyright (C) 2012, Eric Dillmann
*/

/*
   LZ4 - Fast LZ compression algorithm
   Header File
   Copyright (C) 2011-2012, Yann Collet.
   BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

       * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
       * Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the
   distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   You can contact the author at :
   - LZ4 homepage : http://fastcompression.blogspot.com/p/lz4.html
   - LZ4 source repository : http://code.google.com/p/lz4/
*/

#include <sys/zfs_context.h>
#include <sys/types.h>

#ifdef __GNUC__
#undef likely
#undef unlikely
#define likely(x)       (__builtin_expect((x),1))
#define unlikely(x)     (__builtin_expect((x),0))
#else
#define likely(x)       (x)
#define unlikely(x)     (x)
#endif

#ifndef BE_IN32
#define BE_IN8(xa) \
        *((uint8_t *)(xa))

#define BE_IN16(xa) \
        (((uint16_t)BE_IN8(xa) << 8) | BE_IN8((uint8_t *)(xa)+1))

#define BE_IN32(xa) \
        (((uint32_t)BE_IN16(xa) << 16) | BE_IN16((uint8_t *)(xa)+2))
#endif

//****************************
// functions declaration
//****************************
static int LZ4_uncompress_unknownOutputSize (const uint8_t* source, uint8_t* dest, int isize, int maxOutputSize);

/*
LZ4_uncompress_unknownOutputSize() :
	isize  : is the input size, therefore the compressed size
	maxOutputSize : is the size of the destination buffer (which must be already allocated)
	return : the number of bytes decoded in the destination buffer (necessarily <= maxOutputSize)
			 If the source stream is malformed, the function will stop decoding and return a negative result, indicating the byte position of the faulty instruction
			 This function never writes beyond dest + maxOutputSize, and is therefore protected against malicious data packets
	note   : Destination buffer must be already allocated.
	         This version is slightly slower than LZ4_uncompress()
*/

//**************************************
// Tuning parameters
//**************************************
// MEMORY_USAGE :
// Memory usage formula : N->2^N Bytes (examples : 10 -> 1KB; 12 -> 4KB ; 16 -> 64KB; 20 -> 1MB; etc.)
// Increasing memory usage improves compression ratio
// Reduced memory usage can improve speed, due to cache effect
// Default value is 14, for 16KB, which nicely fits into Intel x86 L1 cache
#define MEMORY_USAGE 14

// NOTCOMPRESSIBLE_DETECTIONLEVEL :
// Decreasing this value will make the algorithm skip faster data segments considered "incompressible"
// This may decrease compression ratio dramatically, but will be faster on incompressible data
// Increasing this value will make the algorithm search more before declaring a segment "incompressible"
// This could improve compression a bit, but will be slower on incompressible data
// The default value (6) is recommended
#define NOTCOMPRESSIBLE_DETECTIONLEVEL 6

// BIG_ENDIAN_NATIVE_BUT_INCOMPATIBLE :
// This will provide a small boost to performance for big endian cpu, but the resulting compressed stream will be incompatible with little-endian CPU.
// You can set this option to 1 in situations where data will remain within closed environment
// This option is useless on Little_Endian CPU (such as x86)
//#define BIG_ENDIAN_NATIVE_BUT_INCOMPATIBLE 1

//**************************************
// CPU Feature Detection
//**************************************
// 32 or 64 bits ?
#if (defined(__x86_64__) || defined(__x86_64) || defined(__amd64__) || defined(__amd64) || defined(__ppc64__) || defined(_WIN64) || defined(__LP64__) || defined(_LP64) )   // Detects 64 bits mode
#  define LZ4_ARCH64 1
#else
#  define LZ4_ARCH64 0
#endif

// Little Endian or Big Endian ?
// Note : overwrite the below #define if you know your architecture endianess
#if (defined(__BIG_ENDIAN__) || defined(__BIG_ENDIAN) || defined(_BIG_ENDIAN) || defined(_ARCH_PPC) || defined(__PPC__) || defined(__PPC) || defined(PPC) || defined(__powerpc__) || defined(__powerpc) || defined(powerpc) || ((defined(__BYTE_ORDER__)&&(__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__))) )
#  define LZ4_BIG_ENDIAN 1
#else
// Little Endian assumed. PDP Endian and other very rare endian format are unsupported.
#endif

// Unaligned memory access is automatically enabled for "common" CPU, such as x86.
// For others CPU, the compiler will be more cautious, and insert extra code to ensure aligned access is respected
// If you know your target CPU supports unaligned memory access, you may want to force this option manually to improve performance
#if defined(__ARM_FEATURE_UNALIGNED)
#  define LZ4_FORCE_UNALIGNED_ACCESS 1
#endif

// Define this parameter if your target system or compiler does not support hardware bit count
#if defined(_MSC_VER) && defined(_WIN32_WCE)            // Visual Studio for Windows CE does not support Hardware bit count
#  define LZ4_FORCE_SW_BITCOUNT
#endif

#define GCC_VERSION (__GNUC__ * 100 + __GNUC_MINOR__)

#define lz4_bswap16(x) ((unsigned short int) ((((x) >> 8) & 0xffu) | (((x) & 0xffu) << 8)))

//**************************************
// Basic Types
//**************************************
#define BYTE	uint8_t
#define U16		uint16_t
#define U32		uint32_t
#define S32		int32_t
#define U64		uint64_t

#ifndef LZ4_FORCE_UNALIGNED_ACCESS
#  pragma pack(push, 1)
#endif

typedef struct _U16_S { U16 v; } U16_S;
typedef struct _U32_S { U32 v; } U32_S;
typedef struct _U64_S { U64 v; } U64_S;

#ifndef LZ4_FORCE_UNALIGNED_ACCESS
#  pragma pack(pop)
#endif

#define A64(x) (((U64_S *)(x))->v)
#define A32(x) (((U32_S *)(x))->v)
#define A16(x) (((U16_S *)(x))->v)

//**************************************
// Constants
//**************************************
#define MINMATCH 4

#define HASH_LOG (MEMORY_USAGE-2)
#define HASHTABLESIZE (1 << HASH_LOG)
#define HASH_MASK (HASHTABLESIZE - 1)

#define SKIPSTRENGTH (NOTCOMPRESSIBLE_DETECTIONLEVEL>2?NOTCOMPRESSIBLE_DETECTIONLEVEL:2)
#define STACKLIMIT 13
#define HEAPMODE 1
#define COPYLENGTH 8
#define LASTLITERALS 5
#define MFLIMIT (COPYLENGTH+MINMATCH)
#define MINLENGTH (MFLIMIT+1)

#define MAXD_LOG 16
#define MAX_DISTANCE ((1 << MAXD_LOG) - 1)

#define ML_BITS 4
#define ML_MASK ((1U<<ML_BITS)-1)
#define RUN_BITS (8-ML_BITS)
#define RUN_MASK ((1U<<RUN_BITS)-1)


//**************************************
// Architecture-specific macros
//**************************************
#if LZ4_ARCH64	// 64-bit
#  define STEPSIZE 8
#  define UARCH U64
#  define AARCH A64
#  define LZ4_COPYSTEP(s,d)		A64(d) = A64(s); d+=8; s+=8;
#  define LZ4_COPYPACKET(s,d)		LZ4_COPYSTEP(s,d)
#  define LZ4_SECURECOPY(s,d,e)	if (d<e) LZ4_WILDCOPY(s,d,e)
#  define HTYPE U32
#  define INITBASE(base)			const BYTE* const base = ip
#else		// 32-bit
#  define STEPSIZE 4
#  define UARCH U32
#  define AARCH A32
#  define LZ4_COPYSTEP(s,d)		A32(d) = A32(s); d+=4; s+=4;
#  define LZ4_COPYPACKET(s,d)		LZ4_COPYSTEP(s,d); LZ4_COPYSTEP(s,d);
#  define LZ4_SECURECOPY			LZ4_WILDCOPY
#  define HTYPE const BYTE*
#  define INITBASE(base)			const int base = 0
#endif

#if (defined(LZ4_BIG_ENDIAN) && !defined(BIG_ENDIAN_NATIVE_BUT_INCOMPATIBLE))
#  define LZ4_READ_LITTLEENDIAN_16(d,s,p) { U16 v = A16(p); v = lz4_bswap16(v); d = (s) - v; }
#  define LZ4_WRITE_LITTLEENDIAN_16(p,i)  { U16 v = (U16)(i); v = lz4_bswap16(v); A16(p) = v; p+=2; }
#else		// Little Endian
#  define LZ4_READ_LITTLEENDIAN_16(d,s,p) { d = (s) - A16(p); }
#  define LZ4_WRITE_LITTLEENDIAN_16(p,v)  { A16(p) = v; p+=2; }
#endif

//**************************************
// Local structures
//**************************************
struct refTables
{
	HTYPE hashTable[HASHTABLESIZE];
};


//**************************************
// Macros
//**************************************
#define LZ4_HASH_FUNCTION(i)	(((i) * 2654435761U) >> ((MINMATCH*8)-HASH_LOG))
#define LZ4_HASH_VALUE(p)		LZ4_HASH_FUNCTION(A32(p))
#define LZ4_WILDCOPY(s,d,e)		do { LZ4_COPYPACKET(s,d) } while (d<e);
#define LZ4_BLINDCOPY(s,d,l)	{ BYTE* e=(d)+l; LZ4_WILDCOPY(s,d,e); d=e; }


//****************************
// Private functions
//****************************
#if LZ4_ARCH64

static inline int LZ4_NbCommonBytes (register U64 val)
{
#if defined(LZ4_BIG_ENDIAN)
    #if defined(__GNUC__) && (GCC_VERSION >= 304) && !defined(LZ4_FORCE_SW_BITCOUNT)
    return (__builtin_clzll(val) >> 3);
    #else
	int r;
	if (!(val>>32)) { r=4; } else { r=0; val>>=32; }
	if (!(val>>16)) { r+=2; val>>=8; } else { val>>=24; }
	r += (!val);
	return r;
    #endif
#else
    #if defined(__GNUC__) && (GCC_VERSION >= 304) && !defined(LZ4_FORCE_SW_BITCOUNT)
    return (__builtin_ctzll(val) >> 3);
    #else
	static const int DeBruijnBytePos[64] = { 0, 0, 0, 0, 0, 1, 1, 2, 0, 3, 1, 3, 1, 4, 2, 7, 0, 2, 3, 6, 1, 5, 3, 5, 1, 3, 4, 4, 2, 5, 6, 7, 7, 0, 1, 2, 3, 3, 4, 6, 2, 6, 5, 5, 3, 4, 5, 6, 7, 1, 2, 4, 6, 4, 4, 5, 7, 2, 6, 5, 7, 6, 7, 7 };
	return DeBruijnBytePos[((U64)((val & -val) * 0x0218A392CDABBD3F)) >> 58];
    #endif
#endif
}

#else

static inline int LZ4_NbCommonBytes (register U32 val)
{
#if defined(LZ4_BIG_ENDIAN)
    #if defined(__GNUC__) && (GCC_VERSION >= 304) && !defined(LZ4_FORCE_SW_BITCOUNT)
    return (__builtin_clz(val) >> 3);
    #else
	int r;
	if (!(val>>16)) { r=2; val>>=8; } else { r=0; val>>=24; }
	r += (!val);
	return r;
    #endif
#else
    #if defined(__GNUC__) && (GCC_VERSION >= 304) && !defined(LZ4_FORCE_SW_BITCOUNT)
    return (__builtin_ctz(val) >> 3);
    #else
	static const int DeBruijnBytePos[32] = { 0, 0, 3, 0, 3, 1, 3, 0, 3, 2, 2, 1, 3, 2, 0, 1, 3, 3, 1, 2, 2, 2, 2, 0, 3, 1, 2, 0, 1, 0, 1, 1 };
	return DeBruijnBytePos[((U32)((val & -(S32)val) * 0x077CB531U)) >> 27];
    #endif
#endif
}

#endif



//******************************
// Compression functions
//******************************

// LZ4_compressCtx :
// -----------------
// Compress 'isize' bytes from 'source' into an output buffer 'dest' of maximum size 'maxOutputSize'.
// If it cannot achieve it, compression will stop, and result of the function will be zero.
// return : the number of bytes written in buffer 'dest', or 0 if the compression fails

static inline int LZ4_compressCtx(void** ctx,
				 const uint8_t* source,
				 uint8_t* dest,
				 int isize,
				 int maxOutputSize)
{
#if HEAPMODE
	struct refTables *srt = (struct refTables *) (*ctx);
	HTYPE* HashTable;
#else
	HTYPE HashTable[HASHTABLESIZE] = {0};
#endif

	const BYTE* ip = (BYTE*) source;
	INITBASE(base);
	const BYTE* anchor = ip;
	const BYTE* const iend = ip + isize;
	const BYTE* const mflimit = iend - MFLIMIT;
#define matchlimit (iend - LASTLITERALS)

	BYTE* op = (BYTE*) dest;
	BYTE* const oend = op + maxOutputSize;

	int len, length;
	const int skipStrength = SKIPSTRENGTH;
	U32 forwardH;


	// Init
	if (isize<MINLENGTH) goto _last_literals;
#if HEAPMODE
	if (*ctx == NULL)
	{
		srt = (struct refTables *) kmem_zalloc(sizeof(struct refTables), KM_PUSHPAGE | KM_NODEBUG);
		*ctx = (void*) srt;
	}
	HashTable = (HTYPE*)(srt->hashTable);
	memset((void*)HashTable, 0, sizeof(srt->hashTable));
#else
	(void) ctx;
#endif


	// First Byte
	HashTable[LZ4_HASH_VALUE(ip)] = ip - base;
	ip++; forwardH = LZ4_HASH_VALUE(ip);

	// Main Loop
    for ( ; ; )
	{
		int findMatchAttempts = (1U << skipStrength) + 3;
		const BYTE* forwardIp = ip;
		const BYTE* ref;
		BYTE* token;

		// Find a match
		do {
			U32 h = forwardH;
			int step = findMatchAttempts++ >> skipStrength;
			ip = forwardIp;
			forwardIp = ip + step;

			if unlikely(forwardIp > mflimit) { goto _last_literals; }

			forwardH = LZ4_HASH_VALUE(forwardIp);
			ref = base + HashTable[h];
			HashTable[h] = ip - base;

		} while ((ref < ip - MAX_DISTANCE) || (A32(ref) != A32(ip)));

		// Catch up
		while ((ip>anchor) && (ref>(BYTE*)source) && unlikely(ip[-1]==ref[-1])) { ip--; ref--; }

		// Encode Literal length
		length = (int)(ip - anchor);
		token = op++;
		if unlikely(op + length + (2 + 1 + LASTLITERALS) + (length>>8) >= oend) return 0; 		// Check output limit

		if (length>=(int)RUN_MASK) { *token=(RUN_MASK<<ML_BITS); len = length-RUN_MASK; for(; len > 254 ; len-=255) *op++ = 255; *op++ = (BYTE)len; }
		else *token = (length<<ML_BITS);

		// Copy Literals
		LZ4_BLINDCOPY(anchor, op, length);

_next_match:
		// Encode Offset
		LZ4_WRITE_LITTLEENDIAN_16(op,(U16)(ip-ref));

		// Start Counting
		ip+=MINMATCH; ref+=MINMATCH;   // MinMatch verified
		anchor = ip;
		while likely(ip<matchlimit-(STEPSIZE-1))
		{
			UARCH diff = AARCH(ref) ^ AARCH(ip);
			if (!diff) { ip+=STEPSIZE; ref+=STEPSIZE; continue; }
			ip += LZ4_NbCommonBytes(diff);
			goto _endCount;
		}
		if (LZ4_ARCH64) if ((ip<(matchlimit-3)) && (A32(ref) == A32(ip))) { ip+=4; ref+=4; }
		if ((ip<(matchlimit-1)) && (A16(ref) == A16(ip))) { ip+=2; ref+=2; }
		if ((ip<matchlimit) && (*ref == *ip)) ip++;
_endCount:

		// Encode MatchLength
		len = (int)(ip - anchor);
		if unlikely(op + (1 + LASTLITERALS) + (len>>8) >= oend) return 0; 		// Check output limit
		if (len>=(int)ML_MASK) { *token+=ML_MASK; len-=ML_MASK; for(; len > 509 ; len-=510) { *op++ = 255; *op++ = 255; } if (len > 254) { len-=255; *op++ = 255; } *op++ = (BYTE)len; }
		else *token += len;

		// Test end of chunk
		if (ip > mflimit) { anchor = ip;  break; }

		// Fill table
		HashTable[LZ4_HASH_VALUE(ip-2)] = ip - 2 - base;

		// Test next position
		ref = base + HashTable[LZ4_HASH_VALUE(ip)];
		HashTable[LZ4_HASH_VALUE(ip)] = ip - base;
		if ((ref > ip - (MAX_DISTANCE + 1)) && (A32(ref) == A32(ip))) { token = op++; *token=0; goto _next_match; }

		// Prepare next loop
		anchor = ip++;
		forwardH = LZ4_HASH_VALUE(ip);
	}

_last_literals:
	// Encode Last Literals
	{
		int lastRun = (int)(iend - anchor);
		if (((uint8_t*)op - dest) + lastRun + 1 + ((lastRun-15)/255) >= maxOutputSize) return 0;
		if (lastRun>=(int)RUN_MASK) { *op++=(RUN_MASK<<ML_BITS); lastRun-=RUN_MASK; for(; lastRun > 254 ; lastRun-=255) *op++ = 255; *op++ = (BYTE) lastRun; }
		else *op++ = (lastRun<<ML_BITS);
		memcpy(op, anchor, iend - anchor);
		op += iend-anchor;
	}

	// End
	return (int) (((uint8_t*)op)-dest);
}



// Note : this function is valid only if isize < LZ4_64KLIMIT
#define LZ4_64KLIMIT ((1<<16) + (MFLIMIT-1))
#define HASHLOG64K (HASH_LOG+1)
#define HASH64KTABLESIZE (1U<<HASHLOG64K)
#define LZ4_HASH64K_FUNCTION(i)	(((i) * 2654435761U) >> ((MINMATCH*8)-HASHLOG64K))
#define LZ4_HASH64K_VALUE(p)	LZ4_HASH64K_FUNCTION(A32(p))
static inline int LZ4_compress64kCtx(void** ctx,
				 const uint8_t* source,
				 uint8_t* dest,
				 int isize,
				 int maxOutputSize)
{
#if HEAPMODE
	struct refTables *srt = (struct refTables *) (*ctx);
	U16* HashTable;
#else
	U16 HashTable[HASH64KTABLESIZE] = {0};
#endif

	const BYTE* ip = (BYTE*) source;
	const BYTE* anchor = ip;
	const BYTE* const base = ip;
	const BYTE* const iend = ip + isize;
	const BYTE* const mflimit = iend - MFLIMIT;
#define matchlimit (iend - LASTLITERALS)

	BYTE* op = (BYTE*) dest;
	BYTE* const oend = op + maxOutputSize;

	int len, length;
	const int skipStrength = SKIPSTRENGTH;
	U32 forwardH;


	// Init
	if (isize<MINLENGTH) goto _last_literals;
#if HEAPMODE
	if (*ctx == NULL)
	{
		srt = (struct refTables *) kmem_zalloc( sizeof(struct refTables) , KM_PUSHPAGE | KM_NODEBUG); 
		*ctx = (void*) srt;
	}
	HashTable = (U16*)(srt->hashTable);
	memset((void*)HashTable, 0, sizeof(srt->hashTable));
#else
	(void) ctx;
#endif


	// First Byte
	ip++; forwardH = LZ4_HASH64K_VALUE(ip);

	// Main Loop
    for ( ; ; )
	{
		int findMatchAttempts = (1U << skipStrength) + 3;
		const BYTE* forwardIp = ip;
		const BYTE* ref;
		BYTE* token;

		// Find a match
		do {
			U32 h = forwardH;
			int step = findMatchAttempts++ >> skipStrength;
			ip = forwardIp;
			forwardIp = ip + step;

			if (forwardIp > mflimit) { goto _last_literals; }

			forwardH = LZ4_HASH64K_VALUE(forwardIp);
			ref = base + HashTable[h];
			HashTable[h] = (U16)(ip - base);

		} while (A32(ref) != A32(ip));

		// Catch up
		while ((ip>anchor) && (ref>(BYTE*)source) && (ip[-1]==ref[-1])) { ip--; ref--; }

		// Encode Literal length
		length = (int)(ip - anchor);
		token = op++;
		if unlikely(op + length + (2 + 1 + LASTLITERALS) + (length>>8) >= oend) return 0; 		// Check output limit

		if (length>=(int)RUN_MASK) { *token=(RUN_MASK<<ML_BITS); len = length-RUN_MASK; for(; len > 254 ; len-=255) *op++ = 255; *op++ = (BYTE)len; }
		else *token = (length<<ML_BITS);

		// Copy Literals
		LZ4_BLINDCOPY(anchor, op, length);

_next_match:
		// Encode Offset
		LZ4_WRITE_LITTLEENDIAN_16(op,(U16)(ip-ref));

		// Start Counting
		ip+=MINMATCH; ref+=MINMATCH;   // MinMatch verified
		anchor = ip;
		while (ip<matchlimit-(STEPSIZE-1))
		{
			UARCH diff = AARCH(ref) ^ AARCH(ip);
			if (!diff) { ip+=STEPSIZE; ref+=STEPSIZE; continue; }
			ip += LZ4_NbCommonBytes(diff);
			goto _endCount;
		}
		if (LZ4_ARCH64) if ((ip<(matchlimit-3)) && (A32(ref) == A32(ip))) { ip+=4; ref+=4; }
		if ((ip<(matchlimit-1)) && (A16(ref) == A16(ip))) { ip+=2; ref+=2; }
		if ((ip<matchlimit) && (*ref == *ip)) ip++;
_endCount:

		// Encode MatchLength
		len = (int)(ip - anchor);
		if unlikely(op + (1 + LASTLITERALS) + (len>>8) >= oend) return 0; 		// Check output limit
		if (len>=(int)ML_MASK) { *token+=ML_MASK; len-=ML_MASK; for(; len > 509 ; len-=510) { *op++ = 255; *op++ = 255; } if (len > 254) { len-=255; *op++ = 255; } *op++ = (BYTE)len; }
		else *token += len;

		// Test end of chunk
		if (ip > mflimit) { anchor = ip;  break; }

		// Fill table
		HashTable[LZ4_HASH64K_VALUE(ip-2)] = (U16)(ip - 2 - base);

		// Test next position
		ref = base + HashTable[LZ4_HASH64K_VALUE(ip)];
		HashTable[LZ4_HASH64K_VALUE(ip)] = (U16)(ip - base);
		if (A32(ref) == A32(ip)) { token = op++; *token=0; goto _next_match; }

		// Prepare next loop
		anchor = ip++;
		forwardH = LZ4_HASH64K_VALUE(ip);
	}

_last_literals:
	// Encode Last Literals
	{
		int lastRun = (int)(iend - anchor);
		if (((uint8_t*)op - dest) + lastRun + 1 + ((lastRun)>>8) >= maxOutputSize) return 0;
		if (lastRun>=(int)RUN_MASK) { *op++=(RUN_MASK<<ML_BITS); lastRun-=RUN_MASK; for(; lastRun > 254 ; lastRun-=255) *op++ = 255; *op++ = (BYTE) lastRun; }
		else *op++ = (lastRun<<ML_BITS);
		memcpy(op, anchor, iend - anchor);
		op += iend-anchor;
	}

	// End
	return (int) (((uint8_t*)op)-dest);
}


static int LZ4_uncompress_unknownOutputSize(
				const uint8_t* source,
				uint8_t* dest,
				int isize,
				int maxOutputSize)
{
	// Local Variables
	const BYTE*  ip = (const BYTE*) source;
	const BYTE* const iend = ip + isize;
	const BYTE*  ref;

	BYTE*  op = (BYTE*) dest;
	BYTE* const oend = op + maxOutputSize;
	BYTE* cpy;

	size_t dec[] ={0, 3, 2, 3, 0, 0, 0, 0};


	// Main Loop
	while (ip<iend)
	{
		BYTE token;
		int length;

		// get runlength
		token = *ip++;
		if ((length=(token>>ML_BITS)) == RUN_MASK) { int s=255; while ((ip<iend) && (s==255)) { s=*ip++; length += s; } }

		// copy literals
		cpy = op+length;
		if ((cpy>oend-COPYLENGTH) || (ip+length>iend-COPYLENGTH))
		{
			if (cpy > oend) goto _output_error;          // Error : request to write beyond destination buffer
			if (ip+length > iend) goto _output_error;    // Error : request to read beyond source buffer
			memcpy(op, ip, length);
			op += length;
			ip += length;
			if (ip<iend) goto _output_error;             // Error : LZ4 format violation
			break;    // Necessarily EOF, due to parsing restrictions
		}
		LZ4_WILDCOPY(ip, op, cpy); ip -= (op-cpy); op = cpy;

		// get offset
		LZ4_READ_LITTLEENDIAN_16(ref,cpy,ip); ip+=2;
		if (ref < (BYTE* const)dest) goto _output_error;   // Error : offset creates reference outside of destination buffer

		// get matchlength
		if ((length=(token&ML_MASK)) == ML_MASK) { while (ip<iend) { int s = *ip++; length +=s; if (s==255) continue; break; } }

		// copy repeated sequence
		if unlikely(op-ref<STEPSIZE)
		{
#if LZ4_ARCH64
			size_t dec2table[]={0, 0, 0, -1, 0, 1, 2, 3};
			size_t dec2 = dec2table[op-ref];
#else
			const int dec2 = 0;
#endif
			*op++ = *ref++;
			*op++ = *ref++;
			*op++ = *ref++;
			*op++ = *ref++;
			ref -= dec[op-ref];
			A32(op)=A32(ref); op += STEPSIZE-4;
			ref -= dec2;
		} else { LZ4_COPYSTEP(ref,op); }
		cpy = op + length - (STEPSIZE-4);
		if (cpy>oend-COPYLENGTH)
		{
			if (cpy > oend) goto _output_error;           // Error : request to write outside of destination buffer
			LZ4_SECURECOPY(ref, op, (oend-COPYLENGTH));
			while(op<cpy) *op++=*ref++;
			op=cpy;
			if (op == oend) break;    // Check EOF (should never happen, since last 5 bytes are supposed to be literals)
			continue;
		}
		LZ4_SECURECOPY(ref, op, cpy);
		op=cpy;		// correction
	}

	// end of decoding
	return (int) (((uint8_t*)op)-dest);

	// write overflow error detected
_output_error:
	return (int) (-(((uint8_t*)ip)-source));
}

static int
real_LZ4_compress(const uint8_t *source, uint8_t *dest, int isize, int osize)
{
#if HEAPMODE
        void *ctx = kmem_zalloc(sizeof(struct refTables), KM_PUSHPAGE | KM_NODEBUG);
        int result;

        /* out of kernel memory, gently fall through - this will disable
         * compression in zio_compress_data */
        if (ctx == NULL)
                return (0);

        if (isize < LZ4_64KLIMIT)
                result = LZ4_compress64kCtx(ctx, source, dest, isize, osize);
        else
                result = LZ4_compressCtx(ctx, source, dest, isize, osize);

        kmem_free(ctx, sizeof(struct refTables));
        return (result);
#else
        if (isize < (int) LZ4_64KLIMIT)
                return LZ4_compress64kCtx(NULL, source, dest, isize, osize);
        return LZ4_compressCtx(NULL, source, dest, isize, osize);
#endif
}


/*ARGSUSED*/
size_t
lz4_compress(void *s_start, void *d_start, size_t s_len, size_t d_len, int n)
{
        uint32_t bufsiz;
        uint8_t *dest = d_start;

        ASSERT(d_len >= 4);

        bufsiz = real_LZ4_compress((uint8_t*)s_start, (uint8_t*)&dest[4], s_len,
            d_len - 4);

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

        return (bufsiz + 4);
}

/*ARGSUSED*/
int
lz4_decompress(void *s_start, void *d_start, size_t s_len, size_t d_len, int n)
{
        const uint8_t *src = s_start;
        uint32_t bufsiz = BE_IN32(src);

        /* invalid compressed buffer size encoded at start */
        if (bufsiz + 4 > s_len)
                return 1;

        /*
         * Returns 0 on success (decompression function returned non-negative)
         * and non-zero on failure (decompression function returned negative.
         */
        return (LZ4_uncompress_unknownOutputSize(&src[4], d_start, bufsiz,
            d_len) < 0);
}

