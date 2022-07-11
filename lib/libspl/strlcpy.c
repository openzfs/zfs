/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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

#ifndef HAVE_STRLCPY

#include <string.h>
#include <sys/types.h>

/*
 * Copies src to the dstsize buffer at dst. The copy will never
 * overflow the destination buffer and the buffer will always be null
 * terminated.
 */

size_t
strlcpy(char *dst, const char *src, size_t len)
{
	size_t slen = strlen(src);
	size_t copied;

	if (len == 0)
		return (slen);

	if (slen >= len)
		copied = len - 1;
	else
		copied = slen;
	(void) memcpy(dst, src, copied);
	dst[copied] = '\0';
	return (slen);
}

#endif /* HAVE_STRLCPY */
