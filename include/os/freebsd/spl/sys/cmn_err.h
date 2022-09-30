/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/


/*
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SYS_CMN_ERR_H
#define	_SYS_CMN_ERR_H

#if !defined(_ASM)
#include <sys/_stdarg.h>
#endif

#ifdef	__cplusplus
extern "C" {
#endif

/* Common error handling severity levels */

#define	CE_CONT		0	/* continuation		*/
#define	CE_NOTE		1	/* notice		*/
#define	CE_WARN		2	/* warning		*/
#define	CE_PANIC	3	/* panic		*/
#define	CE_IGNORE	4	/* print nothing	*/

#ifndef _ASM

extern void cmn_err(int, const char *, ...)
    __attribute__((format(printf, 2, 3)));

extern void vzcmn_err(zoneid_t, int, const char *, __va_list)
    __attribute__((format(printf, 3, 0)));

extern void vcmn_err(int, const char *, __va_list)
    __attribute__((format(printf, 2, 0)));

extern void zcmn_err(zoneid_t, int, const char *, ...)
    __attribute__((format(printf, 3, 4)));

extern void vzprintf(zoneid_t, const char *, __va_list)
    __attribute__((format(printf, 2, 0)));

extern void zprintf(zoneid_t, const char *, ...)
    __attribute__((format(printf, 2, 3)));

extern void vuprintf(const char *, __va_list)
    __attribute__((format(printf, 1, 0)));

extern void panic(const char *, ...)
    __attribute__((format(printf, 1, 2), __noreturn__));

#endif /* !_ASM */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_CMN_ERR_H */
