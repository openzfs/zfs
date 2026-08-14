// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 *
 * $FreeBSD$
 */
/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/limits.h>
#include <sys/systm.h>
#include <sys/string.h>
#include <sys/kmem.h>
#include <sys/stdarg.h>

#define	IS_DIGIT(c)	((c) >= '0' && (c) <= '9')

#define	IS_ALPHA(c)	\
	(((c) >= 'a' && (c) <= 'z') || ((c) >= 'A' && (c) <= 'Z'))

char *
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

/*
 * Convert a string into a valid C identifier by replacing invalid
 * characters with '_'.  Also makes sure the string is nul-terminated
 * and takes up at most n bytes.
 */
void
strident_canon(char *s, size_t n)
{
	char c;
	char *end = s + n - 1;

	if ((c = *s) == 0)
		return;

	if (!IS_ALPHA(c) && c != '_')
		*s = '_';

	while (s < end && ((c = *(++s)) != 0)) {
		if (!IS_ALPHA(c) && !IS_DIGIT(c) && c != '_')
			*s = '_';
	}
	*s = 0;
}

void
kmem_strfree(char *str)
{
	ASSERT3P(str, !=, NULL);
	kmem_free(str, strlen(str) + 1);
}

/*
 * Why these exist at all:
 *
 * sscanf(3) and sprintf(3) are libc.  NetBSD GENERIC does not contain
 * either — subr_prf.c exports printf/snprintf/vsnprintf/vasprintf, and
 * libkern has strtoll/strtoull, but a grep of sys/ finds zero sscanf
 * symbols.  Kernel modules cannot link libc, so the two signatures
 * OpenZFS kernel code actually calls have to live here.
 *
 * sprintf is not dead.  spa_taskq_param_get() formats into a
 * caller buffer through it.  The in-place vdev replace used to
 * as well (`"%s/old"`); that site moved to kmem_asprintf
 * because this sprintf passed a wrapping length (see below).
 * Dropping sprintf would still fail the link.
 *
 * sscanf has one call site, zcp.c:
 *     sscanf(key, "%lld%n", &tmp, &parselen)
 * Implement that format via libkern strtoll so leading isspace(),
 * sign, and saturating overflow match libc sscanf / Linux / FreeBSD.
 * A hand-rolled digit loop that skipped neither whitespace nor
 * overflow made `" 5"` a non-colliding Lua key on NetBSD only.
 */
int
sprintf(char *buf, const char *fmt, ...)
{
	va_list ap;
	uintptr_t room;
	int n;

	/*
	 * NetBSD vsnprintf (subr_prf.c) sets its end pointer to
	 * buf+size and writes only while the cursor is below it.
	 * Kernel VA is above 0xffff800000000000, so (size_t)-1/2
	 * wraps that sum below buf and the call stores nothing,
	 * then plants a NUL at the would-be length.  The in-place
	 * replace buffer kept the old path: alloc strlen+5 (21),
	 * spa_strfree strlen+1 (17).  Cap at the rest of the
	 * address space, and at INT_MAX so the return stays an int.
	 */
	room = (uintptr_t)-1 - (uintptr_t)buf;
	if (room > (uintptr_t)INT_MAX)
		room = (uintptr_t)INT_MAX;
	if (room == 0)
		room = 1;

	va_start(ap, fmt);
	n = vsnprintf(buf, (size_t)room, fmt, ap);
	va_end(ap);
	return (n);
}

int
sscanf(const char *str, const char *fmt, ...)
{
	va_list ap;
	long long *val;
	int *consumed;
	char *end;

	if (strcmp(fmt, "%lld%n") != 0)
		return (0);

	va_start(ap, fmt);
	val = va_arg(ap, long long *);
	consumed = va_arg(ap, int *);
	va_end(ap);

	end = NULL;
	{
		const char *p = str;

		while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' ||
		    *p == '\f' || *p == '\v')
			p++;
		if (*p == '+' || *p == '-')
			p++;
		if (*p < '0' || *p > '9')
			return (0);
	}
	*val = strtoll(str, &end, 10);
	if (end == NULL || end == str)
		return (0);
	*consumed = (int)(end - str);
	return (1);
}
