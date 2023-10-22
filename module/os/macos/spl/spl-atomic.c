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
