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
 *  Solaris Porting Layer (SPL) Atomic Implementation.
 */

/*
 *
 * Copyright (C) 2013 Jorgen Lundman <lundman@lundman.net>
 *
 */

#include <sys/atomic.h>
#include <sys/kernel.h>
#include <libkern/OSAtomic.h>


#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>

void *
atomic_cas_ptr(volatile void *target, void *cmp, void *new)
{
#ifdef __LP64__
	return (void *)__sync_val_compare_and_swap((uint64_t *)target,
	    (uint64_t)cmp, (uint64_t)new);
#else
	return (void *)__sync_val_compare_and_swap((uint32_t *)target, cmp,
	    new);
#endif
}
