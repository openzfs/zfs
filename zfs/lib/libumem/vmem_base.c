/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/* #pragma ident	"@(#)vmem_base.c	1.6	05/06/08 SMI" */

/* #include "mtlib.h" */
#include "config.h"
#include "vmem_base.h"
#include "umem_base.h"

uint_t vmem_backend = 0;

vmem_t *
vmem_heap_arena(vmem_alloc_t **allocp, vmem_free_t **freep)
{
	static mutex_t arena_mutex = DEFAULTMUTEX;

	/*
	 * Allow the init thread through, block others until the init completes
	 */
	if (umem_ready != UMEM_READY && umem_init_thr != thr_self() &&
	    umem_init() == 0)
		return (NULL);

	(void) mutex_lock(&arena_mutex);
	if (vmem_heap == NULL)
		vmem_heap_init();
	(void) mutex_unlock(&arena_mutex);

	if (allocp != NULL)
		*allocp = vmem_heap_alloc;
	if (freep != NULL)
		*freep = vmem_heap_free;
	return (vmem_heap);
}
