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
	return (p == curproc);
}

#endif /* SPL_PROC_H */
