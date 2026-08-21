// SPDX-License-Identifier: Apache-2.0
/*
 * Copyright 2014-2025 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Generated from OpenSSL 4.0.1 crypto/modes/asm/ghashv8-armx.pl, which
 * was written by Andy Polyakov <appro@openssl.org> for the OpenSSL
 * project. The original module is dual licensed under OpenSSL and
 * CRYPTOGAMS licenses, see https://github.com/dot-asm/cryptogams/ for
 * details.
 *
 * Modified for use in OpenZFS:
 * - the required parts of OpenSSL's crypto/arm_arch.h are inlined
 * - the unused gcm_init_v8(), gcm_ghash_v8() and gcm_ghash_v8_4x()
 *   functions are removed; the body of gcm_gmult_v8() is unmodified
 *
 * gcm_gmult_v8() treats Xi as a byte string in the standard GHASH
 * (big-endian) representation. The Htable argument holds the
 * pre-computed "twisted" hash key H and its Karatsuba mid-term, see
 * algs/modes/gcm_pmull.c for how they are derived.
 */

#if defined(__aarch64__) && HAVE_SIMD(ARMV8_PMULL)

/* Inlined from OpenSSL's crypto/arm_arch.h: */
#if defined(__ARM_FEATURE_BTI_DEFAULT) && __ARM_FEATURE_BTI_DEFAULT == 1
#define GNU_PROPERTY_AARCH64_BTI (1 << 0) /* Has Branch Target Identification */
#define AARCH64_VALID_CALL_TARGET hint #34 /* BTI 'c' */
#else
#define GNU_PROPERTY_AARCH64_BTI 0 /* No Branch Target Identification */
#define AARCH64_VALID_CALL_TARGET
#endif

#if defined(__ARM_FEATURE_PAC_DEFAULT) && (__ARM_FEATURE_PAC_DEFAULT & 1) == 1 /* Signed with A-key */
#define GNU_PROPERTY_AARCH64_POINTER_AUTH \
    (1 << 1) /* Has Pointer Authentication */
#define AARCH64_SIGN_LINK_REGISTER hint #25 /* PACIASP */
#define AARCH64_VALIDATE_LINK_REGISTER hint #29 /* AUTIASP */
#elif defined(__ARM_FEATURE_PAC_DEFAULT) && (__ARM_FEATURE_PAC_DEFAULT & 2) == 2 /* Signed with B-key */
#define GNU_PROPERTY_AARCH64_POINTER_AUTH \
    (1 << 1) /* Has Pointer Authentication */
#define AARCH64_SIGN_LINK_REGISTER hint #27 /* PACIBSP */
#define AARCH64_VALIDATE_LINK_REGISTER hint #31 /* AUTIBSP */
#else
#define GNU_PROPERTY_AARCH64_POINTER_AUTH 0 /* No Pointer Authentication */
#if GNU_PROPERTY_AARCH64_BTI != 0
#define AARCH64_SIGN_LINK_REGISTER AARCH64_VALID_CALL_TARGET
#else
#define AARCH64_SIGN_LINK_REGISTER
#endif
#define AARCH64_VALIDATE_LINK_REGISTER
#endif

#if GNU_PROPERTY_AARCH64_POINTER_AUTH != 0 || GNU_PROPERTY_AARCH64_BTI != 0
.pushsection .note.gnu.property, "a";
.balign 8;
.long 4;
.long 0x10;
.long 0x5;
.asciz "GNU";
.long 0xc0000000; /* GNU_PROPERTY_AARCH64_FEATURE_1_AND */
.long 4;
.long(GNU_PROPERTY_AARCH64_POINTER_AUTH | GNU_PROPERTY_AARCH64_BTI);
.long 0;
.popsection;
#endif
/* End of arm_arch.h */

.arch	armv8-a+crypto
.text
.globl	gcm_gmult_v8
.type	gcm_gmult_v8,%function
.align	4
gcm_gmult_v8:
	AARCH64_VALID_CALL_TARGET
	ld1	{v17.2d},[x0]		//load Xi
	movi	v19.16b,#0xe1
	ld1	{v20.2d,v21.2d},[x1]	//load twisted H, ...
	shl	v19.2d,v19.2d,#57
#ifndef __AARCH64EB__
	rev64	v17.16b,v17.16b
#endif
	ext	v3.16b,v17.16b,v17.16b,#8

	pmull	v0.1q,v20.1d,v3.1d		//H.lo·Xi.lo
	eor	v17.16b,v17.16b,v3.16b		//Karatsuba pre-processing
	pmull2	v2.1q,v20.2d,v3.2d		//H.hi·Xi.hi
	pmull	v1.1q,v21.1d,v17.1d		//(H.lo+H.hi)·(Xi.lo+Xi.hi)

	ext	v17.16b,v0.16b,v2.16b,#8		//Karatsuba post-processing
	eor	v18.16b,v0.16b,v2.16b
	eor	v1.16b,v1.16b,v17.16b
	eor	v1.16b,v1.16b,v18.16b
	pmull	v18.1q,v0.1d,v19.1d		//1st phase of reduction

	ins	v2.d[0],v1.d[1]
	ins	v1.d[1],v0.d[0]
	eor	v0.16b,v1.16b,v18.16b

	ext	v18.16b,v0.16b,v0.16b,#8		//2nd phase of reduction
	pmull	v0.1q,v0.1d,v19.1d
	eor	v18.16b,v18.16b,v2.16b
	eor	v0.16b,v0.16b,v18.16b

#ifndef __AARCH64EB__
	rev64	v0.16b,v0.16b
#endif
	ext	v0.16b,v0.16b,v0.16b,#8
	st1	{v0.2d},[x0]		//write out Xi

	ret
.size	gcm_gmult_v8,.-gcm_gmult_v8
.section	.rodata
.byte	71,72,65,83,72,32,102,111,114,32,65,82,77,118,56,44,32,67,82,89,80,84,79,71,65,77,83,32,98,121,32,60,104,116,116,112,115,58,47,47,103,105,116,104,117,98,46,99,111,109,47,100,111,116,45,97,115,109,62,0
.align	2
.align	2

/* Mark the stack non-executable. */
#ifdef __ELF__
.section .note.GNU-stack,"",%progbits
#endif

#endif /* defined(__aarch64__) && HAVE_SIMD(ARMV8_PMULL) */
