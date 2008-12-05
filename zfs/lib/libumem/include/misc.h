/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
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

/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_MISC_H
#define	_MISC_H

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <sys/types.h>
#include <sys/time.h>
#include <thread.h>
#include <pthread.h>
#include <stdarg.h>

#ifdef	__cplusplus
extern "C" {
#endif

extern uint_t umem_abort;		/* abort when errors occur */
extern uint_t umem_output;		/* output error messages to stderr */
extern caddr_t umem_min_stack;		/* max stack address for audit log */
extern caddr_t umem_max_stack;		/* min stack address for audit log */

/*
 * various utility functions
 * These are globally implemented.
 */

#undef	offsetof
#define	offsetof(s, m)	((size_t)(&(((s *)0)->m)))

/*
 * a safe printf  -- do not use for error messages.
 */
void debug_printf(const char *format, ...);

/*
 * adds a message to the log without writing it out.
 */
void log_message(const char *format, ...);

/*
 * returns the index of the (high/low) bit + 1
 */
int highbit(ulong_t);
int lowbit(ulong_t);
#pragma no_side_effect(highbit, lowbit)

/*
 * Converts a hrtime_t to a timestruc_t
 */
void hrt2ts(hrtime_t hrt, timestruc_t *tsp);

/*
 * tries to print out the symbol and offset of a pointer using umem_error_info
 */
int print_sym(void *pointer);

/*
 * Information about the current error.  Can be called multiple times, should
 * be followed eventually with a call to umem_err or umem_err_recoverable.
 */
void umem_printf(const char *format, ...);
void umem_vprintf(const char *format, va_list);

void umem_printf_warn(void *ignored, const char *format, ...);

void umem_error_enter(const char *);

/*
 * prints error message and stack trace, then aborts.  Cannot return.
 */
void umem_panic(const char *format, ...) __NORETURN;
#pragma does_not_return(umem_panic)
#pragma rarely_called(umem_panic)

/*
 * like umem_err, but only aborts if umem_abort > 0
 */
void umem_err_recoverable(const char *format, ...);

/*
 * We define our own assertion handling since libc's assert() calls malloc()
 */
#ifdef NDEBUG
#define	ASSERT(assertion) (void)0
#else
#define	ASSERT(assertion) (void)((assertion) || \
    __umem_assert_failed(#assertion, __FILE__, __LINE__))
#endif

int __umem_assert_failed(const char *assertion, const char *file, int line);
#pragma does_not_return(__umem_assert_failed)
#pragma rarely_called(__umem_assert_failed)
/*
 * These have architecture-specific implementations.
 */

/*
 * Returns the current function's frame pointer.
 */
extern void *getfp(void);

/*
 * puts a pc-only stack trace of up to pcstack_limit frames into pcstack.
 * Returns the number of stacks written.
 *
 * if check_sighandler != 0, and we are in a signal context, calls
 * umem_err_recoverable.
 */
extern int getpcstack(uintptr_t *pcstack, int pcstack_limit,
    int check_sighandler);

#ifdef	__cplusplus
}
#endif

#endif /* _MISC_H */
