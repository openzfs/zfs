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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _LIBSPL_SYS_CMN_ERR_H
#define	_LIBSPL_SYS_CMN_ERR_H

#include <atomic.h>

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

/*
 * Note that we are not using the debugging levels.
 */

#define	CE_CONT		0	/* continuation		*/
#define	CE_NOTE		1	/* notice		*/
#define	CE_WARN		2	/* warning		*/
#define	CE_PANIC	3	/* panic		*/
#define	CE_IGNORE	4	/* print nothing	*/

/*
 * ZFS debugging
 */

extern void dprintf_setup(int *argc, char **argv);

extern void cmn_err(int, const char *, ...)
    __attribute__((format(printf, 2, 3)));
extern void vcmn_err(int, const char *, va_list)
    __attribute__((format(printf, 2, 0)));
extern void panic(const char *, ...)
    __attribute__((format(printf, 1, 2), noreturn));
extern void vpanic(const char *, va_list)
    __attribute__((format(printf, 1, 0), noreturn));

#define	fm_panic	panic

#endif
