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
 * Copyright (C) 2017 Jorgen Lundman <lundman@lundman.net>
 *
 */

#include <sys/sysmacros.h>
#include <sys/cmn_err.h>
#include <spl-debug.h>

#include <Trace.h>

#include <zfs_gitrev.h>

void
vcmn_err(int ce, const char *fmt, va_list ap)
{
	char msg[MAXMSGLEN];

	_vsnprintf(msg, MAXMSGLEN - 1, fmt, ap);

	switch (ce) {
		case CE_IGNORE:
			break;
		case CE_CONT:
			dprintf("%s", msg);
			break;
		case CE_NOTE:
			dprintf("SPL: Notice: %s\n", msg);
			break;
		case CE_WARN:
			TraceEvent(TRACE_WARNING, "SPL: Warning: %s\n", msg);
			break;
		case CE_PANIC:
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

void
spl_panic(const char *file, const char *func, int line, const char *fmt, ...)
{
	char msg[MAXMSGLEN];
	va_list ap;

	va_start(ap, fmt);
	_vsnprintf(msg, sizeof (msg) - 1, fmt, ap);
	va_end(ap);

	/* Log to debugger output and circular buffer */
	KdPrintEx((DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
	    "OpenZFS panic at %s:%d in %s: %s\n", file, line, func, msg));
	printBuffer("OpenZFS panic at %s:%d in %s: %s\n",
	    file, line, func, msg);
	printBuffer("OpenZFS version %s\n", ZFS_META_GITREV);

	/*
	 * KeBugCheckEx is not an SEH exception; no try/except can intercept
	 * it.  This always produces a crash dump and is the only reliable
	 * way to record that a ZFS invariant was violated.
	 *
	 * Note: do NOT call KdBreakPoint() before this.  In kernel mode
	 * without an attached debugger the int 3 is dispatched through the
	 * kernel's global exception handler which produces a 0x7E bugcheck
	 * before any local __try/__except can run, regardless of clang-cl
	 * SEH frames.  The dump already contains the full call stack.
	 */
	KeBugCheckEx(0x00FFFFFF,
	    (ULONG_PTR)file,
	    (ULONG_PTR)func,
	    (ULONG_PTR)line,
	    (ULONG_PTR)msg);
}

// Backward compatible, loses FILE/FUNCTION/LINE
// but no longer used much in ZFS.
void
panic(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	spl_panic(__FILE__, __FUNCTION__, __LINE__, fmt, ap);
	va_end(ap);
}
