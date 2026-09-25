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
