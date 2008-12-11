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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_MACHELF_H
#define	_SYS_MACHELF_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#ifdef	__cplusplus
extern "C" {
#endif

#if defined(__amd64)
#include <sys/elf_amd64.h>
#elif defined(__i386)
#include <sys/elf_386.h>
#elif defined(__sparc)
#include <sys/elf_SPARC.h>
#endif
#ifndef	_ASM
#include <sys/types.h>
#include <sys/elf.h>
#include <sys/link.h>	/* for Elf*_Dyn */
#endif	/* _ASM */

/*
 * Make machine class dependent data types transparent to the common code
 */
#if defined(_ELF64) && !defined(_ELF32_COMPAT)

#ifndef	_ASM
typedef	Elf64_Xword	Xword;
typedef	Elf64_Lword	Lword;
typedef	Elf64_Sxword	Sxword;
typedef	Elf64_Word	Word;
typedef	Elf64_Sword	Sword;
typedef	Elf64_Half	Half;
typedef	Elf64_Addr	Addr;
typedef	Elf64_Off	Off;
typedef	uchar_t		Byte;
#endif	/* _ASM */

#if defined(_KERNEL)
#define	ELF_R_TYPE	ELF64_R_TYPE
#define	ELF_R_SYM	ELF64_R_SYM
#define	ELF_R_TYPE_DATA ELF64_R_TYPE_DATA
#define	ELF_R_INFO	ELF64_R_INFO
#define	ELF_ST_BIND	ELF64_ST_BIND
#define	ELF_ST_TYPE	ELF64_ST_TYPE
#define	ELF_M_SYM	ELF64_M_SYM
#define	ELF_M_SIZE	ELF64_M_SIZE
#endif

#ifndef	_ASM
typedef	Elf64_Ehdr	Ehdr;
typedef	Elf64_Shdr	Shdr;
typedef	Elf64_Sym	Sym;
typedef	Elf64_Syminfo	Syminfo;
typedef	Elf64_Rela	Rela;
typedef	Elf64_Rel	Rel;
typedef	Elf64_Nhdr	Nhdr;
typedef	Elf64_Phdr	Phdr;
typedef	Elf64_Dyn	Dyn;
typedef	Elf64_Boot	Boot;
typedef	Elf64_Verdef	Verdef;
typedef	Elf64_Verdaux	Verdaux;
typedef	Elf64_Verneed	Verneed;
typedef	Elf64_Vernaux	Vernaux;
typedef	Elf64_Versym	Versym;
typedef	Elf64_Move	Move;
typedef	Elf64_Cap	Cap;
#endif	/* _ASM */

#else	/* _ILP32 */

#ifndef	_ASM
typedef	Elf32_Word	Xword;	/* Xword/Sxword are 32-bits in Elf32 */
typedef	Elf32_Lword	Lword;
typedef	Elf32_Sword	Sxword;
typedef	Elf32_Word	Word;
typedef	Elf32_Sword	Sword;
typedef	Elf32_Half	Half;
typedef	Elf32_Addr	Addr;
typedef	Elf32_Off	Off;
typedef	uchar_t		Byte;
#endif	/* _ASM */

#if defined(_KERNEL)
#define	ELF_R_TYPE	ELF32_R_TYPE
#define	ELF_R_SYM	ELF32_R_SYM
#define	ELF_R_TYPE_DATA(x)	(0)
#define	ELF_R_INFO	ELF32_R_INFO
#define	ELF_ST_BIND	ELF32_ST_BIND
#define	ELF_ST_TYPE	ELF32_ST_TYPE
#define	ELF_M_SYM	ELF32_M_SYM
#define	ELF_M_SIZE	ELF32_M_SIZE
#endif

#ifndef	_ASM
typedef	Elf32_Ehdr	Ehdr;
typedef	Elf32_Shdr	Shdr;
typedef	Elf32_Sym	Sym;
typedef	Elf32_Syminfo	Syminfo;
typedef	Elf32_Rela	Rela;
typedef	Elf32_Rel	Rel;
typedef	Elf32_Nhdr	Nhdr;
typedef	Elf32_Phdr	Phdr;
typedef	Elf32_Dyn	Dyn;
typedef	Elf32_Boot	Boot;
typedef	Elf32_Verdef	Verdef;
typedef	Elf32_Verdaux	Verdaux;
typedef	Elf32_Verneed	Verneed;
typedef	Elf32_Vernaux	Vernaux;
typedef	Elf32_Versym	Versym;
typedef	Elf32_Move	Move;
typedef	Elf32_Cap	Cap;
#endif	/* _ASM */

#endif	/* _ILP32 */

/*
 * Elf `printf' type-cast macros.  These force arguments to be a fixed size
 * so that Elf32 and Elf64 can share common format strings.
 */
#ifndef	__lint
#define	EC_ADDR(a)	((Elf64_Addr)(a))		/* "ull" */
#define	EC_OFF(a)	((Elf64_Off)(a))		/* "ull"  */
#define	EC_HALF(a)	((Elf64_Half)(a))		/* "d"   */
#define	EC_WORD(a)	((Elf64_Word)(a))		/* "u"   */
#define	EC_SWORD(a)	((Elf64_Sword)(a))		/* "d"   */
#define	EC_XWORD(a)	((Elf64_Xword)(a))		/* "ull" */
#define	EC_SXWORD(a)	((Elf64_Sxword)(a))		/* "ll"  */
#define	EC_LWORD(a)	((Elf64_Lword)(a))		/* "ull" */

/*
 * A native pointer is special.  Although it can be convenient to display
 * these from a common format (ull), compilers may flag the cast of a pointer
 * to an integer as illegal.  Casting these pointers to the native pointer
 * size, suppresses any compiler errors.
 */
#define	EC_NATPTR(a)	((Elf64_Xword)(uintptr_t)(a))	/* "ull" */
#else
#define	EC_ADDR(a)	((u_longlong_t)(a))
#define	EC_OFF(a)	((u_longlong_t)(a))
#define	EC_HALF(a)	((ushort_t)(a))
#define	EC_WORD(a)	((uint_t)(a))
#define	EC_SWORD(a)	((int)(a))
#define	EC_XWORD(a)	((u_longlong_t)(a))
#define	EC_SXWORD(a)	((longlong_t)(a))
#define	EC_LWORD(a)	((u_longlong_t)(a))

#define	EC_NATPTR(a)	((u_longlong_t)(a))
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_MACHELF_H */
