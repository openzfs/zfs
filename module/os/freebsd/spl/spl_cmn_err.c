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
 *
 * $FreeBSD$
 */
/*
 * Copyright 2007 John Birrell <jb@FreeBSD.org>. All rights reserved.
 * Copyright 2012 Martin Matuska <mm@FreeBSD.org>. All rights reserved.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cmn_err.h>

void
vcmn_err(int ce, const char *fmt, va_list adx)
{
	char buf[256];
	const char *prefix;

	prefix = NULL; /* silence unwitty compilers */
	switch (ce) {
	case CE_CONT:
		prefix = "zfs(cont): ";
		break;
	case CE_NOTE:
		prefix = "zfs: NOTICE: ";
		break;
	case CE_WARN:
		prefix = "zfs: WARNING: ";
		break;
	case CE_PANIC:
		prefix = "zfs(panic): ";
		break;
	case CE_IGNORE:
		break;
	default:
		panic("zfs: unknown severity level");
	}
	if (ce == CE_PANIC) {
		vsnprintf(buf, sizeof (buf), fmt, adx);
		panic("%s%s", prefix, buf);
	}
	if (ce != CE_IGNORE) {
		printf("%s", prefix);
		vprintf(fmt, adx);
		printf("\n");
	}
}

void
cmn_err(int type, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vcmn_err(type, fmt, ap);
	va_end(ap);
}
