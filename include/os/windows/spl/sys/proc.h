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

#ifndef _SPL_PROC_H
#define	_SPL_PROC_H

#include <sys/types.h>

typedef struct _KPROCESS proc_t;

extern proc_t p0;

#define	current_proc PsGetCurrentProcess
#define	getpid() (pid_t)(uintptr_t)PsGetProcessId(PsGetCurrentProcess())

static inline boolean_t
zfs_proc_is_caller(proc_t *p)
{
	return (p == PsGetCurrentProcess());
}

static inline char *
getcomm(void)
{
	return ("procname"); // WIN32 me
}

#endif /* SPL_PROC_H */
