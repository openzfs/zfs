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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_U8_TEXTPREP_H
#define	_SYS_U8_TEXTPREP_H



#include <sys/isa_defs.h>
#include <sys/types.h>
#include <sys/errno.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * UTF-8 text preparation functions and their macros.
 *
 * Among the macros defined, U8_CANON_DECOMP, U8_COMPAT_DECOMP, and
 * U8_CANON_COMP are not public interfaces and must not be used directly
 * at the flag input argument.
 */
#define	U8_STRCMP_CS			(0x00000001)
#define	U8_STRCMP_CI_UPPER		(0x00000002)
#if 0
#define	U8_STRCMP_CI_LOWER		(0x00000004)
#endif

#define	U8_CANON_DECOMP			(0x00000010)
#define	U8_COMPAT_DECOMP		(0x00000020)
#define	U8_CANON_COMP			(0x00000040)

#define	U8_STRCMP_NFD			(U8_CANON_DECOMP)
#define	U8_STRCMP_NFC			(U8_CANON_DECOMP | U8_CANON_COMP)
#define	U8_STRCMP_NFKD			(U8_COMPAT_DECOMP)
#define	U8_STRCMP_NFKC			(U8_COMPAT_DECOMP | U8_CANON_COMP)

#define	U8_TEXTPREP_TOUPPER		(U8_STRCMP_CI_UPPER)
#ifdef U8_STRCMP_CI_LOWER
#define	U8_TEXTPREP_TOLOWER		(U8_STRCMP_CI_LOWER)
#endif

#define	U8_TEXTPREP_NFD			(U8_STRCMP_NFD)
#define	U8_TEXTPREP_NFC			(U8_STRCMP_NFC)
#define	U8_TEXTPREP_NFKD		(U8_STRCMP_NFKD)
#define	U8_TEXTPREP_NFKC		(U8_STRCMP_NFKC)

#define	U8_TEXTPREP_IGNORE_NULL		(0x00010000)
#define	U8_TEXTPREP_IGNORE_INVALID	(0x00020000)
#define	U8_TEXTPREP_NOWAIT		(0x00040000)

#if 0
#define	U8_UNICODE_320			(0)
#define	U8_UNICODE_500			(1)
#else
#define	U8_UNICODE_500			(0)
#endif
#define	U8_UNICODE_LATEST		(U8_UNICODE_500)

#define	U8_VALIDATE_ENTIRE		(0x00100000)
#define	U8_VALIDATE_CHECK_ADDITIONAL	(0x00200000)
#define	U8_VALIDATE_UCS2_RANGE		(0x00400000)

#define	U8_ILLEGAL_CHAR			(-1)
#define	U8_OUT_OF_RANGE_CHAR		(-2)

extern int u8_validate(const char *, size_t, char **, int, int *);
extern int u8_strcmp(const char *, const char *, size_t, int, size_t, int *);
extern size_t u8_textprep_str(char *, size_t *, char *, size_t *, int, size_t,
	int *);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_U8_TEXTPREP_H */
