////////////////////////////////////////////////////////////////////////////////
//  Copyright(c) 2011-2016 Intel Corporation All rights reserved.
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

#ifndef GCM_DEFINES_ASM_INCLUDED
#define GCM_DEFINES_ASM_INCLUDED

//
// Authors:
//       Erdinc Ozturk
//       Vinodh Gopal
//       James Guilford

// Port to GNU as, translation to GNU as att-syntax and adoptions for the ICP
// Copyright(c) 2023 Attila Fülöp <attila@fueloep.org>

SECTION_STATIC

.balign 16
POLY:		.quad     0x0000000000000001, 0xC200000000000000

// unused for sse
.balign 64
POLY2:		.quad     0x00000001C2000000, 0xC200000000000000
		.quad     0x00000001C2000000, 0xC200000000000000
		.quad     0x00000001C2000000, 0xC200000000000000
		.quad     0x00000001C2000000, 0xC200000000000000
.balign 16
TWOONE:		.quad     0x0000000000000001, 0x0000000100000000

// order of these constants should not change.
// more specifically, ALL_F should follow SHIFT_MASK, and ZERO should
// follow ALL_F

.balign 64
SHUF_MASK:	.quad     0x08090A0B0C0D0E0F, 0x0001020304050607
		.quad     0x08090A0B0C0D0E0F, 0x0001020304050607
		.quad     0x08090A0B0C0D0E0F, 0x0001020304050607
		.quad     0x08090A0B0C0D0E0F, 0x0001020304050607

SHIFT_MASK:	.quad     0x0706050403020100, 0x0f0e0d0c0b0a0908
ALL_F:		.quad     0xffffffffffffffff, 0xffffffffffffffff
ZERO:		.quad     0x0000000000000000, 0x0000000000000000 // unused for sse
ONE:		.quad     0x0000000000000001, 0x0000000000000000
TWO:		.quad     0x0000000000000002, 0x0000000000000000 // unused for sse
ONEf:		.quad     0x0000000000000000, 0x0100000000000000
TWOf:		.quad     0x0000000000000000, 0x0200000000000000 // unused for sse

// Below unused for sse
.balign 64
ddq_add_1234:
	.quad	0x0000000000000001, 0x0000000000000000
	.quad	0x0000000000000002, 0x0000000000000000
	.quad	0x0000000000000003, 0x0000000000000000
	.quad	0x0000000000000004, 0x0000000000000000

.balign 64
ddq_add_5678:
	.quad	0x0000000000000005, 0x0000000000000000
	.quad	0x0000000000000006, 0x0000000000000000
	.quad	0x0000000000000007, 0x0000000000000000
	.quad	0x0000000000000008, 0x0000000000000000

.balign 64
ddq_add_4444:
	.quad	0x0000000000000004, 0x0000000000000000
	.quad	0x0000000000000004, 0x0000000000000000
	.quad	0x0000000000000004, 0x0000000000000000
	.quad	0x0000000000000004, 0x0000000000000000

.balign 64
ddq_add_8888:
	.quad	0x0000000000000008, 0x0000000000000000
	.quad	0x0000000000000008, 0x0000000000000000
	.quad	0x0000000000000008, 0x0000000000000000
	.quad	0x0000000000000008, 0x0000000000000000

.balign 64
ddq_addbe_1234:
	.quad	0x0000000000000000, 0x0100000000000000
	.quad	0x0000000000000000, 0x0200000000000000
	.quad	0x0000000000000000, 0x0300000000000000
	.quad	0x0000000000000000, 0x0400000000000000

.balign 64
ddq_addbe_5678:
	.quad	0x0000000000000000, 0x0500000000000000
	.quad	0x0000000000000000, 0x0600000000000000
	.quad	0x0000000000000000, 0x0700000000000000
	.quad	0x0000000000000000, 0x0800000000000000

.balign 64
ddq_addbe_4444:
	.quad	0x0000000000000000, 0x0400000000000000
	.quad	0x0000000000000000, 0x0400000000000000
	.quad	0x0000000000000000, 0x0400000000000000
	.quad	0x0000000000000000, 0x0400000000000000

.balign 64
ddq_addbe_8888:
	.quad	0x0000000000000000, 0x0800000000000000
	.quad	0x0000000000000000, 0x0800000000000000
	.quad	0x0000000000000000, 0x0800000000000000
	.quad	0x0000000000000000, 0x0800000000000000

.balign 64
byte_len_to_mask_table:
	.short      0x0000, 0x0001, 0x0003, 0x0007
	.short      0x000f, 0x001f, 0x003f, 0x007f
	.short      0x00ff, 0x01ff, 0x03ff, 0x07ff
	.short      0x0fff, 0x1fff, 0x3fff, 0x7fff
	.short      0xffff

.balign 64
byte64_len_to_mask_table:
	.quad      0x0000000000000000, 0x0000000000000001
	.quad      0x0000000000000003, 0x0000000000000007
	.quad      0x000000000000000f, 0x000000000000001f
	.quad      0x000000000000003f, 0x000000000000007f
	.quad      0x00000000000000ff, 0x00000000000001ff
	.quad      0x00000000000003ff, 0x00000000000007ff
	.quad      0x0000000000000fff, 0x0000000000001fff
	.quad      0x0000000000003fff, 0x0000000000007fff
	.quad      0x000000000000ffff, 0x000000000001ffff
	.quad      0x000000000003ffff, 0x000000000007ffff
	.quad      0x00000000000fffff, 0x00000000001fffff
	.quad      0x00000000003fffff, 0x00000000007fffff
	.quad      0x0000000000ffffff, 0x0000000001ffffff
	.quad      0x0000000003ffffff, 0x0000000007ffffff
	.quad      0x000000000fffffff, 0x000000001fffffff
	.quad      0x000000003fffffff, 0x000000007fffffff
	.quad      0x00000000ffffffff, 0x00000001ffffffff
	.quad      0x00000003ffffffff, 0x00000007ffffffff
	.quad      0x0000000fffffffff, 0x0000001fffffffff
	.quad      0x0000003fffffffff, 0x0000007fffffffff
	.quad      0x000000ffffffffff, 0x000001ffffffffff
	.quad      0x000003ffffffffff, 0x000007ffffffffff
	.quad      0x00000fffffffffff, 0x00001fffffffffff
	.quad      0x00003fffffffffff, 0x00007fffffffffff
	.quad      0x0000ffffffffffff, 0x0001ffffffffffff
	.quad      0x0003ffffffffffff, 0x0007ffffffffffff
	.quad      0x000fffffffffffff, 0x001fffffffffffff
	.quad      0x003fffffffffffff, 0x007fffffffffffff
	.quad      0x00ffffffffffffff, 0x01ffffffffffffff
	.quad      0x03ffffffffffffff, 0x07ffffffffffffff
	.quad      0x0fffffffffffffff, 0x1fffffffffffffff
	.quad      0x3fffffffffffffff, 0x7fffffffffffffff
	.quad      0xffffffffffffffff

.balign 64
mask_out_top_block:
	.quad      0xffffffffffffffff, 0xffffffffffffffff
	.quad      0xffffffffffffffff, 0xffffffffffffffff
	.quad      0xffffffffffffffff, 0xffffffffffffffff
	.quad      0x0000000000000000, 0x0000000000000000

.section .text

// #define	KEYSCHED_LEN (15 * GCM_BLOCKSIZE)
// #define	AES_KEY_LEN (2 * KEYSCHED_LEN + 16 + 8 + 4 + 4)	// 512

// Offsets into struct gcm_ctx:
//
// typedef struct gcm_ctx {
//	void *gcm_keysched;		OFFSET:   0	=   0
//	size_t gcm_keysched_len;	OFFSET:	  1*8	=   8
//	uint64_t gcm_cb[2];		OFFSET:   2*8	=  16
//	uint64_t gcm_remainder[2];	OFFSET:   4*8	=  32
//	size_t gcm_remainder_len;	OFFSET:   6*8	=  48
//	uint8_t *gcm_lastp;		OFFSET:   7*8	=  56
//	uint8_t *gcm_copy_to;		OFFSET:   8*8	=  64
//	uint32_t gcm_flags;		OFFSET:   9*8	=  72
//	size_t gcm_tag_len;		OFFSET:  10*8	=  80
//	size_t gcm_processed_data_len;	OFFSET:  11*8	=  88
//	size_t gcm_pt_buf_len;		OFFSET:  12*8	=  96
//	uint32_t gcm_tmp[4];		OFFSET:  13*8	= 104
//	uint64_t gcm_ghash[2];		OFFSET:  15*8	= 120
//	uint64_t gcm_H[2];		OFFSET:  17*8	= 136
//	uint64_t *gcm_Htable;		OFFSET:  19*8	= 152
//	size_t gcm_htab_len;		OFFSET:  20*8	= 160
//	uint64_t gcm_J0[2];		OFFSET:  21*8	= 168
//	uint64_t gcm_len_a_len_c[2];	OFFSET:  23*8	= 184
//	uint8_t *gcm_pt_buf;		OFFSET:  25*8	= 200
//	gcm_simd_impl_t gcm_simd_impl;	OFFSET:  26*8	= 208
// } gcm_ctx_t;				SIZE:		= 216

// AadHash:
//	Store current Hash of data which has been input: gcm_ctx->ghash.
//
// AadLen:
//	Store length of input data which will not be encrypted or decrypted:
//	gcm_ctx->gcm_tag_len.
//
// InLen:
//	Store length of input data which will be encrypted or decrypted:
//	gcm_ctx->gcm_processed_data_len.
//
// PBlockEncKey:
//	Encryption key for the partial block at the end of the previous update:
//	no real match, use: gcm_ctx->gcm_remainder.
//
// OrigIV:
//	The initial counter: 12 bytes IV with (int32_t) 1 appended:
//	gcm_ctx->gcm_J0.
//
// CurCount:
//	Current counter for generation of encryption key: gcm_ctx->gcm_cb.
//
// PBlockLen:
//	Length of partial block at the end of the previous update:
//	gcm_ctx->gcm_remainder_len.

#define KeySched	    0		// gcm_ctx->gcm_keysched
#define AadHash		(15*8)		// gcm_ctx->gcm_ghash
#define AadLen		(23*8)		// gcm_ctx->gcm_len_a_len_c[0]
#define	TagLen		(10*8)		// gcm_ctx->gcm_tag_len
#define InLen		(11*8)		// gcm_ctx->gcm_processed_data_len
#define PBlockEncKey	 (4*8)		// gcm_ctx->gcm_remainder
#define OrigIV		(21*8)		// gcm_ctx->gcm_J0
#define CurCount	 (2*8)		// gcm_ctx->gcm_cb
#define PBlockLen	 (6*8)		// gcm_ctx->gcm_remainder_len
#define GcmH		(17*8)		// gcm_ctx->gcm_H
#define GcmHtab		(19*8)		// gcm_ctx->gcm_Htable
#define LenALenC	(23*8)		// gcm_ctx->gcm_len_a_len_c

// Define the offsets into gcm_ctx of the fields fields of gcm_htab.
//  u8 shifted_hkey_1[16]	store HashKey <<1 mod poly here
//  u8 shifted_hkey_2[16]	store HashKey^2 <<1 mod poly here
//  u8 shifted_hkey_3[16]	store HashKey^3 <<1 mod poly here
//  u8 shifted_hkey_4[16]	store HashKey^4 <<1 mod poly here
//  u8 shifted_hkey_5[16]	store HashKey^5 <<1 mod poly here
//  u8 shifted_hkey_6[16]	store HashKey^6 <<1 mod poly here
//  u8 shifted_hkey_7[16]	store HashKey^7 <<1 mod poly here
//  u8 shifted_hkey_8[16]	store HashKey^8 <<1 mod poly here
//  u8 shifted_hkey_1_k[16]	store XOR of High 64 bits and Low 64 bits of  HashKey <<1 mod poly here (for Karatsuba purposes)
//  u8 shifted_hkey_2_k[16]	store XOR of High 64 bits and Low 64 bits of  HashKey^2 <<1 mod poly here (for Karatsuba purposes)
//  u8 shifted_hkey_3_k[16]	store XOR of High 64 bits and Low 64 bits of  HashKey^3 <<1 mod poly here (for Karatsuba purposes)
//  u8 shifted_hkey_4_k[16]	store XOR of High 64 bits and Low 64 bits of  HashKey^4 <<1 mod poly here (for Karatsuba purposes)
//  u8 shifted_hkey_5_k[16]	store XOR of High 64 bits and Low 64 bits of  HashKey^5 <<1 mod poly here (for Karatsuba purposes)
//  u8 shifted_hkey_6_k[16]	store XOR of High 64 bits and Low 64 bits of  HashKey^6 <<1 mod poly here (for Karatsuba purposes)
//  u8 shifted_hkey_7_k[16]	store XOR of High 64 bits and Low 64 bits of  HashKey^7 <<1 mod poly here (for Karatsuba purposes)
//  u8 shifted_hkey_8_k[16]	store XOR of High 64 bits and Low 64 bits of  HashKey^8 <<1 mod poly here (for Karatsuba purposes)

#define	GCM_BLOCKSIZE	16

#ifndef GCM_KEYS_VAES_AVX512_INCLUDED
#define	HashKey		(GCM_BLOCKSIZE *  0)
#define	HashKey_1	(GCM_BLOCKSIZE *  0)
#define	HashKey_2	(GCM_BLOCKSIZE *  1)
#define	HashKey_3	(GCM_BLOCKSIZE *  2)
#define	HashKey_4	(GCM_BLOCKSIZE *  3)
#define	HashKey_5	(GCM_BLOCKSIZE *  4)
#define	HashKey_6	(GCM_BLOCKSIZE *  5)
#define	HashKey_7	(GCM_BLOCKSIZE *  6)
#define	HashKey_8	(GCM_BLOCKSIZE *  7)
#define	HashKey_k	(GCM_BLOCKSIZE *  8)
#define	HashKey_2_k	(GCM_BLOCKSIZE *  9)
#define	HashKey_3_k	(GCM_BLOCKSIZE * 10)
#define	HashKey_4_k	(GCM_BLOCKSIZE * 11)
#define	HashKey_5_k	(GCM_BLOCKSIZE * 12)
#define	HashKey_6_k	(GCM_BLOCKSIZE * 13)
#define	HashKey_7_k	(GCM_BLOCKSIZE * 14)
#define	HashKey_8_k	(GCM_BLOCKSIZE * 15)
#endif

.macro xmmreg name, num
	.set xmm\name, %xmm\num
.endm

// Push a 64 bit register to the stack and generate the needed CFI directives.
.macro CFI_PUSHQ	REG, OFFS
	pushq	\REG
	.cfi_adjust_cfa_offset	8
	.cfi_offset	\REG, \OFFS
.endm

// Pop a 64 bit register from the stack and generate the needed CFI directives.
.macro CFI_POPQ		REG
	popq	\REG
	.cfi_restore	\REG
	.cfi_adjust_cfa_offset	-8
.endm

#define arg(x) (STACK_OFFSET + 8*(x))(%r14)

/*
.macro STACK_FRAME_NON_STANDARD func:req
	.pushsection .discard.func_stack_frame_non_standard, "aw"
-		.long \func - .
+#ifdef CONFIG_64BIT
+		.quad \func
+#else
+		.long \func
+#endif
 	.popsection
.endm
*/

#define arg1 %rdi
#define arg2 %rsi
#define arg3 %rdx
#define arg4 %rcx
#define arg5 %r8
#define arg6 %r9
#define arg7 ((STACK_OFFSET) + 8*1)(%r14)
#define arg8 ((STACK_OFFSET) + 8*2)(%r14)
#define arg9 ((STACK_OFFSET) + 8*3)(%r14)
#define arg10 ((STACK_OFFSET) + 8*4)(%r14)

#ifdef NT_LDST
#define NT_LD
#define NT_ST
#endif

////// Use Non-temporal load/stor
#ifdef NT_LD
#define	XLDR	 movntdqa
#define	VXLDR	 vmovntdqa
#define	VX512LDR vmovntdqa
#else
#define	XLDR	 movdqu
#define	VXLDR	 vmovdqu
#define	VX512LDR vmovdqu8
#endif

////// Use Non-temporal load/stor
#ifdef NT_ST
#define	XSTR	 movntdq
#define	VXSTR	 vmovntdq
#define	VX512STR vmovntdq
#else
#define	XSTR	 movdqu
#define	VXSTR	 vmovdqu
#define	VX512STR vmovdqu8
#endif

#endif // GCM_DEFINES_ASM_INCLUDED
