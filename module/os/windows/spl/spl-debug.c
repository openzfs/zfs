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

#include <sys/sysmacros.h>
#include <spl-debug.h>
#include <spl-trace.h>
#include <spl-ctl.h>

void panic(const char *fmt, ...) __attribute__((__noreturn__))
{
	va_list ap;
	va_start(ap, fmt);
	do {
		KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, fmt, ap));
		DbgBreakPoint();
		windows_delay(hz);
	} while (1);
	 va_end(ap);
}


/* Debug log support enabled */
