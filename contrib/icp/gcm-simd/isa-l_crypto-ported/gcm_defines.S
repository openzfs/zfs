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

// Port to GNU as and translation to GNU as att-syntax
// Copyright(c) 2023 Attila Fülöp <attila@fueloep.org>

////////////

.section .rodata

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


////define the fields of gcm_data struct
//typedef struct gcm_data
//{
//        u8 expanded_keys[16*15]//
//        u8 shifted_hkey_1[16]//  // store HashKey <<1 mod poly here
//        u8 shifted_hkey_2[16]//  // store HashKey^2 <<1 mod poly here
//        u8 shifted_hkey_3[16]//  // store HashKey^3 <<1 mod poly here
//        u8 shifted_hkey_4[16]//  // store HashKey^4 <<1 mod poly here
//        u8 shifted_hkey_5[16]//  // store HashKey^5 <<1 mod poly here
//        u8 shifted_hkey_6[16]//  // store HashKey^6 <<1 mod poly here
//        u8 shifted_hkey_7[16]//  // store HashKey^7 <<1 mod poly here
//        u8 shifted_hkey_8[16]//  // store HashKey^8 <<1 mod poly here
//        u8 shifted_hkey_1_k[16]//  // store XOR of High 64 bits and Low 64 bits of  HashKey <<1 mod poly here (for Karatsuba purposes)
//        u8 shifted_hkey_2_k[16]//  // store XOR of High 64 bits and Low 64 bits of  HashKey^2 <<1 mod poly here (for Karatsuba purposes)
//        u8 shifted_hkey_3_k[16]//  // store XOR of High 64 bits and Low 64 bits of  HashKey^3 <<1 mod poly here (for Karatsuba purposes)
//        u8 shifted_hkey_4_k[16]//  // store XOR of High 64 bits and Low 64 bits of  HashKey^4 <<1 mod poly here (for Karatsuba purposes)
//        u8 shifted_hkey_5_k[16]//  // store XOR of High 64 bits and Low 64 bits of  HashKey^5 <<1 mod poly here (for Karatsuba purposes)
//        u8 shifted_hkey_6_k[16]//  // store XOR of High 64 bits and Low 64 bits of  HashKey^6 <<1 mod poly here (for Karatsuba purposes)
//        u8 shifted_hkey_7_k[16]//  // store XOR of High 64 bits and Low 64 bits of  HashKey^7 <<1 mod poly here (for Karatsuba purposes)
//        u8 shifted_hkey_8_k[16]//  // store XOR of High 64 bits and Low 64 bits of  HashKey^8 <<1 mod poly here (for Karatsuba purposes)
//} gcm_data//

#ifndef GCM_KEYS_VAES_AVX512_INCLUDED
#define HashKey         16*15    // store HashKey <<1 mod poly here
#define HashKey_1       16*15    // store HashKey <<1 mod poly here
#define HashKey_2       16*16    // store HashKey^2 <<1 mod poly here
#define HashKey_3       16*17    // store HashKey^3 <<1 mod poly here
#define HashKey_4       16*18    // store HashKey^4 <<1 mod poly here
#define HashKey_5       16*19    // store HashKey^5 <<1 mod poly here
#define HashKey_6       16*20    // store HashKey^6 <<1 mod poly here
#define HashKey_7       16*21    // store HashKey^7 <<1 mod poly here
#define HashKey_8       16*22    // store HashKey^8 <<1 mod poly here
#define HashKey_k       16*23    // store XOR of High 64 bits and Low 64 bits of  HashKey <<1 mod poly here (for Karatsuba purposes)
#define HashKey_2_k     16*24    // store XOR of High 64 bits and Low 64 bits of  HashKey^2 <<1 mod poly here (for Karatsuba purposes)
#define HashKey_3_k     16*25   // store XOR of High 64 bits and Low 64 bits of  HashKey^3 <<1 mod poly here (for Karatsuba purposes)
#define HashKey_4_k     16*26   // store XOR of High 64 bits and Low 64 bits of  HashKey^4 <<1 mod poly here (for Karatsuba purposes)
#define HashKey_5_k     16*27   // store XOR of High 64 bits and Low 64 bits of  HashKey^5 <<1 mod poly here (for Karatsuba purposes)
#define HashKey_6_k     16*28   // store XOR of High 64 bits and Low 64 bits of  HashKey^6 <<1 mod poly here (for Karatsuba purposes)
#define HashKey_7_k     16*29   // store XOR of High 64 bits and Low 64 bits of  HashKey^7 <<1 mod poly here (for Karatsuba purposes)
#define HashKey_8_k     16*30   // store XOR of High 64 bits and Low 64 bits of  HashKey^8 <<1 mod poly here (for Karatsuba purposes)
#endif

#define AadHash		16*0	// store current Hash of data which has been input
#define AadLen		16*1	// store length of input data which will not be encrypted or decrypted
#define InLen		(16*1)+8 // store length of input data which will be encrypted or decrypted
#define PBlockEncKey	16*2	// encryption key for the partial block at the end of the previous update
#define OrigIV		16*3	// input IV
#define CurCount	16*4	// Current counter for generation of encryption key
#define PBlockLen	16*5	// length of partial block at the end of the previous update

.macro xmmreg name, num
	.set xmm\name, %xmm\num
.endm

#define arg(x) (STACK_OFFSET + 8*(x))(%r14)


#if __OUTPUT_FORMAT__ != elf64
#define arg1 %rcx
#define arg2 %rdx
#define arg3 %r8
#define arg4 %r9
#define arg5 %rsi
#define arg6 (STACK_OFFSET + 8*6)(%r14)
#define arg7 (STACK_OFFSET + 8*7)(%r14)
#define arg8 (STACK_OFFSET + 8*8)(%r14)
#define arg9 (STACK_OFFSET + 8*9)(%r14)
#define arg10 (STACK_OFFSET + 8*10)(%r14)
#else
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
#endif

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
