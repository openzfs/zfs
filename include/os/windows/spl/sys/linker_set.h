/*
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1999 John D. Polstra
 * Copyright (c) 1999,2001 Peter Wemm <peter@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Copyright (c) 2022 Jorgen Lundman <lundman@lundman.net>
 *   - Windows version (x64 clang)
 */

#ifndef	_SYS_LINKER_SET_H_
#define	_SYS_LINKER_SET_H_

/* From cdefs.h */
#ifndef	__has_attribute
#define	__has_attribute(x)	0
#endif

#define	__CONCAT1(x, y)	x ## y
#define	__CONCAT(x, y)	__CONCAT1(x, y)
#define	__STRING(x)	#x		/* stringify without expanding x */
#define	__XSTRING(x)	__STRING(x)	/* expand x, then stringify */

#define	__used		__attribute__((__used__))
#define	__GLOBL(sym)	__asm__(".globl " __XSTRING(sym))
#define	__WEAK(sym)	__asm__(".weak " __XSTRING(sym))

#define	__weak_symbol	/* __attribute__((__weak__)) */

#if __has_attribute(no_sanitize) && defined(__clang__)
#ifdef _KERNEL
#define	__nosanitizeaddress	__attribute__((no_sanitize("kernel-address")))
#else
#define	__nosanitizeaddress	__attribute__((no_sanitize("address")))
#endif
#endif

/*
 * The following macros are used to declare global sets of objects, which
 * are collected by the linker into a `linker_set' as defined below.
 * For ELF, this is done by constructing a separate segment for each set.
 */

#if defined(__powerpc64__) && (!defined(_CALL_ELF) || _CALL_ELF == 1)
/*
 * ELFv1 pointers to functions are actaully pointers to function
 * descriptors.
 *
 * Move the symbol pointer from ".text" to ".data" segment, to make
 * the GCC compiler happy:
 */
#define	__MAKE_SET_CONST
#else
#define	__MAKE_SET_CONST const
#endif

/*
 * The userspace address sanitizer inserts redzones around global variables,
 * violating the assumption that linker set elements are packed.
 */
#ifdef _KERNEL
#define	__NOASAN
#else
#define	__NOASAN	__nosanitizeaddress
#endif

/*
 * set = "mine".
 * ".mine$a" sets the start
 * ".mine$m" all entries
 * ".mine$z" sets the end
 */

#define	__MAKE_SET_QV(set, sym, qv)					\
	__declspec(selectany) __declspec(allocate("." #set "$a"))	\
	__weak_symbol const void * qv __CONCAT(__start_set_, set);	\
	__declspec(selectany) __declspec(allocate("." #set "$z")) 	\
	__weak_symbol const void * qv __CONCAT(__stop_set_, set);	\
	__declspec(allocate("." #set "$m")) const void * qv 		\
	__NOASAN							\
	__set_##set##_sym_##sym 					\
	__used = (void *)&(sym)

#define	__MAKE_SET(set, sym)	__MAKE_SET_QV(set, sym, __MAKE_SET_CONST)

/*
 * Public macros.
 */
#define	TEXT_SET(set, sym)	__MAKE_SET(set, sym)
#define	DATA_SET(set, sym)	__MAKE_SET(set, sym)
#define	DATA_WSET(set, sym)	__MAKE_SET_QV(set, sym, /* */)
#define	BSS_SET(set, sym)	__MAKE_SET(set, sym)
#define	ABS_SET(set, sym)	__MAKE_SET(set, sym)
#define	SET_ENTRY(set, sym)	__MAKE_SET(set, sym)

/*
 * Initialize before referring to a given linker set.
 * We can not do the direct "extern ptype" here, due to:
 * "redeclaration of '__start_set_mine' with a different type: 'int *' vs
 * 'void *'"
 * So we declare a local variable '__xstart_set_mine' and point
 * it at '__start_set_mine' with a cast to (ptype *).
 * We also need to skip first object as it is the start value.
 */
#define	SET_DECLARE(set, ptype)					\
	extern const void * __weak_symbol __CONCAT(__start_set_, set); \
	extern const void * __weak_symbol __CONCAT(__stop_set_, set); \
	static void * __CONCAT(__xstart_set_, set);  \
	static void * __CONCAT(__xstop_set_, set);  \
	__CONCAT(__xstart_set_, set) = \
	    (ptype * const) __weak_symbol &__CONCAT(__start_set_, set);	\
	__CONCAT(__xstop_set_, set)  = \
	    (ptype * const) __weak_symbol &__CONCAT(__stop_set_, set); \
	__CONCAT(__xstart_set_, set) += sizeof (void *)

#define	SET_BEGIN(set)							\
	(__CONCAT(__xstart_set_, set))
#define	SET_LIMIT(set)							\
	(__CONCAT(__xstop_set_, set))

/*
 * Iterate over all the elements of a set.
 *
 * Sets always contain addresses of things, and "pvar" points to words
 * containing those addresses.  Thus is must be declared as "type **pvar",
 * and the address of each set item is obtained inside the loop by "*pvar".
 */
#define	SET_FOREACH(pvar, set)						\
	for (pvar = (__typeof__((pvar))) \
	    SET_BEGIN(set); pvar < (__typeof__((pvar))) SET_LIMIT(set); pvar++)

#define	SET_ITEM(set, i)						\
	((SET_BEGIN(set))[i])

/*
 * Provide a count of the items in a set.
 */
#define	SET_COUNT(set)							\
	(SET_LIMIT(set) - SET_BEGIN(set))

#endif	/* _SYS_LINKER_SET_H_ */
