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

/*
 * Failure routines for libumem (not standalone)
 */

#include <sys/types.h>
#include <signal.h>
#include <stdarg.h>
#include <string.h>

#include "misc.h"

static volatile int umem_exiting = 0;
#define	UMEM_EXIT_ABORT	1

static mutex_t umem_exit_lock = DEFAULTMUTEX; /* protects umem_exiting */

static int
firstexit(int type)
{
	if (umem_exiting)
		return (0);

	(void) mutex_lock(&umem_exit_lock);
	if (umem_exiting) {
		(void) mutex_unlock(&umem_exit_lock);
		return (0);
	}
	umem_exiting = type;
	(void) mutex_unlock(&umem_exit_lock);

	return (1);
}

/*
 * We can't use abort(3C), since it closes all of the standard library
 * FILEs, which can call free().
 *
 * In addition, we can't just raise(SIGABRT), since the current handler
 * might do allocation.  We give them once chance, though.
 */
static void __NORETURN
umem_do_abort(void)
{
	if (firstexit(UMEM_EXIT_ABORT))
		(void) raise(SIGABRT);

	for (;;) {
		(void) signal(SIGABRT, SIG_DFL);
		(void) sigrelse(SIGABRT);
		(void) raise(SIGABRT);
	}
}

#define	SKIP_FRAMES		1	/* skip the panic frame */
#define	ERR_STACK_FRAMES	128

static void
print_stacktrace(void)
{
	uintptr_t cur_stack[ERR_STACK_FRAMES];

	/*
	 * if we are in a signal context, checking for it will recurse
	 */
	uint_t nframes = getpcstack(cur_stack, ERR_STACK_FRAMES, 0);
	uint_t idx;

	if (nframes > SKIP_FRAMES) {
		umem_printf("stack trace:\n");

		for (idx = SKIP_FRAMES; idx < nframes; idx++) {
			(void) print_sym((void *)cur_stack[idx]);
			umem_printf("\n");
		}
	}
}

void
umem_panic(const char *format, ...)
{
	va_list va;

	va_start(va, format);
	umem_vprintf(format, va);
	va_end(va);

	if (format[strlen(format)-1] != '\n')
		umem_error_enter("\n");

	print_stacktrace();

	umem_do_abort();
}

void
umem_err_recoverable(const char *format, ...)
{
	va_list va;

	va_start(va, format);
	umem_vprintf(format, va);
	va_end(va);

	if (format[strlen(format)-1] != '\n')
		umem_error_enter("\n");

	print_stacktrace();

	if (umem_abort > 0)
		umem_do_abort();
}

int
__umem_assert_failed(const char *assertion, const char *file, int line)
{
	umem_panic("Assertion failed: %s, file %s, line %d\n",
	    assertion, file, line);
	/*NOTREACHED*/
	return (0);
}
