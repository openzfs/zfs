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
/* Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T */
/* All Rights Reserved */


/*
 * Copyright 2004 Sun Microsystems, Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright 2012 Nexenta Systems, Inc. All rights reserved.
 */

#ifndef _SPL_CMN_ERR_H
#define	_SPL_CMN_ERR_H

#include <stdarg.h>
#include <sys/varargs.h>
#include <sys/atomic.h>

#define	CE_CONT		0 /* continuation	*/
#define	CE_NOTE		1 /* notice		*/
#define	CE_WARN		2 /* warning		*/
#define	CE_PANIC	3 /* panic		*/
#define	CE_IGNORE	4 /* print nothing	*/

#ifdef _KERNEL

extern void vcmn_err(int, const char *, __va_list);
extern void cmn_err(int, const char *, ...);

#define	cmn_err_once(ce, ...)				\
{							\
	static volatile uint32_t printed = 0;		\
	if (atomic_cas_32(&printed, 0, 1) == 0) {	\
		cmn_err(ce, __VA_ARGS__);		\
	}						\
}

#define	vcmn_err_once(ce, fmt, ap)			\
{							\
	static volatile uint32_t printed = 0;		\
	if (atomic_cas_32(&printed, 0, 1) == 0) {	\
		vcmn_err(ce, fmt, ap);			\
	}						\
}

#define	zcmn_err_once(zone, ce, ...)			\
{							\
	static volatile uint32_t printed = 0;		\
	if (atomic_cas_32(&printed, 0, 1) == 0) {	\
		zcmn_err(zone, ce, __VA_ARGS__);	\
	}						\
}

#define	vzcmn_err_once(zone, ce, fmt, ap)		\
{							\
	static volatile uint32_t printed = 0;		\
	if (atomic_cas_32(&printed, 0, 1) == 0) {	\
		vzcmn_err(zone, ce, fmt, ap);		\
	}						\
}

#endif /* _KERNEL */

#define	fm_panic	panic

#endif /* SPL_CMN_ERR_H */
