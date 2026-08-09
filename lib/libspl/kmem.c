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
 */
/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2016 Actifio, Inc. All rights reserved.
 * Copyright (c) 2025, Klara, Inc.
 */

#include <sys/kmem.h>

char *
kmem_vasprintf(const char *fmt, va_list adx)
{
	char *buf = NULL;
	va_list adx_copy;

	va_copy(adx_copy, adx);
	VERIFY(vasprintf(&buf, fmt, adx_copy) != -1);
	va_end(adx_copy);

	return (buf);
}

char *
kmem_asprintf(const char *fmt, ...)
{
	char *buf = NULL;
	va_list adx;

	va_start(adx, fmt);
	VERIFY(vasprintf(&buf, fmt, adx) != -1);
	va_end(adx);

	return (buf);
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

fstrans_cookie_t
spl_fstrans_mark(void)
{
	return ((fstrans_cookie_t)0);
}

void
spl_fstrans_unmark(fstrans_cookie_t cookie)
{
	(void) cookie;
}

int
kmem_cache_reap_active(void)
{
	return (0);
}
