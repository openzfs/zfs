////////////////////////////////////////////////////////////////////////////////
//  Copyright(c) 2011-2017 Intel Corporation All rights reserved.
//
//  Redistribution and use in source and binary forms, with or without
//  modification, are permitted provided that the following conditions
//  are met:
//    * Redistributions of source code must retain the above copyright
//      notice, this list of conditions and the following disclaimer.
//    * Redistributions in binary form must reproduce the above copyright
//      notice, this list of conditions and the following disclaimer in
//      the documentation and/or other materials provided with the
//      distribution.
//    * Neither the name of Intel Corporation nor the names of its
//      contributors may be used to endorse or promote products derived
//      from this software without specific prior written permission.
//
//  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
//  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
//  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
//  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
//  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
//  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
//  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES// LOSS OF USE,
//  DATA, OR PROFITS// OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
//  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
//  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
//  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//
// Authors:
//       Erdinc Ozturk
//       Vinodh Gopal
//       James Guilford
//
//
// References:
//       This code was derived and highly optimized from the code described in
//	 paper:
//               Vinodh Gopal et. al. Optimized Galois-Counter-Mode
//		  Implementation on Intel Architecture Processors. August, 2010
//
//       For the shift-based reductions used in this code, we used the method
//	 described in paper:
//               Shay Gueron, Michael E. Kounavis. Intel Carry-Less
//		  Multiplication Instruction and its Usage for Computing the GCM
//		  Mode. January, 2010.
//
//
// Assumptions:
//
//
//
// iv:
//       0                   1                   2                   3
//       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//       |                             Salt  (From the SA)               |
//       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//       |                     Initialization Vector                     |
//       |         (This is the sequence number from IPSec header)       |
//       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//       |                              0x1                              |
//       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
//
//
// AAD:
//       AAD will be padded with 0 to the next 16byte multiple
//       for example, assume AAD is a u32 vector
//
//       if AAD is 8 bytes:
//       AAD[3] = {A0, A1};
//       padded AAD in xmm register = {A1 A0 0 0}
//
//       0                   1                   2                   3
//       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//       |                               SPI (A1)                        |
//       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//       |                     32-bit Sequence Number (A0)               |
//       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//       |                              0x0                              |
//       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
//                                       AAD Format with 32-bit Sequence Number
//
//       if AAD is 12 bytes:
//       AAD[3] = {A0, A1, A2};
//       padded AAD in xmm register = {A2 A1 A0 0}
//
//       0                   1                   2                   3
//       0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
//       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//       |                               SPI (A2)                        |
//       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//       |                 64-bit Extended Sequence Number {A1,A0}       |
//       |                                                               |
//       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//       |                              0x0                              |
//       +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
//
//        AAD Format with 64-bit Extended Sequence Number
//
//
// aadLen:
//       Must be a multiple of 4 bytes and from the definition of the spec.
//       The code additionally supports any aadLen length.
//
// TLen:
//       from the definition of the spec, TLen can only be 8, 12 or 16 bytes.
//
// poly = x^128 + x^127 + x^126 + x^121 + 1
// throughout the code, one tab and two tab indentations are used. one tab is
// for GHASH part, two tabs is for AES part.
//

// .altmacro
.att_syntax prefix

#include "isalc_reg_sizes_att.S"
#include "isalc_gcm_defines_att.S"

#if !defined(GCM128_MODE) && !defined(GCM256_MODE)
#error "No GCM mode selected for gcm_sse.S!"
#endif

#if defined(FUNCT_EXTENSION)
#error "No support for non-temporal versions yet!"
#endif
#define	_nt	1

#ifdef GCM128_MODE
#define FN_NAME(x,y) aes_gcm_ ## x ## _128 ## y ## sse
#define NROUNDS 9
#endif

#ifdef GCM256_MODE
#define FN_NAME(x,y) aes_gcm_ ## x ## _256 ## y ## sse
#define NROUNDS 13
#endif


// need to push 5 registers into stack to maintain
#define STACK_OFFSET 8*5

#define	TMP2	16*0    // Temporary storage for AES State 2 (State 1 is stored in an XMM register)
#define	TMP3	16*1    // Temporary storage for AES State 3
#define	TMP4	16*2    // Temporary storage for AES State 4
#define	TMP5	16*3    // Temporary storage for AES State 5
#define	TMP6	16*4    // Temporary storage for AES State 6
#define	TMP7	16*5    // Temporary storage for AES State 7
#define	TMP8	16*6    // Temporary storage for AES State 8

#define	LOCAL_STORAGE	16*7

#if __OUTPUT_FORMAT == win64
#define	XMM_STORAGE	16*10
#else
#define	XMM_STORAGE	0
#endif

#define	VARIABLE_OFFSET	LOCAL_STORAGE + XMM_STORAGE

////////////////////////////////////////////////////////////////
// Utility Macros
////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// GHASH_MUL MACRO to implement: Data*HashKey mod (128,127,126,121,0)
// Input: A and B (128-bits each, bit-reflected)
// Output: C = A*B*x mod poly, (i.e. >>1 )
// To compute GH = GH*HashKey mod poly, give HK = HashKey<<1 mod poly as input
// GH = GH * HK * x mod poly which is equivalent to GH*HashKey mod poly.
////////////////////////////////////////////////////////////////////////////////
.macro  GHASH_MUL  GH, HK, T1, T2, T3, T4, T5
	// \GH, \HK hold the values for the two operands which are carry-less
	// multiplied.
	////////////////////////////////////////////////////////////////////////
	// Karatsuba Method
	movdqa	\GH, \T1
	pshufd	$0b01001110, \GH, \T2
	pshufd	$0b01001110, \HK, \T3
	pxor	\GH, \T2			// \T2 = (a1+a0)
	pxor	\HK, \T3			// \T3 = (b1+b0)

	pclmulqdq	$0x11, \HK, \T1		// \T1 = a1*b1
	pclmulqdq       $0x00, \HK, \GH		// \GH = a0*b0
	pclmulqdq       $0x00, \T3, \T2		// \T2 = (a1+a0)*(b1+b0)
	pxor	\GH, \T2
	pxor	\T1, \T2			// \T2 = a0*b1+a1*b0

	movdqa	\T2, \T3
	pslldq	$8, \T3		// shift-L \T3 2 DWs
	psrldq	$8, \T2		// shift-R \T2 2 DWs
	pxor	\T3, \GH
	pxor	\T2, \T1	// <\T1:\GH> holds the result of the carry-less multiplication of \GH by \HK


	//first phase of the reduction
	movdqa	\GH, \T2
	movdqa	\GH, \T3
	movdqa	\GH, \T4	// move \GH into \T2, \T3, \T4 in order to perform the three shifts independently

	pslld	$31, \T2	// packed right shifting << 31
	pslld	$30, \T3	// packed right shifting shift << 30
	pslld	$25, \T4	// packed right shifting shift << 25
	pxor	\T3, \T2	// xor the shifted versions
	pxor	\T4, \T2

	movdqa	\T2, \T5
	psrldq	$4, \T5		 // shift-R \T5 1 DW

	pslldq	$12, \T2	// shift-L \T2 3 DWs
	pxor	\T2, \GH	// first phase of the reduction complete
	////////////////////////////////////////////////////////////////////////

	//second phase of the reduction
	movdqa	\GH, \T2	// make 3 copies of \GH (in in \T2, \T3, \T4) for doing three shift operations
	movdqa	\GH, \T3
	movdqa	\GH, \T4

	psrld	$1, \T2		// packed left shifting >> 1
	psrld	$2, \T3		// packed left shifting >> 2
	psrld	$7, \T4		// packed left shifting >> 7
	pxor	\T3, \T2	// xor the shifted versions
	pxor	\T4, \T2

	pxor	\T5, \T2
	pxor	\T2, \GH
	pxor	\T1, \GH	// the result is in \T1

.endm // GHASH_MUL

////////////////////////////////////////////////////////////////////////////////
// PRECOMPUTE: Precompute HashKey_{2..8} and HashKey{,_{2..8}}_k.
// HasKey_i_k holds XORed values of the low and high parts of the Haskey_i.
////////////////////////////////////////////////////////////////////////////////
.macro PRECOMPUTE GDATA, HK, T1, T2, T3, T4, T5, T6

	movdqa	\HK, \T4
	pshufd	$0b01001110, \HK, \T1
	pxor	\HK, \T1
	movdqu	\T1, HashKey_k(\GDATA)


	GHASH_MUL \T4, \HK, \T1, \T2, \T3, \T5, \T6	//  \T4 = HashKey^2<<1 mod poly
	movdqu	\T4, HashKey_2(\GDATA)		//  [HashKey_2] = HashKey^2<<1 mod poly
	pshufd	$0b01001110, \T4, \T1
	pxor	\T4, \T1
	movdqu	\T1, HashKey_2_k(\GDATA)

	GHASH_MUL \T4, \HK, \T1, \T2, \T3, \T5, \T6	//  \T4 = HashKey^3<<1 mod poly
	movdqu	\T4, HashKey_3(\GDATA)
	pshufd	$0b01001110, \T4, \T1
	pxor	\T4, \T1
	movdqu	\T1, HashKey_3_k(\GDATA)


	GHASH_MUL \T4, \HK, \T1, \T2, \T3, \T5, \T6	//  \T4 = HashKey^4<<1 mod poly
	movdqu	\T4, HashKey_4(\GDATA)
	pshufd	$0b01001110, \T4, \T1
	pxor	\T4, \T1
	movdqu	\T1, HashKey_4_k(\GDATA)

	GHASH_MUL \T4, \HK, \T1, \T2, \T3, \T5, \T6	//  \T4 = HashKey^5<<1 mod poly
	movdqu	\T4, HashKey_5(\GDATA)
	pshufd	$0b01001110, \T4, \T1
	pxor	\T4, \T1
	movdqu	\T1, HashKey_5_k(\GDATA)


	GHASH_MUL \T4, \HK, \T1, \T2, \T3, \T5, \T6	//  \T4 = HashKey^6<<1 mod poly
	movdqu	\T4, HashKey_6(\GDATA)
	pshufd	$0b01001110, \T4, \T1
	pxor	\T4, \T1
	movdqu	\T1, HashKey_6_k(\GDATA)

	GHASH_MUL \T4, \HK, \T1, \T2, \T3, \T5, \T6	//  \T4 = HashKey^7<<1 mod poly
	movdqu	\T4, HashKey_7(\GDATA)
	pshufd	$0b01001110, \T4, \T1
	pxor	\T4, \T1
	movdqu	\T1, HashKey_7_k(\GDATA)

	GHASH_MUL \T4, \HK, \T1, \T2, \T3, \T5, \T6	//  \T4 = HashKey^8<<1 mod poly
	movdqu  \T4, HashKey_8(\GDATA)
	pshufd  $0b01001110, \T4, \T1
	pxor    \T4, \T1
	movdqu  \T1, HashKey_8_k(\GDATA)

.endm // PRECOMPUTE


////////////////////////////////////////////////////////////////////////////////
// READ_SMALL_DATA_INPUT: Packs xmm register with data when data input is less
// than 16 bytes.
// Returns 0 if data has length 0.
// Input: The input data (INPUT), that data's length (LENGTH).
// Output: The packed xmm register (OUTPUT).
////////////////////////////////////////////////////////////////////////////////
.macro READ_SMALL_DATA_INPUT	OUTPUT, INPUT, LENGTH, \
				END_READ_LOCATION, COUNTER, TMP1

	// clang compat: no local support
	// LOCAL _byte_loop_1, _byte_loop_2, _done

	pxor	\OUTPUT, \OUTPUT
	mov	\LENGTH, \COUNTER
	mov	\INPUT, \END_READ_LOCATION
	add	\LENGTH, \END_READ_LOCATION
	xor	\TMP1, \TMP1


	cmp	$8, \COUNTER
	jl	_byte_loop_2_\@
	pinsrq	$0, (\INPUT), \OUTPUT	//Read in 8 bytes if they exists
	je	_done_\@

	sub	$8, \COUNTER

_byte_loop_1_\@:		//Read in data 1 byte at a time while data is left
	shl	$8, \TMP1	//This loop handles when 8 bytes were already read in
	dec	\END_READ_LOCATION

	////  mov	BYTE(\TMP1), BYTE [\END_READ_LOCATION]
	bytereg \TMP1
	movb	(\END_READ_LOCATION), breg
	dec	\COUNTER
	jg	_byte_loop_1_\@
	pinsrq	$1, \TMP1, \OUTPUT
	jmp	_done_\@

_byte_loop_2_\@:		//Read in data 1 byte at a time while data is left
	cmp	$0, \COUNTER
	je	_done_\@
	shl	$8, \TMP1	//This loop handles when no bytes were already read in
	dec	\END_READ_LOCATION
	//// mov	BYTE(\TMP1), BYTE [\END_READ_LOCATION]
	bytereg \TMP1
	movb	(\END_READ_LOCATION), breg
	dec	\COUNTER
	jg	_byte_loop_2_\@
	pinsrq	$0, \TMP1, \OUTPUT
_done_\@:

.endm // READ_SMALL_DATA_INPUT


////////////////////////////////////////////////////////////////////////////////
// CALC_AAD_HASH: Calculates the hash of the data which will not be encrypted.
// Input: The input data (A_IN), that data's length (A_LEN), and the hash key
// (HASH_KEY).
// Output: The hash of the data (AAD_HASH).
////////////////////////////////////////////////////////////////////////////////
.macro	CALC_AAD_HASH	A_IN, A_LEN, AAD_HASH, HASH_KEY, XTMP1, XTMP2, XTMP3, \
			XTMP4, XTMP5, T1, T2, T3, T4, T5

	// clang compat: no local support
	// LOCAL _get_AAD_loop16, _get_small_AAD_block, _CALC_AAD_done

	mov	\A_IN, \T1		// T1 = AAD
	mov	\A_LEN, \T2		// T2 = aadLen
	pxor	\AAD_HASH, \AAD_HASH

	cmp	$16, \T2
	jl	_get_small_AAD_block_\@

_get_AAD_loop16_\@:

	movdqu	(\T1), \XTMP1
	//byte-reflect the AAD data
	pshufb	 SHUF_MASK(%rip), \XTMP1
	pxor	\XTMP1, \AAD_HASH
	GHASH_MUL \AAD_HASH, \HASH_KEY, \XTMP1, \XTMP2, \XTMP3, \XTMP4, \XTMP5

	sub	$16, \T2
	je	_CALC_AAD_done_\@

	add	$16, \T1
	cmp	$16, \T2
	jge	_get_AAD_loop16_\@

_get_small_AAD_block_\@:
	READ_SMALL_DATA_INPUT	\XTMP1, \T1, \T2, \T3, \T4, \T5
	//byte-reflect the AAD data
	pshufb	 SHUF_MASK(%rip), \XTMP1
	pxor	\XTMP1, \AAD_HASH
	GHASH_MUL \AAD_HASH, \HASH_KEY, \XTMP1, \XTMP2, \XTMP3, \XTMP4, \XTMP5

_CALC_AAD_done_\@:

.endm // CALC_AAD_HASH



////////////////////////////////////////////////////////////////////////////////
// PARTIAL_BLOCK: Handles encryption/decryption and the tag partial blocks
// between update calls. Requires the input data be at least 1 byte long.
// Input: gcm_key_data (GDATA_KEY), gcm_context_data (GDATA_CTX), input text
// (PLAIN_CYPH_IN), input text length (PLAIN_CYPH_LEN), the current data offset
// (DATA_OFFSET), and whether encoding or decoding (ENC_DEC).
// Output: A cypher of the first partial block (CYPH_PLAIN_OUT), and updated
// GDATA_CTX.
// Clobbers rax, r10, r12, r13, r15, xmm0, xmm1, xmm2, xmm3, xmm5, xmm6, xmm9,
// xmm10, xmm11, xmm13
////////////////////////////////////////////////////////////////////////////////
.macro PARTIAL_BLOCK	GDATA_KEY, GDATA_CTX, CYPH_PLAIN_OUT, PLAIN_CYPH_IN, \
			PLAIN_CYPH_LEN, DATA_OFFSET, AAD_HASH, ENC_DEC

	// clang compat: no local support
	// LOCAL _fewer_than_16_bytes, _data_read, _no_extra_mask_1
	// LOCAL _partial_incomplete_1, _dec_done, _no_extra_mask_2
	// LOCAL _partial_incomplete_2, _encode_done, _partial_fill
	// LOCAL _count_set, _less_than_8_bytes_left, _partial_block_done

	mov	PBlockLen(\GDATA_CTX), %r13
	cmp	$0, %r13
	je	_partial_block_done_\@		//Leave Macro if no partial blocks

	cmp	$16, \PLAIN_CYPH_LEN		//Read in input data without over reading
	jl	_fewer_than_16_bytes_\@
	XLDR	(\PLAIN_CYPH_IN), %xmm1		//If more than 16 bytes of data, just fill the xmm register
	jmp	_data_read_\@

_fewer_than_16_bytes_\@:
	lea	(\PLAIN_CYPH_IN, \DATA_OFFSET), %r10
	READ_SMALL_DATA_INPUT	%xmm1, %r10, \PLAIN_CYPH_LEN, %rax, %r12, %r15
	mov	PBlockLen(\GDATA_CTX), %r13

_data_read_\@:				//Finished reading in data


	movdqu	PBlockEncKey(\GDATA_CTX), %xmm9	//xmm9 = ctx_data.partial_block_enc_key
	movdqu	HashKey(\GDATA_KEY), %xmm13

	lea	SHIFT_MASK(%rip), %r12

	add	%r13, %r12		// adjust the shuffle mask pointer to be able to shift r13 bytes (16-r13 is the number of bytes in plaintext mod 16)
	movdqu	(%r12), %xmm2		// get the appropriate shuffle mask
	pshufb	%xmm2, %xmm9		// shift right r13 bytes

	.ifc	\ENC_DEC, DEC

	movdqa	%xmm1, %xmm3
	pxor	%xmm1, %xmm9		// Cyphertext XOR E(K, Yn)

	mov	\PLAIN_CYPH_LEN, %r15
	add	%r13, %r15
	sub	$16, %r15		//Set r15 to be the amount of data left in CYPH_PLAIN_IN after filling the block
	jge	_no_extra_mask_1_\@	//Determine if if partial block is not being filled and shift mask accordingly
	sub	%r15, %r12
_no_extra_mask_1_\@:

	movdqu	(ALL_F - SHIFT_MASK)(%r12), %xmm1	// get the appropriate mask to mask out bottom r13 bytes of xmm9
	pand	%xmm1, %xmm9			// mask out bottom r13 bytes of xmm9

	pand	%xmm1, %xmm3
	pshufb	SHUF_MASK(%rip), %xmm3
	pshufb	%xmm2, %xmm3
	pxor	%xmm3, \AAD_HASH


	cmp	$0, %r15
	jl	_partial_incomplete_1_\@

	GHASH_MUL \AAD_HASH, %xmm13, %xmm0, %xmm10, %xmm11, %xmm5, %xmm6	//GHASH computation for the last <16 Byte block
	xor	%rax, %rax
	mov	%rax, PBlockLen(\GDATA_CTX)
	jmp	_dec_done_\@
_partial_incomplete_1_\@:
	add	\PLAIN_CYPH_LEN, PBlockLen(\GDATA_CTX)
_dec_done_\@:
	movdqu	\AAD_HASH, AadHash(\GDATA_CTX)

	.else	// .ifc \ENC_DEC, DEC

	pxor	%xmm1, %xmm9		// Plaintext XOR E(K, Yn)

	mov	\PLAIN_CYPH_LEN, %r15
	add	%r13, %r15
	sub	$16, %r15		//Set r15 to be the amount of data left in CYPH_PLAIN_IN after filling the block
	jge	_no_extra_mask_2_\@	//Determine if if partial block is not being filled and shift mask accordingly
	sub	%r15, %r12
_no_extra_mask_2_\@:

	movdqu	(ALL_F - SHIFT_MASK)(%r12), %xmm1  // get the appropriate mask to mask out bottom r13 bytes of xmm9
	pand	%xmm1, %xmm9			 // mask out bottom r13  bytes of xmm9

	pshufb	SHUF_MASK(%rip), %xmm9
	pshufb	%xmm2, %xmm9
	pxor	%xmm9, \AAD_HASH

	cmp	$0, %r15
	jl	_partial_incomplete_2_\@

	GHASH_MUL \AAD_HASH, %xmm13, %xmm0, %xmm10, %xmm11, %xmm5, %xmm6	//GHASH computation for the last <16 Byte block
	xor	%rax, %rax
	mov	%rax, PBlockLen(\GDATA_CTX)
	jmp	_encode_done_\@
_partial_incomplete_2_\@:
	add     \PLAIN_CYPH_LEN, PBlockLen(\GDATA_CTX)
_encode_done_\@:
	movdqu	\AAD_HASH, AadHash(\GDATA_CTX)

	pshufb	SHUF_MASK(%rip), %xmm9	// shuffle xmm9 back to output as ciphertext
	pshufb	%xmm2, %xmm9

	.endif	// .ifc \ENC_DEC, DEC


	//////////////////////////////////////////////////////////
	// output encrypted Bytes
	cmp	$0, %r15
	jl	_partial_fill_\@
	mov	%r13, %r12
	mov	$16, %r13
	sub	%r12, %r13		// Set r13 to be the number of bytes to write out
	jmp	_count_set_\@
_partial_fill_\@:
	mov	\PLAIN_CYPH_LEN, %r13
_count_set_\@:
	movq	%xmm9, %rax
	cmp	$8, %r13
	jle	_less_than_8_bytes_left_\@
	mov	%rax, (\CYPH_PLAIN_OUT, \DATA_OFFSET)
	add	$8, \DATA_OFFSET
	psrldq	$8, %xmm9
	movq	%xmm9, %rax
	sub	$8, %r13
_less_than_8_bytes_left_\@:
	mov	%al, (\CYPH_PLAIN_OUT, \DATA_OFFSET)
	add	$1, \DATA_OFFSET
	shr	$8, %rax
	sub	$1, %r13
	jne	_less_than_8_bytes_left_\@
	//////////////////////////////////////////////////////////
_partial_block_done_\@:
.endm // PARTIAL_BLOCK

////////////////////////////////////////////////////////////////////////////////
// INITIAL_BLOCKS: If a = number of total plaintext bytes; b = floor(a/16);
// \num_initial_blocks = b mod 8;  encrypt the initial \num_initial_blocks
// blocks and apply ghash on the ciphertext.
// \GDATA_KEY, \GDATA_CTX, \CYPH_PLAIN_OUT, \PLAIN_CYPH_IN, r14 are used as a
// pointer only, not modified.
// Updated AAD_HASH is returned in \T3.
////////////////////////////////////////////////////////////////////////////////
.macro INITIAL_BLOCKS	GDATA_KEY, GDATA_CTX, CYPH_PLAIN_OUT, PLAIN_CYPH_IN, \
			LENGTH, DATA_OFFSET, num_initial_blocks, T1, HASH_KEY, \
			T3, T4, T5, CTR, XMM1, XMM2, XMM3, XMM4, XMM5, XMM6, \
			XMM7, XMM8, T6, T_key, ENC_DEC

	// clang compat: no local support
	// LOCAL _initial_blocks_done

.altmacro
.set i, (8-\num_initial_blocks)
		xmmreg	i, %i
		movdqu	\XMM8, xmmi	// move AAD_HASH to temp reg

		// start AES for \num_initial_blocks blocks
		movdqu	CurCount(\GDATA_CTX), \CTR	// \CTR = Y0


.set i, (9-\num_initial_blocks)
.rept \num_initial_blocks
		xmmreg	i, %i
		paddd	ONE(%rip), \CTR			// INCR Y0
		movdqa	\CTR, xmmi
		pshufb	SHUF_MASK(%rip), xmmi		// perform a 16Byte swap
.set i, (i+1)
.endr

movdqu	16*0(\GDATA_KEY), \T_key
.set i, (9-\num_initial_blocks)
.rept \num_initial_blocks
		xmmreg	i, %i
		pxor	\T_key, xmmi
.set i, (i+1)
.endr

.set j, 1
.rept NROUNDS						// encrypt N blocks with 13 key rounds (11 for GCM192)
movdqu	16*j(\GDATA_KEY), \T_key
.set i, (9-\num_initial_blocks)
.rept \num_initial_blocks
		xmmreg	i, %i
		aesenc	\T_key, xmmi
.set i, (i+1)
.endr

.set j, (j+1)
.endr

movdqu	16*j(\GDATA_KEY), \T_key			// encrypt with last (14th) key round (12 for GCM192)
.set i, (9-\num_initial_blocks)
.rept \num_initial_blocks
		xmmreg	i, %i
		aesenclast	\T_key, xmmi
.set i, (i+1)
.endr

.set i, (9-\num_initial_blocks)
.rept \num_initial_blocks
		xmmreg	i, %i
		XLDR	(\PLAIN_CYPH_IN, \DATA_OFFSET), \T1
		pxor	\T1, xmmi
		XSTR	xmmi, (\CYPH_PLAIN_OUT, \DATA_OFFSET)	// write back ciphertext for \num_initial_blocks blocks
		add	$16, \DATA_OFFSET
		.ifc \ENC_DEC, DEC
		movdqa	\T1, xmmi
		.endif
		pshufb	SHUF_MASK(%rip), xmmi      // prepare ciphertext for GHASH computations
.set i, (i+1)
.endr


.set i, (8-\num_initial_blocks)
.set j, (9-\num_initial_blocks)
.rept \num_initial_blocks
	xmmreg	i, %i
	xmmreg	j, %j
	pxor	xmmi, xmmj
	GHASH_MUL xmmj, <\HASH_KEY>, <\T1>, <\T3>, <\T4>, <\T5>, <\T6>	// apply GHASH on \num_initial_blocks blocks
.set i, (i+1)
.set j, (j+1)
.endr
.noaltmacro

	// \XMM8 has the current Hash Value
	movdqa	\XMM8, \T3

	cmp	$128, \LENGTH
	jl	_initial_blocks_done_\@	// no need for precomputed constants

////////////////////////////////////////////////////////////////////////////////
// Haskey_i_k holds XORed values of the low and high parts of the Haskey_i
		paddd   ONE(%rip), \CTR		// INCR Y0
		movdqa  \CTR, \XMM1
		pshufb  SHUF_MASK(%rip), \XMM1	// perform a 16Byte swap

		paddd   ONE(%rip), \CTR		// INCR Y0
		movdqa  \CTR, \XMM2
		pshufb  SHUF_MASK(%rip), \XMM2	// perform a 16Byte swap

		paddd   ONE(%rip), \CTR		// INCR Y0
		movdqa  \CTR, \XMM3
		pshufb  SHUF_MASK(%rip), \XMM3	// perform a 16Byte swap

		paddd   ONE(%rip), \CTR		// INCR Y0
		movdqa  \CTR, \XMM4
		pshufb  SHUF_MASK(%rip), \XMM4	// perform a 16Byte swap

		paddd   ONE(%rip), \CTR		// INCR Y0
		movdqa  \CTR, \XMM5
		pshufb  SHUF_MASK(%rip), \XMM5	// perform a 16Byte swap

		paddd   ONE(%rip), \CTR		// INCR Y0
		movdqa  \CTR, \XMM6
		pshufb  SHUF_MASK(%rip), \XMM6	// perform a 16Byte swap

		paddd   ONE(%rip), \CTR		// INCR Y0
		movdqa  \CTR, \XMM7
		pshufb  SHUF_MASK(%rip), \XMM7	// perform a 16Byte swap

		paddd   ONE(%rip), \CTR		// INCR Y0
		movdqa  \CTR, \XMM8
		pshufb  SHUF_MASK(%rip), \XMM8	// perform a 16Byte swap

		movdqu  16*0(\GDATA_KEY), \T_key
		pxor	\T_key, \XMM1
		pxor	\T_key, \XMM2
		pxor	\T_key, \XMM3
		pxor	\T_key, \XMM4
		pxor	\T_key, \XMM5
		pxor	\T_key, \XMM6
		pxor	\T_key, \XMM7
		pxor	\T_key, \XMM8

.set i, 1
.rept    NROUNDS			// do early (13) rounds (11 for GCM192)
		movdqu  16*i(\GDATA_KEY), \T_key
		aesenc	\T_key, \XMM1
		aesenc	\T_key, \XMM2
		aesenc	\T_key, \XMM3
		aesenc	\T_key, \XMM4
		aesenc	\T_key, \XMM5
		aesenc	\T_key, \XMM6
		aesenc	\T_key, \XMM7
		aesenc	\T_key, \XMM8
.set i, (i+1)
.endr

		movdqu	16*i(\GDATA_KEY), \T_key	// do final key round
		aesenclast	\T_key, \XMM1
		aesenclast	\T_key, \XMM2
		aesenclast	\T_key, \XMM3
		aesenclast	\T_key, \XMM4
		aesenclast	\T_key, \XMM5
		aesenclast	\T_key, \XMM6
		aesenclast	\T_key, \XMM7
		aesenclast	\T_key, \XMM8

		XLDR	16*0(\PLAIN_CYPH_IN, \DATA_OFFSET), \T1
		pxor	\T1, \XMM1
		XSTR	\XMM1, 16*0(\CYPH_PLAIN_OUT, \DATA_OFFSET)
		.ifc	\ENC_DEC, DEC
		movdqa	\T1, \XMM1
		.endif

		XLDR	16*1(\PLAIN_CYPH_IN, \DATA_OFFSET), \T1
		pxor	\T1, \XMM2
		XSTR	\XMM2, 16*1(\CYPH_PLAIN_OUT, \DATA_OFFSET)
		.ifc	\ENC_DEC, DEC
		movdqa	\T1, \XMM2
		.endif

		XLDR	16*2(\PLAIN_CYPH_IN, \DATA_OFFSET), \T1
		pxor	\T1, \XMM3
		XSTR	\XMM3, 16*2(\CYPH_PLAIN_OUT, \DATA_OFFSET)
		.ifc	\ENC_DEC, DEC
		movdqa	\T1, \XMM3
		.endif

		XLDR	16*3(\PLAIN_CYPH_IN, \DATA_OFFSET), \T1
		pxor	\T1, \XMM4
		XSTR	\XMM4, 16*3(\CYPH_PLAIN_OUT, \DATA_OFFSET)
		.ifc	\ENC_DEC, DEC
		movdqa	\T1, \XMM4
		.endif

		XLDR	16*4(\PLAIN_CYPH_IN, \DATA_OFFSET), \T1
		pxor	\T1, \XMM5
		XSTR	\XMM5, 16*4(\CYPH_PLAIN_OUT, \DATA_OFFSET)
		.ifc	\ENC_DEC, DEC
		movdqa	\T1, \XMM5
		.endif

		XLDR	16*5(\PLAIN_CYPH_IN, \DATA_OFFSET), \T1
		pxor	\T1, \XMM6
		XSTR	\XMM6, 16*5(\CYPH_PLAIN_OUT, \DATA_OFFSET)
		.ifc	\ENC_DEC, DEC
		movdqa	\T1, \XMM6
		.endif

		XLDR	16*6(\PLAIN_CYPH_IN, \DATA_OFFSET), \T1
		pxor	\T1, \XMM7
		XSTR	\XMM7, 16*6(\CYPH_PLAIN_OUT, \DATA_OFFSET)
		.ifc	\ENC_DEC, DEC
		movdqa	\T1, \XMM7
		.endif

		XLDR	16*7(\PLAIN_CYPH_IN, \DATA_OFFSET), \T1
		pxor	\T1, \XMM8
		XSTR	\XMM8, 16*7(\CYPH_PLAIN_OUT, \DATA_OFFSET)
		.ifc	\ENC_DEC, DEC
		movdqa	\T1, \XMM8
		.endif

		add	$128, \DATA_OFFSET

		pshufb  SHUF_MASK(%rip), \XMM1	// perform a 16Byte swap
		pxor	\T3, \XMM1		// combine GHASHed value with the corresponding ciphertext
		pshufb  SHUF_MASK(%rip), \XMM2	// perform a 16Byte swap
		pshufb  SHUF_MASK(%rip), \XMM3	// perform a 16Byte swap
		pshufb  SHUF_MASK(%rip), \XMM4	// perform a 16Byte swap
		pshufb  SHUF_MASK(%rip), \XMM5	// perform a 16Byte swap
		pshufb  SHUF_MASK(%rip), \XMM6	// perform a 16Byte swap
		pshufb  SHUF_MASK(%rip), \XMM7	// perform a 16Byte swap
		pshufb  SHUF_MASK(%rip), \XMM8	// perform a 16Byte swap

////////////////////////////////////////////////////////////////////////////////

_initial_blocks_done_\@:
.noaltmacro
.endm // INITIAL_BLOCKS


////////////////////////////////////////////////////////////////////////////////
// GHASH_8_ENCRYPT_8_PARALLEL: Encrypt 8 blocks at a time and ghash the 8
// previously encrypted ciphertext blocks.
// \GDATA (KEY), \CYPH_PLAIN_OUT, \PLAIN_CYPH_IN are used as pointers only,
// not modified.
// \DATA_OFFSET is the data offset value
////////////////////////////////////////////////////////////////////////////////
.macro GHASH_8_ENCRYPT_8_PARALLEL GDATA, CYPH_PLAIN_OUT, PLAIN_CYPH_IN, \
				  DATA_OFFSET, T1, T2, T3, T4, T5, T6, CTR, \
				  XMM1, XMM2, XMM3, XMM4, XMM5, XMM6, XMM7, \
				  XMM8, T7, loop_idx, ENC_DEC


	movdqa	\XMM1, \T7
	movdqu	\XMM2, TMP2(%rsp)
	movdqu	\XMM3, TMP3(%rsp)
	movdqu	\XMM4, TMP4(%rsp)
	movdqu	\XMM5, TMP5(%rsp)
	movdqu	\XMM6, TMP6(%rsp)
	movdqu	\XMM7, TMP7(%rsp)
	movdqu	\XMM8, TMP8(%rsp)

	////////////////////////////////////////////////////////////////////////
	//// Karatsuba Method

	movdqa	\T7, \T4
	pshufd	$0b01001110, \T7, \T6
	pxor	\T7, \T6
		.ifc \loop_idx, in_order
		paddd	ONE(%rip), \CTR			// INCR CNT
		.else
		paddd	ONEf(%rip), \CTR			// INCR CNT
		.endif
	movdqu	HashKey_8(\GDATA), \T5
	pclmulqdq	 $0x11, \T5, \T4		// \T1 = a1*b1
	pclmulqdq	$0x00, \T5, \T7			// \T7 = a0*b0
	movdqu	HashKey_8_k(\GDATA), \T5
	pclmulqdq	$0x00, \T5, \T6			// \T2 = (a1+a0)*(b1+b0)
		movdqa	\CTR, \XMM1

		.ifc \loop_idx, in_order

		paddd	ONE(%rip), \CTR			// INCR CNT
		movdqa	\CTR, \XMM2

		paddd	ONE(%rip), \CTR			// INCR CNT
		movdqa	\CTR, \XMM3

		paddd	ONE(%rip), \CTR			// INCR CNT
		movdqa	\CTR, \XMM4

		paddd	ONE(%rip), \CTR			// INCR CNT
		movdqa	\CTR, \XMM5

		paddd	ONE(%rip), \CTR			// INCR CNT
		movdqa	\CTR, \XMM6

		paddd	ONE(%rip), \CTR			// INCR CNT
		movdqa	\CTR, \XMM7

		paddd	ONE(%rip), \CTR			// INCR CNT
		movdqa	\CTR, \XMM8

		pshufb	SHUF_MASK(%rip), \XMM1		// perform a 16Byte swap
		pshufb	SHUF_MASK(%rip), \XMM2		// perform a 16Byte swap
		pshufb	SHUF_MASK(%rip), \XMM3		// perform a 16Byte swap
		pshufb	SHUF_MASK(%rip), \XMM4		// perform a 16Byte swap
		pshufb	SHUF_MASK(%rip), \XMM5		// perform a 16Byte swap
		pshufb	SHUF_MASK(%rip), \XMM6		// perform a 16Byte swap
		pshufb	SHUF_MASK(%rip), \XMM7		// perform a 16Byte swap
		pshufb	SHUF_MASK(%rip), \XMM8		// perform a 16Byte swap

		.else	// .ifc \loop_idx, in_order

		paddd	ONEf(%rip), \CTR		// INCR CNT
		movdqa	\CTR, \XMM2

		paddd	ONEf(%rip), \CTR		// INCR CNT
		movdqa	\CTR, \XMM3

		paddd	ONEf(%rip), \CTR		// INCR CNT
		movdqa	\CTR, \XMM4

		paddd	ONEf(%rip), \CTR		// INCR CNT
		movdqa	\CTR, \XMM5

		paddd	ONEf(%rip), \CTR		// INCR CNT
		movdqa	\CTR, \XMM6

		paddd	ONEf(%rip), \CTR		// INCR CNT
		movdqa	\CTR, \XMM7

		paddd	ONEf(%rip), \CTR		// INCR CNT
		movdqa	\CTR, \XMM8

		.endif	// .ifc \loop_idx, in_order
	////////////////////////////////////////////////////////////////////////

		movdqu	16*0(\GDATA), \T1
		pxor	\T1, \XMM1
		pxor	\T1, \XMM2
		pxor	\T1, \XMM3
		pxor	\T1, \XMM4
		pxor	\T1, \XMM5
		pxor	\T1, \XMM6
		pxor	\T1, \XMM7
		pxor	\T1, \XMM8

	// \XMM6, \T5 hold the values for the two operands which are
	// carry-less multiplied
	////////////////////////////////////////////////////////////////////////
	// Karatsuba Method
	movdqu	TMP2(%rsp), \T1
	movdqa	\T1, \T3

	pshufd	$0b01001110, \T3, \T2
	pxor	\T3, \T2
	movdqu	HashKey_7(\GDATA), \T5
	pclmulqdq	$0x11, \T5, \T1			// \T1 = a1*b1
	pclmulqdq	$0x00, \T5, \T3			// \T3 = a0*b0
	movdqu  HashKey_7_k(\GDATA), \T5
	pclmulqdq	$0x00, \T5, \T2			// \T2 = (a1+a0)*(b1+b0)
	pxor	\T1, \T4				// accumulate the results in \T4:\T7, \T6 holds the middle part
	pxor	\T3, \T7
	pxor	\T2, \T6

		movdqu	16*1(\GDATA), \T1
		aesenc	\T1, \XMM1
		aesenc	\T1, \XMM2
		aesenc	\T1, \XMM3
		aesenc	\T1, \XMM4
		aesenc	\T1, \XMM5
		aesenc	\T1, \XMM6
		aesenc	\T1, \XMM7
		aesenc	\T1, \XMM8

		movdqu	16*2(\GDATA), \T1
		aesenc	\T1, \XMM1
		aesenc	\T1, \XMM2
		aesenc	\T1, \XMM3
		aesenc	\T1, \XMM4
		aesenc	\T1, \XMM5
		aesenc	\T1, \XMM6
		aesenc	\T1, \XMM7
		aesenc	\T1, \XMM8

	////////////////////////////////////////////////////////////////////////
	// Karatsuba Method
	movdqu	TMP3(%rsp), \T1
	movdqa	\T1, \T3

	pshufd	$0b01001110, \T3, \T2
	pxor	\T3, \T2
	movdqu	HashKey_6(\GDATA), \T5
	pclmulqdq	$0x11, \T5, \T1		// \T1 = a1*b1
	pclmulqdq	$0x00, \T5, \T3		// \T3 = a0*b0
	movdqu  HashKey_6_k(\GDATA), \T5
	pclmulqdq	$0x00, \T5, \T2		// \T2 = (a1+a0)*(b1+b0)
	pxor	\T1, \T4			// accumulate the results in \T4:\T7, \T6 holds the middle part
	pxor	\T3, \T7
	pxor	\T2, \T6

		movdqu	16*3(\GDATA), \T1
		aesenc	\T1, \XMM1
		aesenc	\T1, \XMM2
		aesenc	\T1, \XMM3
		aesenc	\T1, \XMM4
		aesenc	\T1, \XMM5
		aesenc	\T1, \XMM6
		aesenc	\T1, \XMM7
		aesenc	\T1, \XMM8

	movdqu	TMP4(%rsp), \T1
	movdqa	\T1, \T3

	pshufd	$0b01001110, \T3, \T2
	pxor	\T3, \T2
	movdqu	HashKey_5(\GDATA), \T5
	pclmulqdq	$0x11, \T5, \T1		// \T1 = a1*b1
	pclmulqdq	$0x00, \T5, \T3		// \T3 = a0*b0
	movdqu  HashKey_5_k(\GDATA), \T5
	pclmulqdq	$0x00, \T5, \T2		// \T2 = (a1+a0)*(b1+b0)
	pxor	\T1, \T4			// accumulate the results in \T4:\T7, \T6 holds the middle part
	pxor	\T3, \T7
	pxor	\T2, \T6

		movdqu	16*4(\GDATA), \T1
		aesenc	\T1, \XMM1
		aesenc	\T1, \XMM2
		aesenc	\T1, \XMM3
		aesenc	\T1, \XMM4
		aesenc	\T1, \XMM5
		aesenc	\T1, \XMM6
		aesenc	\T1, \XMM7
		aesenc	\T1, \XMM8

		movdqu	16*5(\GDATA), \T1
		aesenc	\T1, \XMM1
		aesenc	\T1, \XMM2
		aesenc	\T1, \XMM3
		aesenc	\T1, \XMM4
		aesenc	\T1, \XMM5
		aesenc	\T1, \XMM6
		aesenc	\T1, \XMM7
		aesenc	\T1, \XMM8

	movdqu	TMP5(%rsp), \T1
	movdqa	\T1, \T3

	pshufd	$0b01001110, \T3, \T2
	pxor	\T3, \T2
	movdqu	HashKey_4(\GDATA), \T5
	pclmulqdq	$0x11, \T5, \T1		// \T1 = a1*b1
	pclmulqdq	$0x00, \T5, \T3		// \T3 = a0*b0
	movdqu  HashKey_4_k(\GDATA), \T5
	pclmulqdq	$0x00, \T5, \T2		// \T2 = (a1+a0)*(b1+b0)
	pxor	\T1, \T4			// accumulate the results in \T4:\T7, \T6 holds the middle part
	pxor	\T3, \T7
	pxor	\T2, \T6

		movdqu	16*6(\GDATA), \T1
		aesenc	\T1, \XMM1
		aesenc	\T1, \XMM2
		aesenc	\T1, \XMM3
		aesenc	\T1, \XMM4
		aesenc	\T1, \XMM5
		aesenc	\T1, \XMM6
		aesenc	\T1, \XMM7
		aesenc	\T1, \XMM8


	movdqu	TMP6(%rsp), \T1
	movdqa	\T1, \T3

	pshufd	$0b01001110, \T3, \T2
	pxor	\T3, \T2
	movdqu	HashKey_3(\GDATA), \T5
	pclmulqdq	$0x11, \T5, \T1		// \T1 = a1*b1
	pclmulqdq	$0x00, \T5, \T3		// \T3 = a0*b0
	movdqu  HashKey_3_k(\GDATA), \T5
	pclmulqdq	$0x00, \T5, \T2		// \T2 = (a1+a0)*(b1+b0)
	pxor	\T1, \T4			// accumulate the results in \T4:\T7, \T6 holds the middle part
	pxor	\T3, \T7
	pxor	\T2, \T6

		movdqu	16*7(\GDATA), \T1
		aesenc	\T1, \XMM1
		aesenc	\T1, \XMM2
		aesenc	\T1, \XMM3
		aesenc	\T1, \XMM4
		aesenc	\T1, \XMM5
		aesenc	\T1, \XMM6
		aesenc	\T1, \XMM7
		aesenc	\T1, \XMM8

	movdqu	TMP7(%rsp), \T1
	movdqa	\T1, \T3

	pshufd	$0b01001110, \T3, \T2
	pxor	\T3, \T2
	movdqu	HashKey_2(\GDATA), \T5
	pclmulqdq	$0x11, \T5, \T1			// \T1 = a1*b1
	pclmulqdq	$0x00, \T5, \T3			// \T3 = a0*b0
	movdqu  HashKey_2_k(\GDATA), \T5
	pclmulqdq	$0x00, \T5, \T2			// \T2 = (a1+a0)*(b1+b0)
	pxor	\T1, \T4				// accumulate the results in \T4:\T7, \T6 holds the middle part
	pxor	\T3, \T7
	pxor	\T2, \T6

		movdqu	16*8(\GDATA), \T1
		aesenc	\T1, \XMM1
		aesenc	\T1, \XMM2
		aesenc	\T1, \XMM3
		aesenc	\T1, \XMM4
		aesenc	\T1, \XMM5
		aesenc	\T1, \XMM6
		aesenc	\T1, \XMM7
		aesenc	\T1, \XMM8


	// \XMM8, \T5 hold the values for the two operands which are
	// carry-less multiplied.
	////////////////////////////////////////////////////////////////////////
	// Karatsuba Method
	movdqu	TMP8(%rsp), \T1
	movdqa	\T1, \T3

	pshufd	$0b01001110, \T3, \T2
	pxor	\T3, \T2
	movdqu	HashKey(\GDATA), \T5
	pclmulqdq	$0x11, \T5, \T1		// \T1 = a1*b1
	pclmulqdq	$0x00, \T5, \T3		// \T3 = a0*b0
	movdqu  HashKey_k(\GDATA), \T5
	pclmulqdq	$0x00, \T5, \T2		// \T2 = (a1+a0)*(b1+b0)
	pxor	\T3, \T7
	pxor	\T1, \T4			// accumulate the results in \T4:\T7, \T6 holds the middle part

		movdqu	16*9(\GDATA), \T1
		aesenc	\T1, \XMM1
		aesenc	\T1, \XMM2
		aesenc	\T1, \XMM3
		aesenc	\T1, \XMM4
		aesenc	\T1, \XMM5
		aesenc	\T1, \XMM6
		aesenc	\T1, \XMM7
		aesenc	\T1, \XMM8


#ifdef GCM128_MODE
		movdqu	16*10(\GDATA), \T5
#endif
#ifdef GCM192_MODE
		movdqu	16*10(\GDATA), \T1
		aesenc	\T1, \XMM1
		aesenc	\T1, \XMM2
		aesenc	\T1, \XMM3
		aesenc	\T1, \XMM4
		aesenc	\T1, \XMM5
		aesenc	\T1, \XMM6
		aesenc	\T1, \XMM7
		aesenc	\T1, \XMM8

		movdqu	16*11(\GDATA), \T1
		aesenc	\T1, \XMM1
		aesenc	\T1, \XMM2
		aesenc	\T1, \XMM3
		aesenc	\T1, \XMM4
		aesenc	\T1, \XMM5
		aesenc	\T1, \XMM6
		aesenc	\T1, \XMM7
		aesenc	\T1, \XMM8

		movdqu	16*12(\GDATA), \T5	// finish last key round
#endif
#ifdef GCM256_MODE
		movdqu	16*10(\GDATA), \T1
		aesenc	\T1, \XMM1
		aesenc	\T1, \XMM2
		aesenc	\T1, \XMM3
		aesenc	\T1, \XMM4
		aesenc	\T1, \XMM5
		aesenc	\T1, \XMM6
		aesenc	\T1, \XMM7
		aesenc	\T1, \XMM8

		movdqu	16*11(\GDATA), \T1
		aesenc	\T1, \XMM1
		aesenc	\T1, \XMM2
		aesenc	\T1, \XMM3
		aesenc	\T1, \XMM4
		aesenc	\T1, \XMM5
		aesenc	\T1, \XMM6
		aesenc	\T1, \XMM7
		aesenc	\T1, \XMM8

		movdqu	16*12(\GDATA), \T1
		aesenc	\T1, \XMM1
		aesenc	\T1, \XMM2
		aesenc	\T1, \XMM3
		aesenc	\T1, \XMM4
		aesenc	\T1, \XMM5
		aesenc	\T1, \XMM6
		aesenc	\T1, \XMM7
		aesenc	\T1, \XMM8

		movdqu	16*13(\GDATA), \T1
		aesenc	\T1, \XMM1
		aesenc	\T1, \XMM2
		aesenc	\T1, \XMM3
		aesenc	\T1, \XMM4
		aesenc	\T1, \XMM5
		aesenc	\T1, \XMM6
		aesenc	\T1, \XMM7
		aesenc	\T1, \XMM8

	movdqu	16*14(\GDATA), \T5		// finish last key round
#endif

.altmacro
.set i, 0
.set j, 1
.rept 8
		xmmreg j, %j
		XLDR	16*i(\PLAIN_CYPH_IN, \DATA_OFFSET), \T1

		.ifc \ENC_DEC, DEC
		movdqa	\T1, \T3
		.endif

		pxor	\T5, \T1
		aesenclast	\T1, xmmj				// XMM1:XMM8
		XSTR	xmmj, 16*i(\CYPH_PLAIN_OUT, \DATA_OFFSET)	// Write to the Output buffer

		.ifc \ENC_DEC, DEC
		movdqa	\T3, xmmj
		.endif
.set i, (i+1)
.set j, (j+1)
.endr
.noaltmacro

	pxor	\T6, \T2
	pxor	\T4, \T2
	pxor	\T7, \T2


	movdqa	\T2, \T3
	pslldq	$8, \T3			// shift-L \T3 2 DWs
	psrldq	$8, \T2			// shift-R \T2 2 DWs
	pxor	\T3, \T7
	pxor	\T2, \T4		// accumulate the results in \T4:\T7



	//first phase of the reduction
	movdqa	\T7, \T2
	movdqa	\T7, \T3
	movdqa	\T7, \T1		// move \T7 into \T2, \T3, \T1 in order to perform the three shifts independently

	pslld	$31, \T2		// packed right shifting << 31
	pslld	$30, \T3		// packed right shifting shift << 30
	pslld	$25, \T1		// packed right shifting shift << 25
	pxor	\T3, \T2		// xor the shifted versions
	pxor	\T1, \T2

	movdqa	\T2, \T5
	psrldq	$4, \T5			// shift-R \T5 1 DW

	pslldq	$12, \T2		// shift-L \T2 3 DWs
	pxor	\T2, \T7		// first phase of the reduction complete

	////////////////////////////////////////////////////////////////////////

		pshufb	SHUF_MASK(%rip), \XMM1	// perform a 16Byte swap
		pshufb	SHUF_MASK(%rip), \XMM2	// perform a 16Byte swap
		pshufb	SHUF_MASK(%rip), \XMM3	// perform a 16Byte swap
		pshufb	SHUF_MASK(%rip), \XMM4	// perform a 16Byte swap
		pshufb	SHUF_MASK(%rip), \XMM5	// perform a 16Byte swap
		pshufb	SHUF_MASK(%rip), \XMM6	// perform a 16Byte swap
		pshufb	SHUF_MASK(%rip), \XMM7	// perform a 16Byte swap
		pshufb	SHUF_MASK(%rip), \XMM8	// perform a 16Byte swap

	//second phase of the reduction
	movdqa	\T7, \T2		// make 3 copies of \T7 (in in \T2, \T3, \T1) for doing three shift operations
	movdqa	\T7, \T3
	movdqa	\T7, \T1

	psrld	$1, \T2			// packed left shifting >> 1
	psrld	$2, \T3			// packed left shifting >> 2
	psrld	$7, \T1			// packed left shifting >> 7
	pxor	\T3, \T2		// xor the shifted versions
	pxor	\T1, \T2

	pxor	\T5, \T2
	pxor	\T2, \T7
	pxor	\T4, \T7		// the result is in \T4


	pxor    \T7, \XMM1

.endm // GHASH_8_ENCRYPT_8_PARALLEL

////////////////////////////////////////////////////////////////////////////////
// GHASH_LAST_8: GHASH the last 8 ciphertext blocks.
////////////////////////////////////////////////////////////////////////////////
.macro	GHASH_LAST_8	GDATA, T1, T2, T3, T4, T5, T6, T7, \
			XMM1, XMM2, XMM3, XMM4, XMM5, XMM6, XMM7, XMM8


	// Karatsuba Method
	movdqa	\XMM1, \T6
	pshufd	$0b01001110,  \XMM1, \T2
	pxor	\XMM1, \T2
	movdqu	HashKey_8(\GDATA), \T5
	pclmulqdq	$0x11, \T5, \T6		// \T6 = a1*b1

	pclmulqdq	$0x00, \T5, \XMM1	// \XMM1 = a0*b0
	movdqu	HashKey_8_k(\GDATA), \T4
	pclmulqdq	$0x00, \T4, \T2		// \T2 = (a1+a0)*(b1+b0)

	movdqa	\XMM1, \T7
	movdqa	\T2, \XMM1			// result in \T6, \T7, \XMM1

	// Karatsuba Method
	movdqa	\XMM2, \T1
	pshufd	$0b01001110,  \XMM2, \T2
	pxor	\XMM2, \T2
	movdqu	HashKey_7(\GDATA), \T5
	pclmulqdq	$0x11, \T5, \T1		// \T1 = a1*b1

	pclmulqdq	$0x00, \T5, \XMM2	// \XMM2 = a0*b0
	movdqu	HashKey_7_k(\GDATA), \T4
	pclmulqdq	$0x00, \T4, \T2		// \T2 = (a1+a0)*(b1+b0)

	pxor	\T1, \T6
	pxor	\XMM2, \T7
	pxor	\T2, \XMM1			// results accumulated in \T6, \T7, \XMM1

	// Karatsuba Method
	movdqa	\XMM3, \T1
	pshufd	$0b01001110,  \XMM3, \T2
	pxor	\XMM3, \T2
	movdqu	HashKey_6(\GDATA), \T5
	pclmulqdq	$0x11, \T5, \T1		// \T1 = a1*b1

	pclmulqdq	$0x00, \T5, \XMM3	// \XMM3 = a0*b0
	movdqu	HashKey_6_k(\GDATA), \T4
	pclmulqdq	$0x00, \T4, \T2		// \T2 = (a1+a0)*(b1+b0)

	pxor	\T1, \T6
	pxor	\XMM3, \T7
	pxor	\T2, \XMM1			// results accumulated in \T6, \T7, \XMM1

	// Karatsuba Method
	movdqa	\XMM4, \T1
	pshufd	$0b01001110,  \XMM4, \T2
	pxor	\XMM4, \T2
	movdqu	HashKey_5(\GDATA), \T5
	pclmulqdq	$0x11, \T5, \T1		// \T1 = a1*b1

	pclmulqdq	$0x00, \T5, \XMM4	// \XMM4 = a0*b0
	movdqu	HashKey_5_k(\GDATA), \T4
	pclmulqdq	$0x00, \T4, \T2		// \T2 = (a1+a0)*(b1+b0)

	pxor	\T1, \T6
	pxor	\XMM4, \T7
	pxor	\T2, \XMM1			// results accumulated in \T6, \T7, \XMM1

	// Karatsuba Method
	movdqa	\XMM5, \T1
	pshufd	$0b01001110,  \XMM5, \T2
	pxor	\XMM5, \T2
	movdqu	HashKey_4(\GDATA), \T5
	pclmulqdq	$0x11, \T5, \T1		// \T1 = a1*b1

	pclmulqdq	$0x00, \T5, \XMM5	// \XMM5 = a0*b0
	movdqu	HashKey_4_k(\GDATA), \T4
	pclmulqdq	$0x00, \T4, \T2		// \T2 = (a1+a0)*(b1+b0)

	pxor	\T1, \T6
	pxor	\XMM5, \T7
	pxor	\T2, \XMM1			// results accumulated in \T6, \T7, \XMM1

	// Karatsuba Method
	movdqa	\XMM6, \T1
	pshufd	$0b01001110,  \XMM6, \T2
	pxor	\XMM6, \T2
	movdqu	HashKey_3(\GDATA), \T5
	pclmulqdq	$0x11, \T5, \T1		// \T1 = a1*b1

	pclmulqdq	$0x00, \T5, \XMM6	// \XMM6 = a0*b0
	movdqu	HashKey_3_k(\GDATA), \T4
	pclmulqdq	$0x00, \T4, \T2		// \T2 = (a1+a0)*(b1+b0)

	pxor	\T1, \T6
	pxor	\XMM6, \T7
	pxor	\T2, \XMM1			// results accumulated in \T6, \T7, \XMM1

	// Karatsuba Method
	movdqa	\XMM7, \T1
	pshufd	$0b01001110,  \XMM7, \T2
	pxor	\XMM7, \T2
	movdqu	HashKey_2(\GDATA), \T5
	pclmulqdq	$0x11, \T5, \T1		// \T1 = a1*b1

	pclmulqdq	$0x00, \T5, \XMM7	// \XMM7 = a0*b0
	movdqu	HashKey_2_k(\GDATA), \T4
	pclmulqdq	$0x00, \T4, \T2		// \T2 = (a1+a0)*(b1+b0)

	pxor	\T1, \T6
	pxor	\XMM7, \T7
	pxor	\T2, \XMM1			// results accumulated in \T6, \T7, \XMM1


	// Karatsuba Method
	movdqa	\XMM8, \T1
	pshufd	$0b01001110,  \XMM8, \T2
	pxor	\XMM8, \T2
	movdqu	HashKey(\GDATA), \T5
	pclmulqdq	$0x11, \T5, \T1		// \T1 = a1*b1

	pclmulqdq	$0x00, \T5, \XMM8	// \XMM8 = a0*b0
	movdqu	HashKey_k(\GDATA), \T4
	pclmulqdq	$0x00, \T4, \T2		// \T2 = (a1+a0)*(b1+b0)

	pxor	\T1, \T6
	pxor	\XMM8, \T7
	pxor	\XMM1, \T2
	pxor	\T6, \T2
	pxor	\T7, \T2		// middle section of the temp results combined as in Karatsuba algorithm


	movdqa	\T2, \T4
	pslldq	$8, \T4			// shift-L \T4 2 DWs
	psrldq	$8, \T2			// shift-R \T2 2 DWs
	pxor	\T4, \T7
	pxor	\T2, \T6		// <\T6:\T7> holds the result of the accumulated carry-less multiplications


	//first phase of the reduction
	movdqa	\T7, \T2
	movdqa	\T7, \T3
	movdqa	\T7, \T4		// move \T7 into \T2, \T3, \T4 in order to perform the three shifts independently

	pslld	$31, \T2		// packed right shifting << 31
	pslld	$30, \T3		// packed right shifting shift << 30
	pslld	$25, \T4		// packed right shifting shift << 25
	pxor	\T3, \T2		// xor the shifted versions
	pxor	\T4, \T2

	movdqa	\T2, \T1
	psrldq	$4, \T1			// shift-R \T1 1 DW

	pslldq	$12, \T2		// shift-L \T2 3 DWs
	pxor	\T2, \T7		// first phase of the reduction complete
	////////////////////////////////////////////////////////////////////////

	//second phase of the reduction
	movdqa	\T7, \T2		// make 3 copies of \T7 (in in \T2, \T3, \T4) for doing three shift operations
	movdqa	\T7, \T3
	movdqa	\T7, \T4

	psrld	$1, \T2			// packed left shifting >> 1
	psrld	$2, \T3			// packed left shifting >> 2
	psrld	$7, \T4			// packed left shifting >> 7
	pxor	\T3, \T2		// xor the shifted versions
	pxor	\T4, \T2

	pxor	\T1, \T2
	pxor	\T2, \T7
	pxor	\T7, \T6		// the result is in \T6

.endm // GHASH_LAST_8

////////////////////////////////////////////////////////////////////////////////
// ENCRYPT_SINGLE_BLOCK: Encrypt a single block.
////////////////////////////////////////////////////////////////////////////////
.macro ENCRYPT_SINGLE_BLOCK	GDATA, ST, T1

		movdqu	16*0(\GDATA), \T1
		pxor	\T1, \ST

.set i, 1
.rept NROUNDS
		movdqu	16*i(\GDATA), \T1
		aesenc	\T1, \ST

.set i, (i+1)
.endr
		movdqu	16*i(\GDATA), \T1
		aesenclast	\T1, \ST
.endm // ENCRYPT_SINGLE_BLOCK


////////////////////////////////////////////////////////////////////////////////
// FUNC_SAVE: Save clobbered regs on the stack.
////////////////////////////////////////////////////////////////////////////////
.macro FUNC_SAVE
	//// Required for Update/GMC_ENC
	//the number of pushes must equal STACK_OFFSET
	push    %r12
	push    %r13
	push    %r14
	push    %r15
	push    %rsi
	mov	%rsp, %r14

	sub	$(VARIABLE_OFFSET), %rsp
	and	$~63, %rsp

#if __OUTPUT_FORMAT__ == win64
	// xmm6:xmm15 need to be maintained for Windows
	movdqu	%xmm6, (LOCAL_STORAGE + 0*16)(%rsp)
	movdqu	%xmm7, (LOCAL_STORAGE + 1*16)(%rsp)
	movdqu	%xmm8, (LOCAL_STORAGE + 2*16)(%rsp)
	movdqu	%xmm9, (LOCAL_STORAGE + 3*16)(%rsp)
	movdqu	%xmm10, (LOCAL_STORAGE + 4*16)(%rsp)
	movdqu	%xmm11, (LOCAL_STORAGE + 5*16)(%rsp)
	movdqu	%xmm12, (LOCAL_STORAGE + 6*16)(%rsp)
	movdqu	%xmm13, (LOCAL_STORAGE + 7*16)(%rsp)
	movdqu	%xmm14, (LOCAL_STORAGE + 8*16)(%rsp)
	movdqu	%xmm15, (LOCAL_STORAGE + 9*16)(%rsp)

	mov	arg(5), arg5  // XXXX [r14 + STACK_OFFSET + 8*5]
#endif
.endm // FUNC_SAVE

////////////////////////////////////////////////////////////////////////////////
// FUNC_RESTORE: Restore clobbered regs from the stack.
////////////////////////////////////////////////////////////////////////////////
.macro FUNC_RESTORE

#if __OUTPUT_FORMAT__ == win64
	movdqu	(LOCAL_STORAGE + 9*16)(%rsp), %xmm15
	movdqu	(LOCAL_STORAGE + 8*16)(%rsp), %xmm14
	movdqu	(LOCAL_STORAGE + 7*16)(%rsp), %xmm13
	movdqu	(LOCAL_STORAGE + 6*16)(%rsp), %xmm12
	movdqu	(LOCAL_STORAGE + 5*16)(%rsp), %xmm11
	movdqu	(LOCAL_STORAGE + 4*16)(%rsp), %xmm10
	movdqu	(LOCAL_STORAGE + 3*16)(%rsp), %xmm9
	movdqu	(LOCAL_STORAGE + 2*16)(%rsp), %xmm8
	movdqu	(LOCAL_STORAGE + 1*16)(%rsp), %xmm7
	movdqu	(LOCAL_STORAGE + 0*16)(%rsp), %xmm6
#endif

	// Required for Update/GMC_ENC
	mov	%r14, %rsp
	pop	%rsi
	pop	%r15
	pop	%r14
	pop	%r13
	pop	%r12
.endm // FUNC_RESTORE


////////////////////////////////////////////////////////////////////////////////
// GCM_INIT: Initializes a gcm_context_data struct to prepare for
// encoding/decoding.
// Input: gcm_key_data * (GDATA_KEY), gcm_context_data *(GDATA_CTX), IV,
// Additional Authentication data (A_IN), Additional Data length (A_LEN).
// Output: Updated GDATA_CTX with the hash of A_IN (AadHash) and initialized
// other parts of GDATA.
// Clobbers rax, r10-r13 and xmm0-xmm6
////////////////////////////////////////////////////////////////////////////////
.macro  GCM_INIT	GDATA_KEY, GDATA_CTX, IV, A_IN, A_LEN

#define AAD_HASH	%xmm0
#define SUBHASH		%xmm1

	movdqu  HashKey(\GDATA_KEY), SUBHASH

	CALC_AAD_HASH \A_IN, \A_LEN, AAD_HASH, SUBHASH, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %r10, %r11, %r12, %r13, %rax
	pxor	%xmm3, %xmm2
	mov	\A_LEN, %r10

	movdqu	AAD_HASH, AadHash(\GDATA_CTX)	// ctx_data.aad hash = aad_hash
	mov	%r10, AadLen(\GDATA_CTX)		// ctx_data.aad_length = aad_length
	xor	%r10, %r10
	mov	%r10, InLen(\GDATA_CTX)		// ctx_data.in_length = 0
	mov	 %r10, PBlockLen(\GDATA_CTX)	// ctx_data.partial_block_length = 0
	movdqu	%xmm2, PBlockEncKey(\GDATA_CTX)	// ctx_data.partial_block_enc_key = 0
	mov	\IV, %r10
	movdqa  ONEf(%rip), %xmm2			// read 12 IV bytes and pad with 0x00000001
	pinsrq  $0, (%r10), %xmm2
	pinsrd  $2, 8(%r10), %xmm2
	movdqu	%xmm2, OrigIV(\GDATA_CTX)	// ctx_data.orig_IV = iv

	pshufb	SHUF_MASK(%rip), %xmm2

	movdqu	%xmm2, CurCount(\GDATA_CTX)	// ctx_data.current_counter = iv
.endm // GCM_INIT


////////////////////////////////////////////////////////////////////////////////
// GCM_ENC_DEC Encodes/Decodes given data. Assumes that the passed
// gcm_context_data struct has been initialized by GCM_INIT.
// Requires the input data be at least 1 byte long because of
// READ_SMALL_INPUT_DATA.
// Input: gcm_key_data * (GDATA_KEY), gcm_context_data (GDATA_CTX),
// input text (PLAIN_CYPH_IN), input text length (PLAIN_CYPH_LEN) and whether
// encoding or decoding (ENC_DEC).
// Output: A cypher of the given plain text (CYPH_PLAIN_OUT), and updated
// GDATA_CTX
// Clobbers rax, r10-r15, and xmm0-xmm15
////////////////////////////////////////////////////////////////////////////////
.macro	GCM_ENC_DEC	GDATA_KEY, GDATA_CTX, CYPH_PLAIN_OUT, PLAIN_CYPH_IN, \
			PLAIN_CYPH_LEN, ENC_DEC

#define	DATA_OFFSET		%r11

	// clang compat: no local support
	// LOCAL _initial_num_blocks_is_7, _initial_num_blocks_is_6
	// LOCAL _initial_num_blocks_is_5, _initial_num_blocks_is_4
	// LOCAL _initial_num_blocks_is_3, _initial_num_blocks_is_2
	// LOCAL _initial_num_blocks_is_1, _initial_num_blocks_is_0
	// LOCAL _initial_blocks_encrypted, _encrypt_by_8_new, _encrypt_by_8
	// LOCAL _eight_cipher_left, _zero_cipher_left, _large_enough_update
	// LOCAL _data_read, _less_than_8_bytes_left, _multiple_of_16_bytes

// Macro flow:
// calculate the number of 16byte blocks in the message
// process (number of 16byte blocks) mod 8 '_initial_num_blocks_is_# .. _initial_blocks_encrypted'
// process 8 16 byte blocks at a time until all are done '_encrypt_by_8_new .. _eight_cipher_left'
// if there is a block of less tahn 16 bytes process it '_zero_cipher_left .. _multiple_of_16_bytes'

	cmp	$0, \PLAIN_CYPH_LEN
	je	_multiple_of_16_bytes_\@

	xor	DATA_OFFSET, DATA_OFFSET
	add	\PLAIN_CYPH_LEN, InLen(\GDATA_CTX)	//Update length of data processed
	movdqu	HashKey(\GDATA_KEY), %xmm13		// xmm13 = HashKey
	movdqu	AadHash(\GDATA_CTX), %xmm8


	PARTIAL_BLOCK \GDATA_KEY, \GDATA_CTX, \CYPH_PLAIN_OUT, \PLAIN_CYPH_IN, \PLAIN_CYPH_LEN, DATA_OFFSET, %xmm8, \ENC_DEC

	mov	\PLAIN_CYPH_LEN, %r13                                // save the number of bytes of plaintext/ciphertext
	sub	DATA_OFFSET, %r13
	mov	%r13, %r10		//save the amount of data left to process in r10
	and     $-16, %r13		// r13 = r13 - (r13 mod 16)

	mov	%r13, %r12
	shr	$4, %r12
	and	$7, %r12
	jz      _initial_num_blocks_is_0_\@


	cmp	$7, %r12
	je      _initial_num_blocks_is_7_\@
	cmp	$6, %r12
	je      _initial_num_blocks_is_6_\@
	cmp	$5, %r12
	je      _initial_num_blocks_is_5_\@
	cmp	$4, %r12
	je      _initial_num_blocks_is_4_\@
	cmp	$3, %r12
	je      _initial_num_blocks_is_3_\@
	cmp	$2, %r12
	je      _initial_num_blocks_is_2_\@

	jmp     _initial_num_blocks_is_1_\@

_initial_num_blocks_is_7_\@:
	INITIAL_BLOCKS	\GDATA_KEY, \GDATA_CTX, \CYPH_PLAIN_OUT, \PLAIN_CYPH_IN, %r13, DATA_OFFSET, 7, %xmm12, %xmm13, %xmm14, %xmm15, %xmm11, %xmm9, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7, %xmm8, %xmm10, %xmm0, \ENC_DEC
	sub	$(16*7), %r13
	jmp     _initial_blocks_encrypted_\@

_initial_num_blocks_is_6_\@:
	INITIAL_BLOCKS	\GDATA_KEY, \GDATA_CTX, \CYPH_PLAIN_OUT, \PLAIN_CYPH_IN, %r13, DATA_OFFSET, 6, %xmm12, %xmm13, %xmm14, %xmm15, %xmm11, %xmm9, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7, %xmm8, %xmm10, %xmm0, \ENC_DEC
	sub	$(16*6), %r13
	jmp     _initial_blocks_encrypted_\@

_initial_num_blocks_is_5_\@:
	INITIAL_BLOCKS	\GDATA_KEY, \GDATA_CTX, \CYPH_PLAIN_OUT, \PLAIN_CYPH_IN, %r13, DATA_OFFSET, 5, %xmm12, %xmm13, %xmm14, %xmm15, %xmm11, %xmm9, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7, %xmm8, %xmm10, %xmm0, \ENC_DEC
	sub	$(16*5), %r13
	jmp     _initial_blocks_encrypted_\@

_initial_num_blocks_is_4_\@:
	INITIAL_BLOCKS	\GDATA_KEY, \GDATA_CTX, \CYPH_PLAIN_OUT, \PLAIN_CYPH_IN, %r13, DATA_OFFSET, 4, %xmm12, %xmm13, %xmm14, %xmm15, %xmm11, %xmm9, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7, %xmm8, %xmm10, %xmm0, \ENC_DEC
	sub	$(16*4), %r13
	jmp     _initial_blocks_encrypted_\@

_initial_num_blocks_is_3_\@:
	INITIAL_BLOCKS	\GDATA_KEY, \GDATA_CTX, \CYPH_PLAIN_OUT, \PLAIN_CYPH_IN, %r13, DATA_OFFSET, 3, %xmm12, %xmm13, %xmm14, %xmm15, %xmm11, %xmm9, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7, %xmm8, %xmm10, %xmm0, \ENC_DEC
	sub	$(16*3), %r13
	jmp     _initial_blocks_encrypted_\@

_initial_num_blocks_is_2_\@:
	INITIAL_BLOCKS	\GDATA_KEY, \GDATA_CTX, \CYPH_PLAIN_OUT, \PLAIN_CYPH_IN, %r13, DATA_OFFSET, 2, %xmm12, %xmm13, %xmm14, %xmm15, %xmm11, %xmm9, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7, %xmm8, %xmm10, %xmm0, \ENC_DEC
	sub	$(16*2), %r13
	jmp     _initial_blocks_encrypted_\@

_initial_num_blocks_is_1_\@:
	INITIAL_BLOCKS	\GDATA_KEY, \GDATA_CTX, \CYPH_PLAIN_OUT, \PLAIN_CYPH_IN, %r13, DATA_OFFSET, 1, %xmm12, %xmm13, %xmm14, %xmm15, %xmm11, %xmm9, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7, %xmm8, %xmm10, %xmm0, \ENC_DEC
	sub	$(16*1), %r13
	jmp     _initial_blocks_encrypted_\@

_initial_num_blocks_is_0_\@:
	INITIAL_BLOCKS	\GDATA_KEY, \GDATA_CTX, \CYPH_PLAIN_OUT, \PLAIN_CYPH_IN, %r13, DATA_OFFSET, 0, %xmm12, %xmm13, %xmm14, %xmm15, %xmm11, %xmm9, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7, %xmm8, %xmm10, %xmm0, \ENC_DEC

_initial_blocks_encrypted_\@:
	cmp	$0, %r13
	je      _zero_cipher_left_\@

	sub	$128, %r13
	je      _eight_cipher_left_\@

	movd	%xmm9, %r15d
	and	$255, %r15d
	pshufb	SHUF_MASK(%rip), %xmm9


_encrypt_by_8_new_\@:
	cmp	$(255-8), %r15d
	jg      _encrypt_by_8_\@

	add	$8, %r15b
	GHASH_8_ENCRYPT_8_PARALLEL	\GDATA_KEY, \CYPH_PLAIN_OUT, \PLAIN_CYPH_IN, DATA_OFFSET, %xmm0, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14, %xmm9, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7, %xmm8, %xmm15, out_order, \ENC_DEC
	add	$128, DATA_OFFSET
	sub	$128, %r13
	jne     _encrypt_by_8_new_\@

	pshufb	SHUF_MASK(%rip), %xmm9
	jmp     _eight_cipher_left_\@

_encrypt_by_8_\@:
	pshufb	SHUF_MASK(%rip), %xmm9
	add	$8, %r15b

	GHASH_8_ENCRYPT_8_PARALLEL	\GDATA_KEY, \CYPH_PLAIN_OUT, \PLAIN_CYPH_IN, DATA_OFFSET, %xmm0, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14, %xmm9, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7, %xmm8, %xmm15, in_order, \ENC_DEC
	pshufb	SHUF_MASK(%rip), %xmm9
	add	$128, DATA_OFFSET
	sub	$128, %r13
	jne     _encrypt_by_8_new_\@

	pshufb	SHUF_MASK(%rip), %xmm9



_eight_cipher_left_\@:
	GHASH_LAST_8	\GDATA_KEY, %xmm0, %xmm10, %xmm11, %xmm12, %xmm13, %xmm14, %xmm15, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5, %xmm6, %xmm7, %xmm8


_zero_cipher_left_\@:
	movdqu	%xmm14, AadHash(\GDATA_CTX) 
	movdqu	%xmm9, CurCount(\GDATA_CTX) 

	mov	%r10, %r13
	and	$15, %r13			// r13 = (\PLAIN_CYPH_LEN mod 16)

	je      _multiple_of_16_bytes_\@

	mov	%r13, PBlockLen(\GDATA_CTX)		// my_ctx.data.partial_blck_length = r13
	// handle the last <16 Byte block seperately

	paddd	ONE(%rip), %xmm9			// INCR CNT to get Yn
	movdqu	%xmm9, CurCount(\GDATA_CTX)	// my_ctx.data.current_counter = xmm9
	pshufb  SHUF_MASK(%rip), %xmm9
	ENCRYPT_SINGLE_BLOCK	\GDATA_KEY, %xmm9, %xmm2	// E(K, Yn)
	movdqu	%xmm9, PBlockEncKey(\GDATA_CTX)	// my_ctx_data.partial_block_enc_key = xmm9

	cmp	$16, \PLAIN_CYPH_LEN
	jge	_large_enough_update_\@

	lea	(\PLAIN_CYPH_IN, DATA_OFFSET), %r10
	READ_SMALL_DATA_INPUT	%xmm1, %r10, %r13, %r12, %r15, %rax
	lea	(SHIFT_MASK + 16)(%rip), %r12
	sub	%r13, %r12
	jmp	_data_read_\@

_large_enough_update_\@:
	sub	$16, DATA_OFFSET
	add	%r13, DATA_OFFSET

	movdqu	(\PLAIN_CYPH_IN, DATA_OFFSET), %xmm1	// receive the last <16 Byte block

	sub	%r13, DATA_OFFSET
	add	$16, DATA_OFFSET

	lea	(SHIFT_MASK + 16)(%rip), %r12
	sub	%r13, %r12			// adjust the shuffle mask pointer to be able to shift 16-r13 bytes (r13 is the number of bytes in plaintext mod 16)
	movdqu	(%r12), %xmm2			// get the appropriate shuffle mask
	pshufb	%xmm2, %xmm1			// shift right 16-r13 bytes
_data_read_\@:
	.ifc  \ENC_DEC, DEC

	movdqa	%xmm1, %xmm2
	pxor	%xmm1, %xmm9			// Plaintext XOR E(K, Yn)
	movdqu	(ALL_F - SHIFT_MASK)(%r12), %xmm1	// get the appropriate mask to mask out top 16-r13 bytes of xmm9
	pand	%xmm1, %xmm9			// mask out top 16-r13 bytes of xmm9
	pand	%xmm1, %xmm2
	pshufb	SHUF_MASK(%rip), %xmm2
	pxor	%xmm2, %xmm14
	movdqu	%xmm14, AadHash(\GDATA_CTX)

	.else	// .ifc  \ENC_DEC, DEC

	pxor	%xmm1, %xmm9			// Plaintext XOR E(K, Yn)
	movdqu	(ALL_F - SHIFT_MASK)(%r12), %xmm1	// get the appropriate mask to mask out top 16-r13 bytes of xmm9
	pand	%xmm1, %xmm9			// mask out top 16-r13 bytes of xmm9
	pshufb	SHUF_MASK(%rip), %xmm9
	pxor	%xmm9, %xmm14
	movdqu	%xmm14, AadHash(\GDATA_CTX)

	pshufb	SHUF_MASK(%rip), %xmm9		// shuffle xmm9 back to output as ciphertext

	.endif	// .ifc  \ENC_DEC, DEC


	//////////////////////////////////////////////////////////
	// output r13 Bytes
	movq	%xmm9, %rax
	cmp	$8, %r13
	jle     _less_than_8_bytes_left_\@

	mov	%rax, (\CYPH_PLAIN_OUT, DATA_OFFSET)
	add	$8, DATA_OFFSET
	psrldq	$8, %xmm9
	movq	%xmm9, %rax
	sub	$8, %r13

_less_than_8_bytes_left_\@:
	movb	%al, (\CYPH_PLAIN_OUT, DATA_OFFSET)
	add     $1, DATA_OFFSET
	shr	$8, %rax
	sub	$1, %r13
	jne     _less_than_8_bytes_left_\@
	//////////////////////////////////////////////////////////

_multiple_of_16_bytes_\@:

.endm // GCM_ENC_DEC


////////////////////////////////////////////////////////////////////////////////
// GCM_COMPLETE: Finishes Encyrption/Decryption of last partial block after
// GCM_UPDATE finishes.
// Input: A gcm_key_data * (GDATA_KEY), gcm_context_data * (GDATA_CTX) and
// whether encoding or decoding (ENC_DEC).
// Output: Authorization Tag (AUTH_TAG) and Authorization Tag length
// (AUTH_TAG_LEN)
// Clobbers %rax, r10-r12, and xmm0, xmm1, xmm5, xmm6, xmm9, xmm11, xmm14, xmm15
////////////////////////////////////////////////////////////////////////////////
.macro	GCM_COMPLETE	GDATA_KEY, GDATA_CTX, AUTH_TAG, AUTH_TAG_LEN, ENC_DEC

#define	PLAIN_CYPH_LEN	%rax

	// clang compat: no local support
	// LOCAL _partial_done, _return_T, _T_8, _T_12, _T_16, _return_T_done

	mov	PBlockLen(\GDATA_CTX), %r12	// r12 = aadLen (number of bytes)
	movdqu	AadHash(\GDATA_CTX), %xmm14
	movdqu	HashKey(\GDATA_KEY), %xmm13

	cmp	$0, %r12

	je _partial_done_\@

	GHASH_MUL %xmm14, %xmm13, %xmm0, %xmm10, %xmm11, %xmm5, %xmm6 //GHASH computation for the last <16 Byte block
	movdqu	%xmm14, AadHash(\GDATA_CTX)

_partial_done_\@:

	mov	AadLen(\GDATA_CTX), %r12		// r12 = aadLen (number of bytes)
	mov	InLen(\GDATA_CTX), PLAIN_CYPH_LEN

	shl	$3, %r12			// convert into number of bits
	movd	%r12d, %xmm15		// len(A) in xmm15

	shl	$3, PLAIN_CYPH_LEN	// len(C) in bits  (*128)
	movq	PLAIN_CYPH_LEN, %xmm1
	pslldq	$8, %xmm15		// xmm15 = len(A)|| 0x0000000000000000
	pxor	%xmm1, %xmm15		// xmm15 = len(A)||len(C)

	pxor	%xmm15, %xmm14
	GHASH_MUL %xmm14, %xmm13, %xmm0, %xmm10, %xmm11, %xmm5, %xmm6    // final GHASH computation
	pshufb	SHUF_MASK(%rip), %xmm14		// perform a 16Byte swap
	movdqu	OrigIV(\GDATA_CTX), %xmm9	// xmm9 = Y0

	ENCRYPT_SINGLE_BLOCK	\GDATA_KEY, %xmm9, %xmm2	// E(K, Y0)

	pxor	%xmm14, %xmm9

_return_T_\@:
	mov	\AUTH_TAG, %r10			// r10 = authTag
	mov	\AUTH_TAG_LEN, %r11		// r11 = auth_tag_len

	cmp	$16, %r11
	je      _T_16_\@

	cmp	$12, %r11
	je      _T_12_\@

_T_8_\@:
	movq	%xmm9, %rax
	mov	%rax, (%r10)
	jmp     _return_T_done_\@

_T_12_\@:
	movq	%xmm9, %rax
	mov	%rax, (%r10)
	psrldq	$8, %xmm9
	movd	%xmm9, %eax
	mov	%eax, 8(%r10)
	jmp     _return_T_done_\@

_T_16_\@:
	movdqu	%xmm9, (%r10)

_return_T_done_\@:
.endm //GCM_COMPLETE


#if 1

	.balign 16
////////////////////////////////////////////////////////////////////////////////
//void	aes_gcm_precomp_{128,256}_sse
//        (struct gcm_key_data *key_data);
////////////////////////////////////////////////////////////////////////////////
#if FUNCT_EXTENSION != _nt
.global FN_NAME(precomp,_)
FN_NAME(precomp,_):

	endbranch

	push	%r12
	push	%r13
	push	%r14
	push	%r15

	mov     %rsp, %r14

	sub	$(VARIABLE_OFFSET), %rsp
	and	$(~63), %rsp				// align rsp to 64 bytes

#if __OUTPUT_FORMAT__ == win64
	// only xmm6 needs to be maintained
	movdqu	%xmm6, (LOCAL_STORAGE + 0*16)(%rsp)
#endif

	pxor	%xmm6, %xmm6
	ENCRYPT_SINGLE_BLOCK	arg1, %xmm6, %xmm2	// xmm6 = HashKey

	pshufb	SHUF_MASK(%rip), %xmm6
	///////////////  PRECOMPUTATION of HashKey<<1 mod poly from the HashKey
	movdqa	%xmm6, %xmm2
	psllq	$1, %xmm6
	psrlq	$63, %xmm2
	movdqa	%xmm2, %xmm1
	pslldq	$8, %xmm2
	psrldq	$8, %xmm1
	por	%xmm2, %xmm6

	//reduction
	pshufd	$0b00100100, %xmm1, %xmm2
	pcmpeqd	TWOONE(%rip), %xmm2
	pand	POLY(%rip), %xmm2
	pxor	%xmm2, %xmm6		// xmm6 holds the HashKey<<1 mod poly
	///////////////////////////////////////////////////////////////////////
	movdqu	 %xmm6, HashKey(arg1)	// store HashKey<<1 mod poly

	PRECOMPUTE  arg1, %xmm6, %xmm0, %xmm1, %xmm2, %xmm3, %xmm4, %xmm5

#if __OUTPUT_FORMAT__ == win64
	movdqu (LOCAL_STORAGE + 0*16)(%rsp), %xmm6
#endif
	mov	%r14, %rsp

	pop	%r15
	pop	%r14
	pop	%r13
	pop	%r12
	ret
#endif	// _nt


////////////////////////////////////////////////////////////////////////////////
//void   aes_gcm_init_128_sse / aes_gcm_init_256_sse (
//        const struct gcm_key_data *key_data,
//        struct gcm_context_data *context_data,
//        u8      *iv,
//        const   u8 *aad,
//        u64     aad_len);
////////////////////////////////////////////////////////////////////////////////
#if FUNCT_EXTENSION != _nt
.global FN_NAME(init,_)
FN_NAME(init,_):
	endbranch

	push	%r12
	push	%r13
#if __OUTPUT_FORMAT__ == win64
	push	arg5
	sub	$(1*16), %rsp
	movdqu	%xmm6, (0*16)(%rsp)
	mov	(1*16 + 8*3 + 8*5)(%rsp), arg5
#endif

	GCM_INIT arg1, arg2, arg3, arg4, arg5

#if __OUTPUT_FORMAT__ == win64
	movdqu	(0*16)(%rsp), %xmm6
	add	$(1*16), %rsp
	pop	arg5
#endif
	pop	%r13
	pop	%r12
	ret
#endif	// _nt


////////////////////////////////////////////////////////////////////////////////
//void   aes_gcm_enc_128_update_sse / aes_gcm_enc_256_update_sse
//        const struct gcm_key_data *key_data,
//        struct gcm_context_data *context_data,
//        u8      *out,
//        const   u8 *in,
//        u64     plaintext_len);
////////////////////////////////////////////////////////////////////////////////
.global FN_NAME(enc,_update_)
FN_NAME(enc,_update_):
	endbranch

	FUNC_SAVE

	GCM_ENC_DEC arg1, arg2, arg3, arg4, arg5, ENC

	FUNC_RESTORE

	ret


////////////////////////////////////////////////////////////////////////////////
//void   aes_gcm_dec_256_update_sse / aes_gcm_dec_256_update_sse
//        const struct gcm_key_data *key_data,
//        struct gcm_context_data *context_data,
//        u8      *out,
//        const   u8 *in,
//        u64     plaintext_len);
////////////////////////////////////////////////////////////////////////////////
.global FN_NAME(dec,_update_)
FN_NAME(dec,_update_):
	endbranch

	FUNC_SAVE

	GCM_ENC_DEC arg1, arg2, arg3, arg4, arg5, DEC

	FUNC_RESTORE

	ret


////////////////////////////////////////////////////////////////////////////////
//void   aes_gcm_enc_128_finalize_sse / aes_gcm_enc_256_finalize_sse
//        const struct gcm_key_data *key_data,
//        struct gcm_context_data *context_data,
//        u8      *auth_tag,
//        u64     auth_tag_len);
////////////////////////////////////////////////////////////////////////////////
#if FUNCT_EXTENSION != _nt
.global FN_NAME(enc,_finalize_)
FN_NAME(enc,_finalize_):

	endbranch

	push	%r12

#if __OUTPUT_FORMAT__ == win64
	// xmm6:xmm15 need to be maintained for Windows
	sub	$(5*16), %rsp
	movdqu	%xmm6, (0*16)(%rsp)
	movdqu	%xmm9, (1*16)(%rsp)
	movdqu	%xmm11, (2*16)(%rsp)
	movdqu	%xmm14, (3*16)(%rsp)
	movdqu	%xmm15, (4*16)(%rsp)
#endif
	GCM_COMPLETE	arg1, arg2, arg3, arg4, ENC

#if __OUTPUT_FORMAT__ == win64
	movdqu	(4*16)(%rsp), %xmm15
	movdqu	(3*16)(%rsp), %xmm14
	movdqu	(2*16)(%rsp), %xmm11
	movdqu	(1*16)(%rsp), %xmm9
	movdqu	(0*16)(%rsp), %xmm6
	add	$(5*16), %rsp
#endif

	pop	%r12
	ret
#endif	// _nt


////////////////////////////////////////////////////////////////////////////////
//void   aes_gcm_dec_128_finalize_sse / aes_gcm_dec_256_finalize_sse
//        const struct gcm_key_data *key_data,
//        struct gcm_context_data *context_data,
//        u8      *auth_tag,
//        u64     auth_tag_len);
////////////////////////////////////////////////////////////////////////////////
#if FUNCT_EXTENSION != _nt
.global FN_NAME(dec,_finalize_)
FN_NAME(dec,_finalize_):

	endbranch

	push	%r12

#if __OUTPUT_FORMAT == win64
	// xmm6:xmm15 need to be maintained for Windows
	sub	$(5*16), %rsp
	movdqu	%xmm6, (0*16)(%rsp)
	movdqu	%xmm9, (1*16)(%rsp)
	movdqu	%xmm11, (2*16)(%rsp)
	movdqu	%xmm14, (3*16)(%rsp)
	movdqu	%xmm15, (4*16)(%rsp)
#endif
	GCM_COMPLETE	arg1, arg2, arg3, arg4, DEC

#if __OUTPUT_FORMAT__ == win64
	movdqu	(4*16)(%rsp), %xmm15
	movdqu	(3*16)(%rsp), %xmm14
	movdqu	(2*16)(%rsp), %xmm11
	movdqu	(1*16)(%rsp), %xmm9
	movdqu	(0*16)(%rsp), %xmm6
	add	$(5*16), %rsp
#endif

	pop	%r12
	ret
#endif	// _nt


////////////////////////////////////////////////////////////////////////////////
//void   aes_gcm_enc_128_sse / aes_gcm_enc_256_sse
//        const struct gcm_key_data *key_data,
//        struct gcm_context_data *context_data,
//        u8      *out,
//        const   u8 *in,
//        u64     plaintext_len,
//        u8      *iv,
//        const   u8 *aad,
//        u64     aad_len,
//        u8      *auth_tag,
//        u64     auth_tag_len)//
////////////////////////////////////////////////////////////////////////////////
.global FN_NAME(enc,_)
FN_NAME(enc,_):
	endbranch

	FUNC_SAVE

	GCM_INIT arg1, arg2, arg6, arg7, arg8

	GCM_ENC_DEC  arg1, arg2, arg3, arg4, arg5, ENC

	GCM_COMPLETE arg1, arg2, arg9, arg10, ENC
	FUNC_RESTORE

	ret

////////////////////////////////////////////////////////////////////////////////
//void   aes_gcm_dec_128_sse / aes_gcm_dec_256_sse
//        const struct gcm_key_data *key_data,
//        struct gcm_context_data *context_data,
//        u8      *out,
//        const   u8 *in,
//        u64     plaintext_len,
//        u8      *iv,
//        const   u8 *aad,
//        u64     aad_len,
//        u8      *auth_tag,
//        u64     auth_tag_len)//
////////////////////////////////////////////////////////////////////////////////
.global FN_NAME(dec,_)
FN_NAME(dec,_):
	endbranch

	FUNC_SAVE

	GCM_INIT arg1, arg2, arg6, arg7, arg8

	GCM_ENC_DEC  arg1, arg2, arg3, arg4, arg5, DEC

	GCM_COMPLETE arg1, arg2, arg9, arg10, DEC
	FUNC_RESTORE

	ret

.global FN_NAME(this_is_gas,_)
FN_NAME(this_is_gas,_):
	endbranch
	FUNC_SAVE
	FUNC_RESTORE
	ret

#else
	// GAS doesnt't provide the linenuber in the macro
	////////////////////////
	// GHASH_MUL xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6
	// PRECOMPUTE rax, xmm0, xmm1, xmm2, xmm3, xmm4, xmm5, xmm6
	// READ_SMALL_DATA_INPUT xmm1, r10, 8, rax, r12, r15
	// ENCRYPT_SINGLE_BLOCK rax, xmm0, xmm1
	// INITIAL_BLOCKS rdi,rsi,rdx,rcx,r13,r11,7,xmm12,xmm13,xmm14,xmm15,xmm11,xmm9,xmm1,xmm2,xmm3,xmm4,xmm5,xmm6,xmm7,xmm8,xmm10,xmm0,ENC
	// CALC_AAD_HASH [r14+8*5+8*1],[r14+8*5+8*2],xmm0,xmm1,xmm2,xmm3,xmm4,xmm5,xmm6,r10,r11,r12,r13,rax
	// READ_SMALL_DATA_INPUT	xmm2, r10, r11, r12, r13, rax
	// PARTIAL_BLOCK  rdi,rsi,rdx,rcx,r8,r11,xmm8,ENC
	// GHASH_8_ENCRYPT_8_PARALLEL rdi,rdx,rcx,r11,xmm0,xmm10,xmm11,xmm12,xmm13,xmm14,xmm9,xmm1,xmm2,xmm3,xmm4,xmm5,xmm6,xmm7,xmm8,xmm15,out_order,ENC
	//GHASH_LAST_8 rdi,xmm0,xmm10,xmm11,xmm12,xmm13,xmm14,xmm15,xmm1,xmm2,xmm3,xmm4,xmm5,xmm6,xmm7,xmm8
#endif
