// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright (c) 2026, TrueNAS.
 */

/* Core stubs, applicable to all test suites. */

#include <stdio.h>
#include <stdarg.h>

#include <sys/types.h>
#include <sys/cmn_err.h>
#include <sys/zfs_debug.h>

#include "munit.h"
#include "unit.h"

/*
 * SET_ERROR() expands to __set_error() in debug builds. It's an
 * under-the-hood tracing aid in production; a no-op is fine.
 */
void
__set_error(const char *file, const char *func, int line, int err)
{
	(void) file; (void) func; (void) line; (void) err;
}

/* Plumb logging and debug into munit for convenience. */

/* dprintf() checks zfs_flags and calls __dprintf() in debug builds. */
int zfs_dbgmsg_enable = 1;
int zfs_flags = ZFS_DEBUG_DPRINTF;

/* Log dprintf() to MUNIT_LOG_DEBUG. */
void
__dprintf(boolean_t dprint, const char *file, const char *func,
    int line, const char *fmt, ...)
{
	char buf[1024];

	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof (buf), fmt, ap);
	va_end(ap);

	munit_logf_ex(MUNIT_LOG_DEBUG, NULL, 0, "%s%s:%d [%s]: %s",
	    dprint ? "dprintf: " : "", file, line, func, buf);
}

/* Log cmn_err() to MUNIT_LOG_INFO or WARNING, abort test on CE_PANIC. */
void
cmn_err(int ce, const char *fmt, ...)
{
	if (ce == CE_IGNORE)
		return;

	char buf[1024];

	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof (buf), fmt, ap);
	va_end(ap);

	switch (ce) {
	case CE_WARN:
		munit_logf_ex(MUNIT_LOG_WARNING, NULL, 0, "%s", buf);
		break;
	case CE_PANIC:
		munit_errorf_ex(NULL, 0, "PANIC: %s", buf);
		break;
	default:
		munit_logf_ex(MUNIT_LOG_INFO, NULL, 0, "%s", buf);
		break;
	}
}

/* helpers to generate useful random data */
uint64_t
unit_rand_uint64(void)
{
	uint64_t v =
	    (((uint64_t)munit_rand_uint32()) << 32) |
	    ((uint64_t)munit_rand_uint32());
	return (v);
}

char *
unit_rand_str(char *buf, size_t bufsz)
{
	for (int i = 0; i < bufsz-1; i++)
		buf[i] = munit_rand_int_range('a', 'z');
	buf[bufsz-1] = '\0';
	return (buf);
}
