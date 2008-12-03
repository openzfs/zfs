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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Initialization routines for the library version of libumem.
 */

#include "umem_base.h"
#include "vmem_base.h"
#include <unistd.h>
#include <dlfcn.h>

void
vmem_heap_init(void)
{
	void *handle = dlopen("libmapmalloc.so.1", RTLD_NOLOAD);

	if (handle != NULL) {
		log_message("sbrk backend disabled\n");
		vmem_backend = VMEM_BACKEND_MMAP;
	}

	if ((vmem_backend & VMEM_BACKEND_MMAP) != 0) {
		vmem_backend = VMEM_BACKEND_MMAP;
		(void) vmem_mmap_arena(NULL, NULL);
	} else {
		vmem_backend = VMEM_BACKEND_SBRK;
		(void) vmem_sbrk_arena(NULL, NULL);
	}
}

/*ARGSUSED*/
void
umem_type_init(caddr_t start, size_t len, size_t pgsize)
{
	pagesize = _sysconf(_SC_PAGESIZE);
}

int
umem_get_max_ncpus(void)
{
	if (thr_main() != -1)
		return (2 * sysconf(_SC_NPROCESSORS_ONLN));
	else
		return (1);
}
