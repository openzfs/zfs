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
 *
 * Copyright (C) 2013, 2020 Jorgen Lundman <lundman@lundman.net>
 *
 */

#include <sys/sysmacros.h>
#include <sys/cmn_err.h>
#include <sys/debug.h>

void IOSleep(int);

void
vcmn_err(int ce, const char *fmt, va_list ap)
{
	char msg[MAXMSGLEN];

	vsnprintf(msg, MAXMSGLEN - 1, fmt, ap);

	switch (ce) {
		case CE_IGNORE:
			break;
		case CE_CONT:
			printf("%s", msg);
			break;
		case CE_NOTE:
			printf("SPL: Notice: %s\n", msg);
			break;
		case CE_WARN:
			printf("SPL: Warning: %s\n", msg);
			break;
		case CE_PANIC:
			printf("PANIC: %s\n", msg);
			IOSleep(4000);
			PANIC("%s", msg);
			break;
	}
} /* vcmn_err() */

void
cmn_err(int ce, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vcmn_err(ce, fmt, ap);
	va_end(ap);
} /* cmn_err() */


int
spl_panic(const char *file, const char *func, int line, const char *fmt, ...)
{
	char msg[MAXMSGLEN];
	va_list ap;

	va_start(ap, fmt);
	(void) vsnprintf(msg, sizeof (msg), fmt, ap);
	va_end(ap);

	printf("%s", msg);
	panic("%s", msg);

	/* Unreachable */
	return (1);
}

int
spl_assertf(const char *file, const char *func, int line,
	const char *fmt, ...)
{
	char msg[MAXMSGLEN];
	va_list ap;
	int ret;

	va_start(ap, fmt);
	ret = vsnprintf(msg, sizeof (msg), fmt, ap);
	va_end(ap);

	printf("%s", msg);

	return (ret);
}
