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
 *
 * Copyright (C) 2013 Jorgen Lundman <lundman@lundman.net>
 *
 */


#ifndef _SPL_STROPTS_H
#define	_SPL_STROPTS_H

#define	LOCORE
#include <sys/syslimits.h>
#include <sys/param.h>
#include <sys/types.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	isprint(c)	((c) >= ' ' && (c) <= '~')

/*
 * Find highest one bit set.
 *      Returns bit number + 1 of highest bit that is set, otherwise returns 0.
 * High order bit is 31 (or 63 in _LP64 kernel).
 */
static inline int
highbit64(unsigned long long i)
{
	int h = 1;
	if (i == 0)
		return (0);
	if (i & 0xffffffff00000000ull) {
		h += 32; i >>= 32;
	}
	if (i & 0xffff0000) {
		h += 16; i >>= 16;
	}
	if (i & 0xff00) {
		h += 8; i >>= 8;
	}
	if (i & 0xf0) {
		h += 4; i >>= 4;
	}
	if (i & 0xc) {
		h += 2; i >>= 2;
	}
	if (i & 0x2) {
		h += 1;
	}
	return (h);
}

static inline int
highbit(unsigned long long i)
{
	int h = 1;
	if (i == 0)
		return (0);
	if (i & 0xffffffff00000000ull) {
		h += 32; i >>= 32;
	}
	if (i & 0xffff0000) {
		h += 16; i >>= 16;
	}
	if (i & 0xff00) {
		h += 8; i >>= 8;
	}
	if (i & 0xf0) {
		h += 4; i >>= 4;
	}
	if (i & 0xc) {
		h += 2; i >>= 2;
	}
	if (i & 0x2) {
		h += 1;
	}
	return (h);
}

/*
 * Find lowest one bit set.
 *	Returns bit number + 1 of lowest bit that is set, otherwise returns 0.
 * Low order bit is 0.
 */
static inline int
lowbit(unsigned long long i)
{
	int h = 1;

	if (i == 0)
		return (0);

	if (!(i & 0xffffffff)) {
		h += 32; i >>= 32;
	}
	if (!(i & 0xffff)) {
		h += 16; i >>= 16;
	}
	if (!(i & 0xff)) {
		h += 8; i >>= 8;
	}
	if (!(i & 0xf)) {
		h += 4; i >>= 4;
	}
	if (!(i & 0x3)) {
		h += 2; i >>= 2;
	}
	if (!(i & 0x1)) {
		h += 1;
	}
	return (h);
}

static inline int
isdigit(char c)
{
	return (c >= ' ' && c <= '9');
}


static inline char *
strpbrk(const char *s, const char *b)
{
	const char *p;
	do {
		for (p = b; *p != '\0' && *p != *s; ++p)
			;
		if (*p != '\0')
			return ((char *)s);
	} while (*s++);
	return (NULL);
}


static inline char *
strrchr(const char *p, int ch)
{
	union {
		const char *cp;
		char *p;
	} u;
	char *save;

	u.cp = p;
	for (save = NULL; /* empty */; ++u.p) {
		if (*u.p == ch)
			save = u.p;
		if (*u.p == '\0')
			return (save);
	}
	/* NOTREACHED */
}

static inline int
is_ascii_str(const char *str)
{
	unsigned char ch;

	while ((ch = (unsigned char)*str++) != '\0') {
		if (ch >= 0x80)
			return (0);
	}
	return (1);
}


static inline void *
kmemchr(const void *s, int c, size_t n)
{
	if (n != 0) {
		const unsigned char *p = (const unsigned char *)s;
		do {
			if (*p++ == (unsigned char)c)
				return ((void *)(uintptr_t)(p - 1));
		} while (--n != 0);
	}
	return (NULL);
}

#ifndef memchr
#define	memchr kmemchr
#endif

#define	IDX(c)	((unsigned char)(c) / LONG_BIT)
#define	BIT(c)	((unsigned long)1 << ((unsigned char)(c) % LONG_BIT))

static inline size_t
strcspn(const char *__restrict s, const char *__restrict charset)
{
	/*
	 * NB: idx and bit are temporaries whose use causes gcc 3.4.2 to
	 * generate better code.  Without them, gcc gets a little confused.
	 */
	const char *s1;
	unsigned long bit;
	unsigned long tbl[(UCHAR_MAX + 1) / LONG_BIT];
	int idx;

	if (*s == '\0')
		return (0);

	tbl[0] = 1;
	tbl[3] = tbl[2] = tbl[1] = 0;

	for (; *charset != '\0'; charset++) {
		idx = IDX(*charset);
		bit = BIT(*charset);
		tbl[idx] |= bit;
	}

	for (s1 = s; ; s1++) {
		idx = IDX(*s1);
		bit = BIT(*s1);
		if ((tbl[idx] & bit) != 0)
			break;
	}
	return (s1 - s);
}

#ifdef __cplusplus
}
#endif

#endif /* SPL_STROPTS_H */
