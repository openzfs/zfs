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
#include <sys/atomic.h>
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
    __attribute__((format(__printf__, 2, 3)));

extern void vzcmn_err(zoneid_t, int, const char *, __va_list)
    __attribute__((format(__printf__, 3, 0)));

extern void vcmn_err(int, const char *, __va_list)
    __attribute__((format(__printf__, 2, 0)));

extern void zcmn_err(zoneid_t, int, const char *, ...)
    __attribute__((format(__printf__, 3, 4)));

extern void vzprintf(zoneid_t, const char *, __va_list)
    __attribute__((format(__printf__, 2, 0)));

extern void zprintf(zoneid_t, const char *, ...)
    __attribute__((format(__printf__, 2, 3)));

extern void vuprintf(const char *, __va_list)
    __attribute__((format(__printf__, 1, 0)));

extern void panic(const char *, ...)
    __attribute__((format(__printf__, 1, 2), __noreturn__));

#define	cmn_err_once(ce, ...)				\
do {							\
	static volatile uint32_t printed = 0;		\
	if (atomic_cas_32(&printed, 0, 1) == 0) {	\
		cmn_err(ce, __VA_ARGS__);		\
	}						\
} while (0)

#define	vcmn_err_once(ce, fmt, ap)			\
do {							\
	static volatile uint32_t printed = 0;		\
	if (atomic_cas_32(&printed, 0, 1) == 0) {	\
		vcmn_err(ce, fmt, ap);			\
	}						\
} while (0)

#define	zcmn_err_once(zone, ce, ...)			\
do {							\
	static volatile uint32_t printed = 0;		\
	if (atomic_cas_32(&printed, 0, 1) == 0) {	\
		zcmn_err(zone, ce, __VA_ARGS__);	\
	}						\
} while (0)

#define	vzcmn_err_once(zone, ce, fmt, ap)		\
do {							\
	static volatile uint32_t printed = 0;		\
	if (atomic_cas_32(&printed, 0, 1) == 0) {	\
		vzcmn_err(zone, ce, fmt, ap);		\
	}						\
} while (0)

#endif /* !_ASM */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_CMN_ERR_H */
