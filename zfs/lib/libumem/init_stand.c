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
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Initialization routines for the standalone version of libumem.
 */

#include "umem_base.h"
#include "vmem_base.h"

#include "vmem_stand.h"

void
vmem_heap_init(void)
{
	vmem_backend = VMEM_BACKEND_STAND;
	(void) vmem_stand_arena(NULL, NULL);
}

void
umem_type_init(caddr_t base, size_t len, size_t pgsize)
{
	pagesize = pgsize;

	vmem_stand_init();
	(void) vmem_stand_add(base, len);
}

int
umem_get_max_ncpus(void)
{
	return (1);
}

int
umem_add(caddr_t base, size_t len)
{
	return (vmem_stand_add(base, len));
}
