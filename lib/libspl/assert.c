/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
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
/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <assert.h>

static boolean_t libspl_assert_ok = B_FALSE;

void
libspl_set_assert_ok(boolean_t val)
{
	libspl_assert_ok = val;
}

/* printf version of libspl_assert */
void
libspl_assertf(const char *file, const char *func, int line,
    const char *format, ...)
{
	va_list args;

	va_start(args, format);
	vfprintf(stderr, format, args);
	fprintf(stderr, "\n");
	fprintf(stderr, "ASSERT at %s:%d:%s()", file, line, func);
	va_end(args);

#if !__has_feature(attribute_analyzer_noreturn) && !defined(__COVERITY__)
	if (libspl_assert_ok) {
		return;
	}
#endif
	abort();
}
