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

#ifndef _SPL_CMN_ERR_H
#define	_SPL_CMN_ERR_H

#include <stdarg.h>
#include <sys/atomic.h>

#define	CE_CONT		0 /* continuation */
#define	CE_NOTE		1 /* notice */
#define	CE_WARN		2 /* warning */
#define	CE_PANIC	3 /* panic */
#define	CE_IGNORE	4 /* print nothing */

extern void cmn_err(int, const char *, ...)
    __attribute__((format(printf, 2, 3)));
extern void vcmn_err(int, const char *, va_list)
    __attribute__((format(printf, 2, 0)));
extern void vpanic(const char *, va_list)
    __attribute__((format(printf, 1, 0), __noreturn__));

#define	fm_panic	panic

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

#endif /* SPL_CMN_ERR_H */
