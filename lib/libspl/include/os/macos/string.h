/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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

#ifndef _LIBSPL_OSX_STRING_H
#define	_LIBSPL_OSX_STRING_H

#include_next <string.h>

/* OsX will assert if src == dst */
static inline size_t
spl_strlcpy(char *__dst, const char *__source, size_t __size)
{
	if (__dst == __source)
		return (0);
	return (strlcpy(__dst, __source, __size));
}

#undef strlcpy
#define	strlcpy spl_strlcpy

#endif /* _LIBSPL_OSX_STRING_H */
