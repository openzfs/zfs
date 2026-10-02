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

#ifndef _SPL_PROC_H
#define	_SPL_PROC_H

#include <sys/ucred.h>
#include <i386/locks.h>
#include_next <sys/proc.h>
#include <sys/kernel_types.h>
#include <sys/vnode.h>

#define	proc_t struct proc

extern proc_t p0; /* process 0 */

static inline boolean_t
zfs_proc_is_caller(proc_t *p)
{
	return (p == (struct proc *)current_proc());
}

#endif /* SPL_PROC_H */
