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
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */



#include <rpc/types.h>
#include <sys/kmem.h>
#include <sys/nvpair.h>

static void *
nv_alloc_sys(nv_alloc_t *nva, size_t size)
{
	return (kmem_alloc(size, (int)(uintptr_t)nva->nva_arg));
}

static void
nv_free_sys(nv_alloc_t *nva, void *buf, size_t size)
{
	(void) nva;
	kmem_free(buf, size);
}

static const nv_alloc_ops_t system_ops = {
	NULL,			/* nv_ao_init() */
	NULL,			/* nv_ao_fini() */
	nv_alloc_sys,		/* nv_ao_alloc() */
	nv_free_sys,		/* nv_ao_free() */
	NULL			/* nv_ao_reset() */
};

static nv_alloc_t nv_alloc_sleep_def = {
	&system_ops,
	(void *)KM_SLEEP
};

static nv_alloc_t nv_alloc_nosleep_def = {
	&system_ops,
	(void *)KM_NOSLEEP
};

nv_alloc_t *const nv_alloc_sleep = &nv_alloc_sleep_def;
nv_alloc_t *const nv_alloc_nosleep = &nv_alloc_nosleep_def;
