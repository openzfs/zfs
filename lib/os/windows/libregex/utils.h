/*	$NetBSD: utils.h,v 1.9 2021/04/22 19:20:24 christos Exp $	*/

/*
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1992, 1993, 1994 Henry Spencer.
 * Copyright (c) 1992, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Henry Spencer.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)utils.h	8.3 (Berkeley) 3/20/94
 * $FreeBSD: head/lib/libc/regex/utils.h 341838 2018-12-12 04:23:00Z yuripv $
 */

#ifdef NLS
#include <wchar.h>
#include <wctype.h>
#else
#include <ctype.h>
#define	wint_t regex_wint_t
#define	mbstate_t regex_mbstate_t
#define	wctype_t regex_wctype_t
typedef short wint_t;
typedef char mbstate_t;
typedef short wctype_t;
// #define iswupper(a) isupper(a)
// #define iswlower(a) islower(a)
// #define iswalpha(a) isalpha(a)
// #define iswalnum(a) isalnum(a)
#define	towupper(a) toupper(a)
#define	towlower(a) tolower(a)
extern wctype_t __regex_wctype(const char *);
extern int __regex_iswctype(wint_t, wctype_t);
// #define wctype(s) __regex_wctype(s)
// #define iswctype(c, t) __regex_iswctype((c), (t))
#endif

/* utility definitions */
// Windows defines a float INFINITY
#define	DUPMAX		 (~0ULL) 	/* xxx is this right? */
#define	REGINFINITY	(DUPMAX + 1)

#define	NC_MAX		(CHAR_MAX - CHAR_MIN + 1)
#define	NC		((MB_CUR_MAX) == 1 ? (NC_MAX) : (128))
typedef unsigned char uch;

/* switch off assertions (if not already off) if no REDEBUG */
#ifndef REDEBUG
#ifndef NDEBUG
#define	NDEBUG	/* no assertions please */
#endif
#endif
#include <assert.h>

/* for old systems with bcopy() but no memmove() */
#ifdef USEBCOPY
#define	memmove(d, s, c)	bcopy(s, d, c)
#endif
