/*
 * Copyright 2011-2024 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the Apache License 2.0 (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#if defined(__aarch64__)
#ifndef OSSL_CRYPTO_ARM_ARCH_H
# define OSSL_CRYPTO_ARM_ARCH_H

# if !defined(__ARM_ARCH__)
#  if defined(__CC_ARM)
#   define __ARM_ARCH__ __TARGET_ARCH_ARM
#   if defined(__BIG_ENDIAN)
#    define __ARMEB__
#   else
#    define __ARMEL__
#   endif
#  elif defined(__GNUC__)
#   if   defined(__aarch64__)
#    define __ARM_ARCH__ 8
  /*
   * Why doesn't gcc define __ARM_ARCH__? Instead it defines
   * bunch of below macros. See all_architectures[] table in
   * gcc/config/arm/arm.c. On a side note it defines
   * __ARMEL__/__ARMEB__ for little-/big-endian.
   */
#   elif defined(__ARM_ARCH)
#    define __ARM_ARCH__ __ARM_ARCH
#   elif defined(__ARM_ARCH_8A__)
#    define __ARM_ARCH__ 8
#   elif defined(__ARM_ARCH_7__) || defined(__ARM_ARCH_7A__)     || \
        defined(__ARM_ARCH_7R__)|| defined(__ARM_ARCH_7M__)     || \
        defined(__ARM_ARCH_7EM__)
#    define __ARM_ARCH__ 7
#   elif defined(__ARM_ARCH_6__) || defined(__ARM_ARCH_6J__)     || \
        defined(__ARM_ARCH_6K__)|| defined(__ARM_ARCH_6M__)     || \
        defined(__ARM_ARCH_6Z__)|| defined(__ARM_ARCH_6ZK__)    || \
        defined(__ARM_ARCH_6T2__)
#    define __ARM_ARCH__ 6
#   elif defined(__ARM_ARCH_5__) || defined(__ARM_ARCH_5T__)     || \
        defined(__ARM_ARCH_5E__)|| defined(__ARM_ARCH_5TE__)    || \
        defined(__ARM_ARCH_5TEJ__)
#    define __ARM_ARCH__ 5
#   elif defined(__ARM_ARCH_4__) || defined(__ARM_ARCH_4T__)
#    define __ARM_ARCH__ 4
#   else
#    error "unsupported ARM architecture"
#   endif
#  elif defined(__ARM_ARCH)
#   define __ARM_ARCH__ __ARM_ARCH
#  endif
# endif

# if !defined(__ARM_MAX_ARCH__)
#  define __ARM_MAX_ARCH__ __ARM_ARCH__
# endif

# if __ARM_MAX_ARCH__<__ARM_ARCH__
#  error "__ARM_MAX_ARCH__ can't be less than __ARM_ARCH__"
# elif __ARM_MAX_ARCH__!=__ARM_ARCH__
#  if __ARM_ARCH__<7 && __ARM_MAX_ARCH__>=7 && defined(__ARMEB__)
#   error "can't build universal big-endian binary"
#  endif
# endif

# ifndef __ASSEMBLER__
extern unsigned int OPENSSL_armcap_P;
extern unsigned int OPENSSL_arm_midr;
extern unsigned int OPENSSL_armv8_rsa_neonized;
# endif

# define ARMV7_NEON      (1<<0)
# define ARMV7_TICK      (1<<1)
# define ARMV8_AES       (1<<2)
# define ARMV8_SHA1      (1<<3)
# define ARMV8_SHA256    (1<<4)
# define ARMV8_PMULL     (1<<5)
# define ARMV8_SHA512    (1<<6)
# define ARMV8_CPUID     (1<<7)
# define ARMV8_RNG       (1<<8)
# define ARMV8_SM3       (1<<9)
# define ARMV8_SM4       (1<<10)
# define ARMV8_SHA3      (1<<11)
# define ARMV8_UNROLL8_EOR3      (1<<12)
# define ARMV8_SVE       (1<<13)
# define ARMV8_SVE2      (1<<14)
# define ARMV8_HAVE_SHA3_AND_WORTH_USING     (1<<15)
# define ARMV8_UNROLL12_EOR3     (1<<16)

/*
 * MIDR_EL1 system register
 *
 * 63___ _ ___32_31___ _ ___24_23_____20_19_____16_15__ _ __4_3_______0
 * |            |             |         |         |          |        |
 * |RES0        | Implementer | Variant | Arch    | PartNum  |Revision|
 * |____ _ _____|_____ _ _____|_________|_______ _|____ _ ___|________|
 *
 */

# define ARM_CPU_IMP_ARM           0x41
# define HISI_CPU_IMP              0x48
# define ARM_CPU_IMP_APPLE         0x61
# define ARM_CPU_IMP_MICROSOFT     0x6D
# define ARM_CPU_IMP_AMPERE        0xC0

# define ARM_CPU_PART_CORTEX_A72   0xD08
# define ARM_CPU_PART_N1           0xD0C
# define ARM_CPU_PART_V1           0xD40
# define ARM_CPU_PART_N2           0xD49
# define HISI_CPU_PART_KP920       0xD01
# define ARM_CPU_PART_V2           0xD4F

# define APPLE_CPU_PART_M1_ICESTORM         0x022
# define APPLE_CPU_PART_M1_FIRESTORM        0x023
# define APPLE_CPU_PART_M1_ICESTORM_PRO     0x024
# define APPLE_CPU_PART_M1_FIRESTORM_PRO    0x025
# define APPLE_CPU_PART_M1_ICESTORM_MAX     0x028
# define APPLE_CPU_PART_M1_FIRESTORM_MAX    0x029
# define APPLE_CPU_PART_M2_BLIZZARD         0x032
# define APPLE_CPU_PART_M2_AVALANCHE        0x033
# define APPLE_CPU_PART_M2_BLIZZARD_PRO     0x034
# define APPLE_CPU_PART_M2_AVALANCHE_PRO    0x035
# define APPLE_CPU_PART_M2_BLIZZARD_MAX     0x038
# define APPLE_CPU_PART_M2_AVALANCHE_MAX    0x039

# define MICROSOFT_CPU_PART_COBALT_100      0xD49

# define MIDR_PARTNUM_SHIFT       4
# define MIDR_PARTNUM_MASK        (0xfffU << MIDR_PARTNUM_SHIFT)
# define MIDR_PARTNUM(midr)       \
           (((midr) & MIDR_PARTNUM_MASK) >> MIDR_PARTNUM_SHIFT)

# define MIDR_IMPLEMENTER_SHIFT   24
# define MIDR_IMPLEMENTER_MASK    (0xffU << MIDR_IMPLEMENTER_SHIFT)
# define MIDR_IMPLEMENTER(midr)   \
           (((midr) & MIDR_IMPLEMENTER_MASK) >> MIDR_IMPLEMENTER_SHIFT)

# define MIDR_ARCHITECTURE_SHIFT  16
# define MIDR_ARCHITECTURE_MASK   (0xfU << MIDR_ARCHITECTURE_SHIFT)
# define MIDR_ARCHITECTURE(midr)  \
           (((midr) & MIDR_ARCHITECTURE_MASK) >> MIDR_ARCHITECTURE_SHIFT)

# define MIDR_CPU_MODEL_MASK \
           (MIDR_IMPLEMENTER_MASK | \
            MIDR_PARTNUM_MASK     | \
            MIDR_ARCHITECTURE_MASK)

# define MIDR_CPU_MODEL(imp, partnum) \
           (((imp)     << MIDR_IMPLEMENTER_SHIFT)  | \
            (0xfU      << MIDR_ARCHITECTURE_SHIFT) | \
            ((partnum) << MIDR_PARTNUM_SHIFT))

# define MIDR_IS_CPU_MODEL(midr, imp, partnum) \
           (((midr) & MIDR_CPU_MODEL_MASK) == MIDR_CPU_MODEL(imp, partnum))

#if defined(__ASSEMBLER__)

   /*
    * Support macros for
    *   - Armv8.3-A Pointer Authentication and
    *   - Armv8.5-A Branch Target Identification
    * features which require emitting a .note.gnu.property section with the
    * appropriate architecture-dependent feature bits set.
    * Read more: "ELF for the Arm® 64-bit Architecture"
    */

#  if defined(__ARM_FEATURE_BTI_DEFAULT) && __ARM_FEATURE_BTI_DEFAULT == 1
#   define GNU_PROPERTY_AARCH64_BTI (1 << 0)   /* Has Branch Target Identification */
#   define AARCH64_VALID_CALL_TARGET hint #34  /* BTI 'c' */
#  else
#   define GNU_PROPERTY_AARCH64_BTI 0  /* No Branch Target Identification */
#   define AARCH64_VALID_CALL_TARGET
#  endif

#  if defined(__ARM_FEATURE_PAC_DEFAULT) && \
       (__ARM_FEATURE_PAC_DEFAULT & 1) == 1  /* Signed with A-key */
#   define GNU_PROPERTY_AARCH64_POINTER_AUTH \
     (1 << 1)                                       /* Has Pointer Authentication */
#   define AARCH64_SIGN_LINK_REGISTER hint #25      /* PACIASP */
#   define AARCH64_VALIDATE_LINK_REGISTER hint #29  /* AUTIASP */
#  elif defined(__ARM_FEATURE_PAC_DEFAULT) && \
       (__ARM_FEATURE_PAC_DEFAULT & 2) == 2  /* Signed with B-key */
#   define GNU_PROPERTY_AARCH64_POINTER_AUTH \
     (1 << 1)                                       /* Has Pointer Authentication */
#   define AARCH64_SIGN_LINK_REGISTER hint #27      /* PACIBSP */
#   define AARCH64_VALIDATE_LINK_REGISTER hint #31  /* AUTIBSP */
#  else
#   define GNU_PROPERTY_AARCH64_POINTER_AUTH 0  /* No Pointer Authentication */
#   if GNU_PROPERTY_AARCH64_BTI != 0
#    define AARCH64_SIGN_LINK_REGISTER AARCH64_VALID_CALL_TARGET
#   else
#    define AARCH64_SIGN_LINK_REGISTER
#   endif
#   define AARCH64_VALIDATE_LINK_REGISTER
#  endif

#  if GNU_PROPERTY_AARCH64_POINTER_AUTH != 0 || GNU_PROPERTY_AARCH64_BTI != 0
    .pushsection .note.gnu.property, "a";
    .balign 8;
    .long 4;
    .long 0x10;
    .long 0x5;
    .asciz "GNU";
    .long 0xc0000000; /* GNU_PROPERTY_AARCH64_FEATURE_1_AND */
    .long 4;
    .long (GNU_PROPERTY_AARCH64_POINTER_AUTH | GNU_PROPERTY_AARCH64_BTI);
    .long 0;
    .popsection;
#  endif

# endif  /* defined __ASSEMBLER__ */

# define IS_CPU_SUPPORT_UNROLL8_EOR3() \
           (OPENSSL_armcap_P & ARMV8_UNROLL8_EOR3)

#endif

.arch	armv8-a+crypto
.text
.align	5
.Lrcon:
.long	0x01,0x01,0x01,0x01
.long	0x0c0f0e0d,0x0c0f0e0d,0x0c0f0e0d,0x0c0f0e0d	// rotate-n-splat
.long	0x1b,0x1b,0x1b,0x1b

.globl	aes_v8_set_encrypt_key
.type	aes_v8_set_encrypt_key,%function
.align	5
aes_v8_set_encrypt_key:
.Lenc_key:
	AARCH64_VALID_CALL_TARGET
	// Armv8.3-A PAuth: even though x30 is pushed to stack it is not popped later.
	stp	x29,x30,[sp,#-16]!
	add	x29,sp,#0
	mov	x3,#-1
	cmp	x0,#0
	b.eq	.Lenc_key_abort
	cmp	x2,#0
	b.eq	.Lenc_key_abort
	mov	x3,#-2
	cmp	w1,#128
	b.lt	.Lenc_key_abort
	cmp	w1,#256
	b.gt	.Lenc_key_abort
	tst	w1,#0x3f
	b.ne	.Lenc_key_abort

	adr	x3,.Lrcon
	cmp	w1,#192

	eor	v0.16b,v0.16b,v0.16b
	ld1	{v3.16b},[x0],#16
	mov	w1,#8		// reuse w1
	ld1	{v1.4s,v2.4s},[x3],#32

	b.lt	.Loop128
	b.eq	.L192
	b	.L256

.align	4
.Loop128:
	tbl	v6.16b,{v3.16b},v2.16b
	ext	v5.16b,v0.16b,v3.16b,#12
	st1	{v3.4s},[x2],#16
	aese	v6.16b,v0.16b
	subs	w1,w1,#1

	eor	v3.16b,v3.16b,v5.16b
	ext	v5.16b,v0.16b,v5.16b,#12
	eor	v3.16b,v3.16b,v5.16b
	ext	v5.16b,v0.16b,v5.16b,#12
	eor	v6.16b,v6.16b,v1.16b
	eor	v3.16b,v3.16b,v5.16b
	shl	v1.16b,v1.16b,#1
	eor	v3.16b,v3.16b,v6.16b
	b.ne	.Loop128

	ld1	{v1.4s},[x3]

	tbl	v6.16b,{v3.16b},v2.16b
	ext	v5.16b,v0.16b,v3.16b,#12
	st1	{v3.4s},[x2],#16
	aese	v6.16b,v0.16b

	eor	v3.16b,v3.16b,v5.16b
	ext	v5.16b,v0.16b,v5.16b,#12
	eor	v3.16b,v3.16b,v5.16b
	ext	v5.16b,v0.16b,v5.16b,#12
	eor	v6.16b,v6.16b,v1.16b
	eor	v3.16b,v3.16b,v5.16b
	shl	v1.16b,v1.16b,#1
	eor	v3.16b,v3.16b,v6.16b

	tbl	v6.16b,{v3.16b},v2.16b
	ext	v5.16b,v0.16b,v3.16b,#12
	st1	{v3.4s},[x2],#16
	aese	v6.16b,v0.16b

	eor	v3.16b,v3.16b,v5.16b
	ext	v5.16b,v0.16b,v5.16b,#12
	eor	v3.16b,v3.16b,v5.16b
	ext	v5.16b,v0.16b,v5.16b,#12
	eor	v6.16b,v6.16b,v1.16b
	eor	v3.16b,v3.16b,v5.16b
	eor	v3.16b,v3.16b,v6.16b
	st1	{v3.4s},[x2]
	add	x2,x2,#0x50

	mov	w12,#10
	b	.Ldone

.align	4
.L192:
	ld1	{v4.8b},[x0],#8
	movi	v6.16b,#8			// borrow v6.16b
	st1	{v3.4s},[x2],#16
	sub	v2.16b,v2.16b,v6.16b	// adjust the mask

.Loop192:
	tbl	v6.16b,{v4.16b},v2.16b
	ext	v5.16b,v0.16b,v3.16b,#12
#ifdef __AARCH64EB__
	st1	{v4.4s},[x2],#16
	sub	x2,x2,#8
#else
	st1	{v4.8b},[x2],#8
#endif
	aese	v6.16b,v0.16b
	subs	w1,w1,#1

	eor	v3.16b,v3.16b,v5.16b
	ext	v5.16b,v0.16b,v5.16b,#12
	eor	v3.16b,v3.16b,v5.16b
	ext	v5.16b,v0.16b,v5.16b,#12
	eor	v3.16b,v3.16b,v5.16b

	dup	v5.4s,v3.s[3]
	eor	v5.16b,v5.16b,v4.16b
	eor	v6.16b,v6.16b,v1.16b
	ext	v4.16b,v0.16b,v4.16b,#12
	shl	v1.16b,v1.16b,#1
	eor	v4.16b,v4.16b,v5.16b
	eor	v3.16b,v3.16b,v6.16b
	eor	v4.16b,v4.16b,v6.16b
	st1	{v3.4s},[x2],#16
	b.ne	.Loop192

	mov	w12,#12
	add	x2,x2,#0x20
	b	.Ldone

.align	4
.L256:
	ld1	{v4.16b},[x0]
	mov	w1,#7
	mov	w12,#14
	st1	{v3.4s},[x2],#16

.Loop256:
	tbl	v6.16b,{v4.16b},v2.16b
	ext	v5.16b,v0.16b,v3.16b,#12
	st1	{v4.4s},[x2],#16
	aese	v6.16b,v0.16b
	subs	w1,w1,#1

	eor	v3.16b,v3.16b,v5.16b
	ext	v5.16b,v0.16b,v5.16b,#12
	eor	v3.16b,v3.16b,v5.16b
	ext	v5.16b,v0.16b,v5.16b,#12
	eor	v6.16b,v6.16b,v1.16b
	eor	v3.16b,v3.16b,v5.16b
	shl	v1.16b,v1.16b,#1
	eor	v3.16b,v3.16b,v6.16b
	st1	{v3.4s},[x2],#16
	b.eq	.Ldone

	dup	v6.4s,v3.s[3]		// just splat
	ext	v5.16b,v0.16b,v4.16b,#12
	aese	v6.16b,v0.16b

	eor	v4.16b,v4.16b,v5.16b
	ext	v5.16b,v0.16b,v5.16b,#12
	eor	v4.16b,v4.16b,v5.16b
	ext	v5.16b,v0.16b,v5.16b,#12
	eor	v4.16b,v4.16b,v5.16b

	eor	v4.16b,v4.16b,v6.16b
	b	.Loop256

.Ldone:
	// str	w12,[x2] // don't store, there is the next key in ICP
	mov	x3,#0

.Lenc_key_abort:
	mov	x0,x3			// return value
	ldr	x29,[sp],#16
	ret
.size	aes_v8_set_encrypt_key,.-aes_v8_set_encrypt_key

.globl	aes_v8_set_decrypt_key
.type	aes_v8_set_decrypt_key,%function
.align	5
aes_v8_set_decrypt_key:
	AARCH64_SIGN_LINK_REGISTER
	stp	x29,x30,[sp,#-16]!
	add	x29,sp,#0
	bl	.Lenc_key

	cmp	x0,#0
	b.ne	.Ldec_key_abort

	sub	x2,x2,#240		// restore original x2
	mov	x4,#-16
	add	x0,x2,x12,lsl#4	// end of key schedule

	ld1	{v0.4s},[x2]
	ld1	{v1.4s},[x0]
	st1	{v0.4s},[x0],x4
	st1	{v1.4s},[x2],#16

.Loop_imc:
	ld1	{v0.4s},[x2]
	ld1	{v1.4s},[x0]
	aesimc	v0.16b,v0.16b
	aesimc	v1.16b,v1.16b
	st1	{v0.4s},[x0],x4
	st1	{v1.4s},[x2],#16
	cmp	x0,x2
	b.hi	.Loop_imc

	ld1	{v0.4s},[x2]
	aesimc	v0.16b,v0.16b
	st1	{v0.4s},[x0]

	eor	x0,x0,x0		// return value
.Ldec_key_abort:
	ldp	x29,x30,[sp],#16
	AARCH64_VALIDATE_LINK_REGISTER
	ret
.size	aes_v8_set_decrypt_key,.-aes_v8_set_decrypt_key

.globl	aes_v8_encrypt
.type	aes_v8_encrypt,%function
.align	5
aes_v8_encrypt:
	AARCH64_VALID_CALL_TARGET
	// ldr	w3,[x2,#240] not required in ICP interface
	ld1	{v0.4s},[x2],#16
	ld1	{v2.16b},[x0]
	sub	w3,w3,#2
	ld1	{v1.4s},[x2],#16

.Loop_enc:
	aese	v2.16b,v0.16b
	aesmc	v2.16b,v2.16b
	ld1	{v0.4s},[x2],#16
	subs	w3,w3,#2
	aese	v2.16b,v1.16b
	aesmc	v2.16b,v2.16b
	ld1	{v1.4s},[x2],#16
	b.gt	.Loop_enc

	aese	v2.16b,v0.16b
	aesmc	v2.16b,v2.16b
	ld1	{v0.4s},[x2]
	aese	v2.16b,v1.16b
	eor	v2.16b,v2.16b,v0.16b

	st1	{v2.16b},[x1]
	ret
.size	aes_v8_encrypt,.-aes_v8_encrypt

.globl	aes_v8_decrypt
.type	aes_v8_decrypt,%function
.align	5
aes_v8_decrypt:
	AARCH64_VALID_CALL_TARGET
	// ldr	w3,[x2,#240] not required in ICP interface
	ld1	{v0.4s},[x2],#16
	ld1	{v2.16b},[x0]
	sub	w3,w3,#2
	ld1	{v1.4s},[x2],#16

.Loop_dec:
	aesd	v2.16b,v0.16b
	aesimc	v2.16b,v2.16b
	ld1	{v0.4s},[x2],#16
	subs	w3,w3,#2
	aesd	v2.16b,v1.16b
	aesimc	v2.16b,v2.16b
	ld1	{v1.4s},[x2],#16
	b.gt	.Loop_dec

	aesd	v2.16b,v0.16b
	aesimc	v2.16b,v2.16b
	ld1	{v0.4s},[x2]
	aesd	v2.16b,v1.16b
	eor	v2.16b,v2.16b,v0.16b

	st1	{v2.16b},[x1]
	ret
.size	aes_v8_decrypt,.-aes_v8_decrypt

#endif