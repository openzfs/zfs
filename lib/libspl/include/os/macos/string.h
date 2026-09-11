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

#ifndef _LIBSPL_OSX_STRING_H
#define	_LIBSPL_OSX_STRING_H

#include_next <string.h>

/*
 * strlcpy/strlcat are BSD extensions hidden when _POSIX_C_SOURCE is set
 * without _DARWIN_C_SOURCE (e.g. munit). Declare them explicitly so
 * spl_strlcpy can call them; if the secure headers already replaced strlcpy
 * with a macro the #ifndef skips the redundant declaration.
 */
#ifndef strlcpy
__BEGIN_DECLS
extern size_t strlcpy(char *, const char *, size_t);
extern size_t strlcat(char *, const char *, size_t);
__END_DECLS
#endif

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

#define	strerror_l(X, Y) strerror(X)

#endif /* _LIBSPL_OSX_STRING_H */
