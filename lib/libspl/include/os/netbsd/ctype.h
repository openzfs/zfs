// SPDX-License-Identifier: CDDL-1.0
#ifndef	_LIBSPL_OS_NETBSD_CTYPE_H
#define	_LIBSPL_OS_NETBSD_CTYPE_H

#include_next <ctype.h>

/*
 * NetBSD's <ctype.h> macros index _C_ctype_[] with the raw argument.
 * A signed char is -Wchar-subscripts under -Werror -Wall, and a
 * negative index is undefined (C99 7.4).  Parentheses force the
 * libc function (int argument); unsigned char is the only legal
 * conversion besides EOF.
 */
#undef	isalnum
#undef	isalpha
#undef	isascii
#undef	isblank
#undef	iscntrl
#undef	isdigit
#undef	isgraph
#undef	islower
#undef	isprint
#undef	ispunct
#undef	isspace
#undef	isupper
#undef	isxdigit
#undef	tolower
#undef	toupper

#define	isalnum(c)	(isalnum)((int)(unsigned char)(c))
#define	isalpha(c)	(isalpha)((int)(unsigned char)(c))
#define	isascii(c)	(isascii)((int)(unsigned char)(c))
#define	isblank(c)	(isblank)((int)(unsigned char)(c))
#define	iscntrl(c)	(iscntrl)((int)(unsigned char)(c))
#define	isdigit(c)	(isdigit)((int)(unsigned char)(c))
#define	isgraph(c)	(isgraph)((int)(unsigned char)(c))
#define	islower(c)	(islower)((int)(unsigned char)(c))
#define	isprint(c)	(isprint)((int)(unsigned char)(c))
#define	ispunct(c)	(ispunct)((int)(unsigned char)(c))
#define	isspace(c)	(isspace)((int)(unsigned char)(c))
#define	isupper(c)	(isupper)((int)(unsigned char)(c))
#define	isxdigit(c)	(isxdigit)((int)(unsigned char)(c))
#define	tolower(c)	(tolower)((int)(unsigned char)(c))
#define	toupper(c)	(toupper)((int)(unsigned char)(c))

#endif /* _LIBSPL_OS_NETBSD_CTYPE_H */
