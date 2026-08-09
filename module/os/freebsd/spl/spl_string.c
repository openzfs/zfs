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
#include <sys/string.h>
#include <sys/kmem.h>
#include <machine/stdarg.h>

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

/*
 * Do not change the length of the returned string; it must be freed
 * with strfree().
 */
char *
kmem_asprintf(const char *fmt, ...)
{
	int size;
	va_list adx;
	char *buf;

	va_start(adx, fmt);
	size = vsnprintf(NULL, 0, fmt, adx) + 1;
	va_end(adx);

	buf = kmem_alloc(size, KM_SLEEP);

	va_start(adx, fmt);
	(void) vsnprintf(buf, size, fmt, adx);
	va_end(adx);

	return (buf);
}

void
kmem_strfree(char *str)
{
	ASSERT3P(str, !=, NULL);
	kmem_free(str, strlen(str) + 1);
}

/*
 * kmem_scnprintf() will return the number of characters that it would have
 * printed whenever it is limited by value of the size variable, rather than
 * the number of characters that it did print. This can cause misbehavior on
 * subsequent uses of the return value, so we define a safe version that will
 * return the number of characters actually printed, minus the NULL format
 * character.  Subsequent use of this by the safe string functions is safe
 * whether it is snprintf(), strlcat() or strlcpy().
 */

int
kmem_scnprintf(char *restrict str, size_t size, const char *restrict fmt, ...)
{
	int n;
	va_list ap;

	/* Make the 0 case a no-op so that we do not return -1 */
	if (size == 0)
		return (0);

	va_start(ap, fmt);
	n = vsnprintf(str, size, fmt, ap);
	va_end(ap);

	if (n >= size)
		n = size - 1;

	return (n);
}
