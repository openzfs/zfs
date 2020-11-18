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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

	.ident	"%Z%%M%	%I%	%E% SMI"

	.file	"%M%"

#define _ASM
#ifdef __linux__
#include <ia32/sys/asm_linkage.h>
#elif __FreeBSD__
#include <machine/asmacros.h>
#define SET_SIZE(x)
#endif
	ENTRY(atomic_inc_8)
	ALTENTRY(atomic_inc_uchar)
	movl	4(%esp), %eax
	lock
	incb	(%eax)
	ret
	SET_SIZE(atomic_inc_uchar)
	SET_SIZE(atomic_inc_8)

	ENTRY(atomic_inc_16)
	ALTENTRY(atomic_inc_ushort)
	movl	4(%esp), %eax
	lock
	incw	(%eax)
	ret
	SET_SIZE(atomic_inc_ushort)
	SET_SIZE(atomic_inc_16)

	ENTRY(atomic_inc_32)
	ALTENTRY(atomic_inc_uint)
	ALTENTRY(atomic_inc_ulong)
	movl	4(%esp), %eax
	lock
	incl	(%eax)
	ret
	SET_SIZE(atomic_inc_ulong)
	SET_SIZE(atomic_inc_uint)
	SET_SIZE(atomic_inc_32)

	ENTRY(atomic_inc_8_nv)
	ALTENTRY(atomic_inc_uchar_nv)
	movl	4(%esp), %edx
	movb	(%edx), %al
1:
	leal	1(%eax), %ecx
	lock
	cmpxchgb %cl, (%edx)
	jne	1b
	movzbl	%cl, %eax
	ret
	SET_SIZE(atomic_inc_uchar_nv)
	SET_SIZE(atomic_inc_8_nv)

	ENTRY(atomic_inc_16_nv)
	ALTENTRY(atomic_inc_ushort_nv)
	movl	4(%esp), %edx
	movw	(%edx), %ax
1:
	leal	1(%eax), %ecx
	lock
	cmpxchgw %cx, (%edx)
	jne	1b
	movzwl	%cx, %eax
	ret
	SET_SIZE(atomic_inc_ushort_nv)
	SET_SIZE(atomic_inc_16_nv)

	ENTRY(atomic_inc_32_nv)
	ALTENTRY(atomic_inc_uint_nv)
	ALTENTRY(atomic_inc_ulong_nv)
	movl	4(%esp), %edx
	movl	(%edx), %eax
1:
	leal	1(%eax), %ecx
	lock
	cmpxchgl %ecx, (%edx)
	jne	1b
	movl	%ecx, %eax
	ret
	SET_SIZE(atomic_inc_ulong_nv)
	SET_SIZE(atomic_inc_uint_nv)
	SET_SIZE(atomic_inc_32_nv)

	/*
	 * NOTE: If atomic_inc_64 and atomic_inc_64_nv are ever
	 * separated, you need to also edit the libc i386 platform
	 * specific mapfile and remove the NODYNSORT attribute
	 * from atomic_inc_64_nv.
	 */
	ENTRY(atomic_inc_64)
	ALTENTRY(atomic_inc_64_nv)
	pushl	%edi
	pushl	%ebx
	movl	12(%esp), %edi
	movl	(%edi), %eax
	movl	4(%edi), %edx
1:
	xorl	%ebx, %ebx
	xorl	%ecx, %ecx
	incl	%ebx
	addl	%eax, %ebx
	adcl	%edx, %ecx
	lock
	cmpxchg8b (%edi)
	jne	1b
	movl	%ebx, %eax
	movl	%ecx, %edx
	popl	%ebx
	popl	%edi
	ret
	SET_SIZE(atomic_inc_64_nv)
	SET_SIZE(atomic_inc_64)

	ENTRY(atomic_dec_8)
	ALTENTRY(atomic_dec_uchar)
	movl	4(%esp), %eax
	lock
	decb	(%eax)
	ret
	SET_SIZE(atomic_dec_uchar)
	SET_SIZE(atomic_dec_8)

	ENTRY(atomic_dec_16)
	ALTENTRY(atomic_dec_ushort)
	movl	4(%esp), %eax
	lock
	decw	(%eax)
	ret
	SET_SIZE(atomic_dec_ushort)
	SET_SIZE(atomic_dec_16)

	ENTRY(atomic_dec_32)
	ALTENTRY(atomic_dec_uint)
	ALTENTRY(atomic_dec_ulong)
	movl	4(%esp), %eax
	lock
	decl	(%eax)
	ret
	SET_SIZE(atomic_dec_ulong)
	SET_SIZE(atomic_dec_uint)
	SET_SIZE(atomic_dec_32)

	ENTRY(atomic_dec_8_nv)
	ALTENTRY(atomic_dec_uchar_nv)
	movl	4(%esp), %edx
	movb	(%edx), %al
1:
	leal	-1(%eax), %ecx
	lock
	cmpxchgb %cl, (%edx)
	jne	1b
	movzbl	%cl, %eax
	ret
	SET_SIZE(atomic_dec_uchar_nv)
	SET_SIZE(atomic_dec_8_nv)

	ENTRY(atomic_dec_16_nv)
	ALTENTRY(atomic_dec_ushort_nv)
	movl	4(%esp), %edx
	movw	(%edx), %ax
1:
	leal	-1(%eax), %ecx
	lock
	cmpxchgw %cx, (%edx)
	jne	1b
	movzwl	%cx, %eax
	ret
	SET_SIZE(atomic_dec_ushort_nv)
	SET_SIZE(atomic_dec_16_nv)

	ENTRY(atomic_dec_32_nv)
	ALTENTRY(atomic_dec_uint_nv)
	ALTENTRY(atomic_dec_ulong_nv)
	movl	4(%esp), %edx
	movl	(%edx), %eax
1:
	leal	-1(%eax), %ecx
	lock
	cmpxchgl %ecx, (%edx)
	jne	1b
	movl	%ecx, %eax
	ret
	SET_SIZE(atomic_dec_ulong_nv)
	SET_SIZE(atomic_dec_uint_nv)
	SET_SIZE(atomic_dec_32_nv)

	/*
	 * NOTE: If atomic_dec_64 and atomic_dec_64_nv are ever
	 * separated, it is important to edit the libc i386 platform
	 * specific mapfile and remove the NODYNSORT attribute
	 * from atomic_dec_64_nv.
	 */
	ENTRY(atomic_dec_64)
	ALTENTRY(atomic_dec_64_nv)
	pushl	%edi
	pushl	%ebx
	movl	12(%esp), %edi
	movl	(%edi), %eax
	movl	4(%edi), %edx
1:
	xorl	%ebx, %ebx
	xorl	%ecx, %ecx
	not	%ecx
	not	%ebx
	addl	%eax, %ebx
	adcl	%edx, %ecx
	lock
	cmpxchg8b (%edi)
	jne	1b
	movl	%ebx, %eax
	movl	%ecx, %edx
	popl	%ebx
	popl	%edi
	ret
	SET_SIZE(atomic_dec_64_nv)
	SET_SIZE(atomic_dec_64)

	ENTRY(atomic_add_8)
	ALTENTRY(atomic_add_char)
	movl	4(%esp), %eax
	movl	8(%esp), %ecx
	lock
	addb	%cl, (%eax)
	ret
	SET_SIZE(atomic_add_char)
	SET_SIZE(atomic_add_8)

	ENTRY(atomic_add_16)
	ALTENTRY(atomic_add_short)
	movl	4(%esp), %eax
	movl	8(%esp), %ecx
	lock
	addw	%cx, (%eax)
	ret
	SET_SIZE(atomic_add_short)
	SET_SIZE(atomic_add_16)

	ENTRY(atomic_add_32)
	ALTENTRY(atomic_add_int)
	ALTENTRY(atomic_add_ptr)
	ALTENTRY(atomic_add_long)
	movl	4(%esp), %eax
	movl	8(%esp), %ecx
	lock
	addl	%ecx, (%eax)
	ret
	SET_SIZE(atomic_add_long)
	SET_SIZE(atomic_add_ptr)
	SET_SIZE(atomic_add_int)
	SET_SIZE(atomic_add_32)

	ENTRY(atomic_sub_8)
	ALTENTRY(atomic_sub_char)
	movl	4(%esp), %eax
	movl	8(%esp), %ecx
	lock
	subb	%cl, (%eax)
	ret
	SET_SIZE(atomic_sub_char)
	SET_SIZE(atomic_sub_8)

	ENTRY(atomic_sub_16)
	ALTENTRY(atomic_sub_short)
	movl	4(%esp), %eax
	movl	8(%esp), %ecx
	lock
	subw	%cx, (%eax)
	ret
	SET_SIZE(atomic_sub_short)
	SET_SIZE(atomic_sub_16)

	ENTRY(atomic_sub_32)
	ALTENTRY(atomic_sub_int)
	ALTENTRY(atomic_sub_ptr)
	ALTENTRY(atomic_sub_long)
	movl	4(%esp), %eax
	movl	8(%esp), %ecx
	lock
	subl	%ecx, (%eax)
	ret
	SET_SIZE(atomic_sub_long)
	SET_SIZE(atomic_sub_ptr)
	SET_SIZE(atomic_sub_int)
	SET_SIZE(atomic_sub_32)

	ENTRY(atomic_or_8)
	ALTENTRY(atomic_or_uchar)
	movl	4(%esp), %eax
	movb	8(%esp), %cl
	lock
	orb	%cl, (%eax)
	ret
	SET_SIZE(atomic_or_uchar)
	SET_SIZE(atomic_or_8)

	ENTRY(atomic_or_16)
	ALTENTRY(atomic_or_ushort)
	movl	4(%esp), %eax
	movw	8(%esp), %cx
	lock
	orw	%cx, (%eax)
	ret
	SET_SIZE(atomic_or_ushort)
	SET_SIZE(atomic_or_16)

	ENTRY(atomic_or_32)
	ALTENTRY(atomic_or_uint)
	ALTENTRY(atomic_or_ulong)
	movl	4(%esp), %eax
	movl	8(%esp), %ecx
	lock
	orl	%ecx, (%eax)
	ret
	SET_SIZE(atomic_or_ulong)
	SET_SIZE(atomic_or_uint)
	SET_SIZE(atomic_or_32)

	ENTRY(atomic_and_8)
	ALTENTRY(atomic_and_uchar)
	movl	4(%esp), %eax
	movb	8(%esp), %cl
	lock
	andb	%cl, (%eax)
	ret
	SET_SIZE(atomic_and_uchar)
	SET_SIZE(atomic_and_8)

	ENTRY(atomic_and_16)
	ALTENTRY(atomic_and_ushort)
	movl	4(%esp), %eax
	movw	8(%esp), %cx
	lock
	andw	%cx, (%eax)
	ret
	SET_SIZE(atomic_and_ushort)
	SET_SIZE(atomic_and_16)

	ENTRY(atomic_and_32)
	ALTENTRY(atomic_and_uint)
	ALTENTRY(atomic_and_ulong)
	movl	4(%esp), %eax
	movl	8(%esp), %ecx
	lock
	andl	%ecx, (%eax)
	ret
	SET_SIZE(atomic_and_ulong)
	SET_SIZE(atomic_and_uint)
	SET_SIZE(atomic_and_32)

	ENTRY(atomic_add_8_nv)
	ALTENTRY(atomic_add_char_nv)
	movl	4(%esp), %edx
	movb	(%edx), %al
1:
	movl	8(%esp), %ecx
	addb	%al, %cl
	lock
	cmpxchgb %cl, (%edx)
	jne	1b
	movzbl	%cl, %eax
	ret
	SET_SIZE(atomic_add_char_nv)
	SET_SIZE(atomic_add_8_nv)

	ENTRY(atomic_add_16_nv)
	ALTENTRY(atomic_add_short_nv)
	movl	4(%esp), %edx
	movw	(%edx), %ax
1:
	movl	8(%esp), %ecx
	addw	%ax, %cx
	lock
	cmpxchgw %cx, (%edx)
	jne	1b
	movzwl	%cx, %eax
	ret
	SET_SIZE(atomic_add_short_nv)
	SET_SIZE(atomic_add_16_nv)

	ENTRY(atomic_add_32_nv)
	ALTENTRY(atomic_add_int_nv)
	ALTENTRY(atomic_add_ptr_nv)
	ALTENTRY(atomic_add_long_nv)
	movl	4(%esp), %edx
	movl	(%edx), %eax
1:
	movl	8(%esp), %ecx
	addl	%eax, %ecx
	lock
	cmpxchgl %ecx, (%edx)
	jne	1b
	movl	%ecx, %eax
	ret
	SET_SIZE(atomic_add_long_nv)
	SET_SIZE(atomic_add_ptr_nv)
	SET_SIZE(atomic_add_int_nv)
	SET_SIZE(atomic_add_32_nv)

	ENTRY(atomic_sub_8_nv)
	ALTENTRY(atomic_sub_char_nv)
	movl	4(%esp), %edx
	movb	(%edx), %al
1:
	movl	8(%esp), %ecx
	subb	%al, %cl
	lock
	cmpxchgb %cl, (%edx)
	jne	1b
	movzbl	%cl, %eax
	ret
	SET_SIZE(atomic_sub_char_nv)
	SET_SIZE(atomic_sub_8_nv)

	ENTRY(atomic_sub_16_nv)
	ALTENTRY(atomic_sub_short_nv)
	movl	4(%esp), %edx
	movw	(%edx), %ax
1:
	movl	8(%esp), %ecx
	subw	%ax, %cx
	lock
	cmpxchgw %cx, (%edx)
	jne	1b
	movzwl	%cx, %eax
	ret
	SET_SIZE(atomic_sub_short_nv)
	SET_SIZE(atomic_sub_16_nv)

	ENTRY(atomic_sub_32_nv)
	ALTENTRY(atomic_sub_int_nv)
	ALTENTRY(atomic_sub_ptr_nv)
	ALTENTRY(atomic_sub_long_nv)
	movl	4(%esp), %edx
	movl	(%edx), %eax
1:
	movl	8(%esp), %ecx
	subl	%eax, %ecx
	lock
	cmpxchgl %ecx, (%edx)
	jne	1b
	movl	%ecx, %eax
	ret
	SET_SIZE(atomic_sub_long_nv)
	SET_SIZE(atomic_sub_ptr_nv)
	SET_SIZE(atomic_sub_int_nv)
	SET_SIZE(atomic_sub_32_nv)

	/*
	 * NOTE: If atomic_add_64 and atomic_add_64_nv are ever
	 * separated, it is important to edit the libc i386 platform
	 * specific mapfile and remove the NODYNSORT attribute
	 * from atomic_add_64_nv.
	 */
	ENTRY(atomic_add_64)
	ALTENTRY(atomic_add_64_nv)
	pushl	%edi
	pushl	%ebx
	movl	12(%esp), %edi
	movl	(%edi), %eax
	movl	4(%edi), %edx
1:
	movl	16(%esp), %ebx
	movl	20(%esp), %ecx
	addl	%eax, %ebx
	adcl	%edx, %ecx
	lock
	cmpxchg8b (%edi)
	jne	1b
	movl	%ebx, %eax
	movl	%ecx, %edx
	popl	%ebx
	popl	%edi
	ret
	SET_SIZE(atomic_add_64_nv)
	SET_SIZE(atomic_add_64)

	ENTRY(atomic_sub_64)
	ALTENTRY(atomic_sub_64_nv)
	pushl	%edi
	pushl	%ebx
	movl	12(%esp), %edi
	movl	(%edi), %eax
	movl	4(%edi), %edx
1:
	movl	16(%esp), %ebx
	movl	20(%esp), %ecx
	subl	%eax, %ebx
	sbbl	%edx, %ecx
	lock
	cmpxchg8b (%edi)
	jne	1b
	movl	%ebx, %eax
	movl	%ecx, %edx
	popl	%ebx
	popl	%edi
	ret
	SET_SIZE(atomic_sub_64_nv)
	SET_SIZE(atomic_sub_64)

	ENTRY(atomic_or_8_nv)
	ALTENTRY(atomic_or_uchar_nv)
	movl	4(%esp), %edx
	movb	(%edx), %al
1:
	movl	8(%esp), %ecx
	orb	%al, %cl
	lock
	cmpxchgb %cl, (%edx)
	jne	1b
	movzbl	%cl, %eax
	ret
	SET_SIZE(atomic_or_uchar_nv)
	SET_SIZE(atomic_or_8_nv)

	ENTRY(atomic_or_16_nv)
	ALTENTRY(atomic_or_ushort_nv)
	movl	4(%esp), %edx
	movw	(%edx), %ax
1:
	movl	8(%esp), %ecx
	orw	%ax, %cx
	lock
	cmpxchgw %cx, (%edx)
	jne	1b
	movzwl	%cx, %eax
	ret
	SET_SIZE(atomic_or_ushort_nv)
	SET_SIZE(atomic_or_16_nv)

	ENTRY(atomic_or_32_nv)
	ALTENTRY(atomic_or_uint_nv)
	ALTENTRY(atomic_or_ulong_nv)
	movl	4(%esp), %edx
	movl	(%edx), %eax
1:
	movl	8(%esp), %ecx
	orl	%eax, %ecx
	lock
	cmpxchgl %ecx, (%edx)
	jne	1b
	movl	%ecx, %eax
	ret
	SET_SIZE(atomic_or_ulong_nv)
	SET_SIZE(atomic_or_uint_nv)
	SET_SIZE(atomic_or_32_nv)

	/*
	 * NOTE: If atomic_or_64 and atomic_or_64_nv are ever
	 * separated, it is important to edit the libc i386 platform
	 * specific mapfile and remove the NODYNSORT attribute
	 * from atomic_or_64_nv.
	 */
	ENTRY(atomic_or_64)
	ALTENTRY(atomic_or_64_nv)
	pushl	%edi
	pushl	%ebx
	movl	12(%esp), %edi
	movl	(%edi), %eax
	movl	4(%edi), %edx
1:
	movl	16(%esp), %ebx
	movl	20(%esp), %ecx
	orl	%eax, %ebx
	orl	%edx, %ecx
	lock
	cmpxchg8b (%edi)
	jne	1b
	movl	%ebx, %eax
	movl	%ecx, %edx
	popl	%ebx
	popl	%edi
	ret
	SET_SIZE(atomic_or_64_nv)
	SET_SIZE(atomic_or_64)

	ENTRY(atomic_and_8_nv)
	ALTENTRY(atomic_and_uchar_nv)
	movl	4(%esp), %edx
	movb	(%edx), %al
1:
	movl	8(%esp), %ecx
	andb	%al, %cl
	lock
	cmpxchgb %cl, (%edx)
	jne	1b
	movzbl	%cl, %eax
	ret
	SET_SIZE(atomic_and_uchar_nv)
	SET_SIZE(atomic_and_8_nv)

	ENTRY(atomic_and_16_nv)
	ALTENTRY(atomic_and_ushort_nv)
	movl	4(%esp), %edx
	movw	(%edx), %ax
1:
	movl	8(%esp), %ecx
	andw	%ax, %cx
	lock
	cmpxchgw %cx, (%edx)
	jne	1b
	movzwl	%cx, %eax
	ret
	SET_SIZE(atomic_and_ushort_nv)
	SET_SIZE(atomic_and_16_nv)

	ENTRY(atomic_and_32_nv)
	ALTENTRY(atomic_and_uint_nv)
	ALTENTRY(atomic_and_ulong_nv)
	movl	4(%esp), %edx
	movl	(%edx), %eax
1:
	movl	8(%esp), %ecx
	andl	%eax, %ecx
	lock
	cmpxchgl %ecx, (%edx)
	jne	1b
	movl	%ecx, %eax
	ret
	SET_SIZE(atomic_and_ulong_nv)
	SET_SIZE(atomic_and_uint_nv)
	SET_SIZE(atomic_and_32_nv)

	/*
	 * NOTE: If atomic_and_64 and atomic_and_64_nv are ever
	 * separated, it is important to edit the libc i386 platform
	 * specific mapfile and remove the NODYNSORT attribute
	 * from atomic_and_64_nv.
	 */
	ENTRY(atomic_and_64)
	ALTENTRY(atomic_and_64_nv)
	pushl	%edi
	pushl	%ebx
	movl	12(%esp), %edi
	movl	(%edi), %eax
	movl	4(%edi), %edx
1:
	movl	16(%esp), %ebx
	movl	20(%esp), %ecx
	andl	%eax, %ebx
	andl	%edx, %ecx
	lock
	cmpxchg8b (%edi)
	jne	1b
	movl	%ebx, %eax
	movl	%ecx, %edx
	popl	%ebx
	popl	%edi
	ret
	SET_SIZE(atomic_and_64_nv)
	SET_SIZE(atomic_and_64)

	ENTRY(atomic_cas_8)
	ALTENTRY(atomic_cas_uchar)
	movl	4(%esp), %edx
	movzbl	8(%esp), %eax
	movb	12(%esp), %cl
	lock
	cmpxchgb %cl, (%edx)
	ret
	SET_SIZE(atomic_cas_uchar)
	SET_SIZE(atomic_cas_8)

	ENTRY(atomic_cas_16)
	ALTENTRY(atomic_cas_ushort)
	movl	4(%esp), %edx
	movzwl	8(%esp), %eax
	movw	12(%esp), %cx
	lock
	cmpxchgw %cx, (%edx)
	ret
	SET_SIZE(atomic_cas_ushort)
	SET_SIZE(atomic_cas_16)

	ENTRY(atomic_cas_32)
	ALTENTRY(atomic_cas_uint)
	ALTENTRY(atomic_cas_ulong)
	ALTENTRY(atomic_cas_ptr)
	movl	4(%esp), %edx
	movl	8(%esp), %eax
	movl	12(%esp), %ecx
	lock
	cmpxchgl %ecx, (%edx)
	ret
	SET_SIZE(atomic_cas_ptr)
	SET_SIZE(atomic_cas_ulong)
	SET_SIZE(atomic_cas_uint)
	SET_SIZE(atomic_cas_32)

	ENTRY(atomic_cas_64)
	pushl	%ebx
	pushl	%esi
	movl	12(%esp), %esi
	movl	16(%esp), %eax
	movl	20(%esp), %edx
	movl	24(%esp), %ebx
	movl	28(%esp), %ecx
	lock
	cmpxchg8b (%esi)
	popl	%esi
	popl	%ebx
	ret
	SET_SIZE(atomic_cas_64)

	ENTRY(atomic_swap_8)
	ALTENTRY(atomic_swap_uchar)
	movl	4(%esp), %edx
	movzbl	8(%esp), %eax
	lock
	xchgb	%al, (%edx)
	ret
	SET_SIZE(atomic_swap_uchar)
	SET_SIZE(atomic_swap_8)

	ENTRY(atomic_swap_16)
	ALTENTRY(atomic_swap_ushort)
	movl	4(%esp), %edx
	movzwl	8(%esp), %eax
	lock
	xchgw	%ax, (%edx)
	ret
	SET_SIZE(atomic_swap_ushort)
	SET_SIZE(atomic_swap_16)

	ENTRY(atomic_swap_32)
	ALTENTRY(atomic_swap_uint)
	ALTENTRY(atomic_swap_ptr)
	ALTENTRY(atomic_swap_ulong)
	movl	4(%esp), %edx
	movl	8(%esp), %eax
	lock
	xchgl	%eax, (%edx)
	ret
	SET_SIZE(atomic_swap_ulong)
	SET_SIZE(atomic_swap_ptr)
	SET_SIZE(atomic_swap_uint)
	SET_SIZE(atomic_swap_32)

	ENTRY(atomic_swap_64)
	pushl	%esi
	pushl	%ebx
	movl	12(%esp), %esi
	movl	16(%esp), %ebx
	movl	20(%esp), %ecx
	movl	(%esi), %eax
	movl	4(%esi), %edx
1:
	lock
	cmpxchg8b (%esi)
	jne	1b
	popl	%ebx
	popl	%esi
	ret
	SET_SIZE(atomic_swap_64)

	ENTRY(atomic_set_long_excl)
	movl	4(%esp), %edx
	movl	8(%esp), %ecx
	xorl	%eax, %eax
	lock
	btsl	%ecx, (%edx)
	jnc	1f
	decl	%eax
1:
	ret
	SET_SIZE(atomic_set_long_excl)

	ENTRY(atomic_clear_long_excl)
	movl	4(%esp), %edx
	movl	8(%esp), %ecx
	xorl	%eax, %eax
	lock
	btrl	%ecx, (%edx)
	jc	1f
	decl	%eax
1:
	ret
	SET_SIZE(atomic_clear_long_excl)

	/*
	 * NOTE: membar_enter, membar_exit, membar_producer, and 
	 * membar_consumer are all identical routines. We define them
	 * separately, instead of using ALTENTRY definitions to alias them
	 * together, so that DTrace and debuggers will see a unique address
	 * for them, allowing more accurate tracing.
	*/


	ENTRY(membar_enter)
	lock
	xorl	$0, (%esp)
	ret
	SET_SIZE(membar_enter)

	ENTRY(membar_exit)
	lock
	xorl	$0, (%esp)
	ret
	SET_SIZE(membar_exit)

	ENTRY(membar_producer)
	lock
	xorl	$0, (%esp)
	ret
	SET_SIZE(membar_producer)

	ENTRY(membar_consumer)
	lock
	xorl	$0, (%esp)
	ret
	SET_SIZE(membar_consumer)

#ifdef __ELF__
.section .note.GNU-stack,"",%progbits
#endif
