/*	$NetBSD: regex2.h,v 1.15 2021/02/24 18:13:21 christos Exp $	*/

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
 *	@(#)regex2.h	8.4 (Berkeley) 3/20/94
 * $FreeBSD: head/lib/libc/regex/regex2.h 368359 2020-12-05 03:18:48Z kevans $
 */

/*
 * First, the stuff that ends up in the outside-world include file
 * typedef off_t regoff_t;
 * typedef struct {
 * 	int re_magic;
 * 	size_t re_nsub;		// number of parenthesized subexpressions
 * 	const char *re_endp;	// end pointer for REG_PEND
 * 	struct re_guts *re_g;	// none of your business :-)
 * } regex_t;
 * typedef struct {
 * 	regoff_t rm_so;		// start of match
 * 	regoff_t rm_eo;		// end of match
 * } regmatch_t;
 */
/*
 * internals of regex_t
 */
#define	MAGIC1	((('r'^0200)<<8) | 'e')

/*
 * The internal representation is a *strip*, a sequence of
 * operators ending with an endmarker.  (Some terminology etc. is a
 * historical relic of earlier versions which used multiple strips.)
 * Certain oddities in the representation are there to permit running
 * the machinery backwards; in particular, any deviation from sequential
 * flow must be marked at both its source and its destination.  Some
 * fine points:
 *
 * - OPLUS_ and O_PLUS are *inside* the loop they create.
 * - OQUEST_ and O_QUEST are *outside* the bypass they create.
 * - OCH_ and O_CH are *outside* the multi-way branch they create, while
 *   OOR1 and OOR2 are respectively the end and the beginning of one of
 *   the branches.  Note that there is an implicit OOR2 following OCH_
 *   and an implicit OOR1 preceding O_CH.
 *
 * In state representations, an operator's bit is on to signify a state
 * immediately *preceding* "execution" of that operator.
 */
typedef uint32_t sop;	/* strip operator */
typedef uint32_t sopno;
#define	OPRMASK	0xf8000000U
#define	OPDMASK	0x07ffffffU
#define	OPSHIFT	(27U)
#define	OP(n)	((n)&OPRMASK)
#define	OPND(n)	((n)&OPDMASK)
#define	SOP(op, opnd)	((op)|(opnd))
/* operators			   meaning	operand			*/
/*						(back, fwd are offsets)	*/
#define	OEND	(1U<<OPSHIFT)	/* endmarker	-			*/
#define	OCHAR	(2U<<OPSHIFT)	/* character	wide character		*/
#define	OBOL	(3U<<OPSHIFT)	/* left anchor	-			*/
#define	OEOL	(4U<<OPSHIFT)	/* right anchor	-			*/
#define	OANY	(5U<<OPSHIFT)	/* .		-			*/
#define	OANYOF	(6U<<OPSHIFT)	/* [...]	set number		*/
#define	OBACK_	(7U<<OPSHIFT)	/* begin \d	paren number		*/
#define	O_BACK	(8U<<OPSHIFT)	/* end \d	paren number		*/
#define	OPLUS_	(9U<<OPSHIFT)	/* + prefix	fwd to suffix		*/
#define	O_PLUS	(10U<<OPSHIFT)	/* + suffix	back to prefix		*/
#define	OQUEST_	(11U<<OPSHIFT)	/* ? prefix	fwd to suffix		*/
#define	O_QUEST	(12U<<OPSHIFT)	/* ? suffix	back to prefix		*/
#define	OLPAREN	(13U<<OPSHIFT)	/* (		fwd to )		*/
#define	ORPAREN	(14U<<OPSHIFT)	/* )		back to (		*/
#define	OCH_	(15U<<OPSHIFT)	/* begin choice	fwd to OOR2		*/
#define	OOR1	(16U<<OPSHIFT)	/* | pt. 1	back to OOR1 or OCH_	*/
#define	OOR2	(17U<<OPSHIFT)	/* | pt. 2	fwd to OOR2 or O_CH	*/
#define	O_CH	(18U<<OPSHIFT)	/* end choice	back to OOR1		*/
#define	OBOW	(19U<<OPSHIFT)	/* begin word	-			*/
#define	OEOW	(20U<<OPSHIFT)	/* end word	-			*/
#define	OBOS	(21U<<OPSHIFT)	/* begin subj.  -			*/
#define	OEOS	(22U<<OPSHIFT)	/* end subj.	-			*/
#define	OWBND	(23U<<OPSHIFT)	/* word bound	-			*/
#define	ONWBND	(24U<<OPSHIFT)	/* not bound	-			*/

/*
 * Structures for [] character-set representation.
 */
typedef struct {
	wint_t		min;
	wint_t		max;
} crange;
typedef struct {
	unsigned char	bmp[NC_MAX / 8];
	wctype_t	*types;
	unsigned int	ntypes;
	wint_t		*wides;
	unsigned int	nwides;
	crange		*ranges;
	unsigned int	nranges;
	int		invert;
	int		icase;
} cset;

static int
CHIN1(cset *cs, wint_t ch)
{
	unsigned int i;

	assert(ch >= 0);
	if (ch < NC)
		return (((cs->bmp[(unsigned)ch >> 3] & (1 << (ch & 7))) != 0) ^
		    cs->invert);
	for (i = 0; i < cs->nwides; i++) {
		if (cs->icase) {
			if (ch == towlower(cs->wides[i]) ||
			    ch == towupper(cs->wides[i]))
				return (!cs->invert);
		} else if (ch == cs->wides[i])
			return (!cs->invert);
	}
	for (i = 0; i < cs->nranges; i++)
		if (cs->ranges[i].min <= ch && ch <= cs->ranges[i].max)
			return (!cs->invert);
	for (i = 0; i < cs->ntypes; i++)
		if (iswctype(ch, cs->types[i]))
			return (!cs->invert);
	return (cs->invert);
}

static __inline int
CHIN(cset *cs, wint_t ch)
{

	assert(ch >= 0);
	if (ch < NC)
		return (((cs->bmp[(unsigned)ch >> 3] & (1 << (ch & 7))) != 0) ^
		    cs->invert);
	else if (cs->icase)
		return (CHIN1(cs, ch) || CHIN1(cs, towlower(ch)) ||
		    CHIN1(cs, towupper(ch)));
	else
		return (CHIN1(cs, ch));
}

/*
 * main compiled-expression structure
 */
struct re_guts {
	int magic;
#define	MAGIC2	((('R'^0200)<<8)|'E')
	sop *strip;		/* malloced area for strip */
	size_t ncsets;		/* number of csets in use */
	cset *sets;		/* -> cset [ncsets] */
	int cflags;		/* copy of regcomp() cflags argument */
	sopno nstates;		/* = number of sops */
	sopno firststate;	/* the initial OEND (normally 0) */
	sopno laststate;	/* the final OEND */
	int iflags;		/* internal flags */
#define	USEBOL	01	/* used ^ */
#define	USEEOL	02	/* used $ */
#define	BAD	04	/* something wrong */
	size_t nbol;		/* number of ^ used */
	size_t neol;		/* number of $ used */
	char *must;		/* match must contain this string */
	int moffset;		/* latest point at which must may be located */
	size_t *charjump;	/* Boyer-Moore char jump table */
	size_t *matchjump;	/* Boyer-Moore match jump table */
	size_t mlen;		/* length of must */
	size_t nsub;		/* copy of re_nsub */
	int backrefs;		/* does it use back references? */
	sopno nplus;		/* how deep does it nest +s? */
};

/* misc utilities */
#undef OUT
#define	OUT	(CHAR_MIN - 1)	/* a non-character value */
#define	IGN	(CHAR_MIN - 2)
#define	ISWORD(c)   (iswalnum((uch)(c)) || (c) == '_')
