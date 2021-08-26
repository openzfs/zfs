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

#include <assert.h>
#include <pthread.h>

int libspl_assert_ok = 0;

libspl_abort_handler_fn_t libspl_alternative_abort_handler = NULL;
void *libspl_alternative_abort_handler_arg;

void
libspl_set_alternative_abort_handler(libspl_abort_handler_fn_t h, void *arg)
{
	/* FIXME synchronization */
	libspl_alternative_abort_handler_arg = arg;
	libspl_alternative_abort_handler = h;
}

/* printf version of libspl_assert */
void
libspl_assertf(const char *file, const char *func, int line,
    const char *format, ...)
{
	void *h_arg = libspl_alternative_abort_handler_arg;
	libspl_abort_handler_fn_t h = libspl_alternative_abort_handler;

	va_list args;
	va_list args_h;
	va_start(args, format);
	if (h != NULL) {
		va_copy(args_h, args);
		char *firstline;
		int err = vasprintf(&firstline, format, args_h);
		if (err == -1)
			goto nohandler;
		char *all;
		err = asprintf(&all, "%s\nASSERT at %s:%d:%s()", firstline, file, line, func);
		if (err == -1)
			goto nohandler;
		h(h_arg, all);
		abort(); /* unrechable */
	}
nohandler:
	vfprintf(stderr, format, args);
	fprintf(stderr, "\n");
	fprintf(stderr, "ASSERT at %s:%d:%s()", file, line, func);
	va_end(args);
	if (libspl_assert_ok) {
		return;
	}
	abort();
}
