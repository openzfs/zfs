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
 * ***************************************************************************
 *  Solaris Porting Layer (SPL) Atomic Implementation.
 * ***************************************************************************
 */

/*
 *
 * Copyright (C) 2017 Jorgen Lundman <lundman@lundman.net>
 *
 */

#include <sys/atomic.h>
#include <sys/param.h>
#include <sys/mutex.h>


#ifdef _KERNEL

void *
atomic_cas_ptr(volatile void *_target, void *_cmp, void *_new)
{
	return (InterlockedCompareExchangePointer((volatile PVOID *)_target,
	    _new, _cmp));
}

#endif
