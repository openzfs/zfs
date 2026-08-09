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

#include <sys/nvpair.h>
#include <sys/kmem.h>
#include <sys/vmem.h>

static void *
nv_alloc_sleep_spl(nv_alloc_t *nva, size_t size)
{
	return (vmem_alloc(size, KM_SLEEP));
}

static void *
nv_alloc_pushpage_spl(nv_alloc_t *nva, size_t size)
{
	return (vmem_alloc(size, KM_PUSHPAGE));
}

static void *
nv_alloc_nosleep_spl(nv_alloc_t *nva, size_t size)
{
	return (kmem_alloc(size, KM_NOSLEEP));
}

static void
nv_free_spl(nv_alloc_t *nva, void *buf, size_t size)
{
	kmem_free(buf, size);
}

static const nv_alloc_ops_t spl_sleep_ops_def = {
	.nv_ao_init = NULL,
	.nv_ao_fini = NULL,
	.nv_ao_alloc = nv_alloc_sleep_spl,
	.nv_ao_free = nv_free_spl,
	.nv_ao_reset = NULL
};

static const nv_alloc_ops_t spl_pushpage_ops_def = {
	.nv_ao_init = NULL,
	.nv_ao_fini = NULL,
	.nv_ao_alloc = nv_alloc_pushpage_spl,
	.nv_ao_free = nv_free_spl,
	.nv_ao_reset = NULL
};

static const nv_alloc_ops_t spl_nosleep_ops_def = {
	.nv_ao_init = NULL,
	.nv_ao_fini = NULL,
	.nv_ao_alloc = nv_alloc_nosleep_spl,
	.nv_ao_free = nv_free_spl,
	.nv_ao_reset = NULL
};

static nv_alloc_t nv_alloc_sleep_def = {
	&spl_sleep_ops_def,
	NULL
};

static nv_alloc_t nv_alloc_pushpage_def = {
	&spl_pushpage_ops_def,
	NULL
};

static nv_alloc_t nv_alloc_nosleep_def = {
	&spl_nosleep_ops_def,
	NULL
};

nv_alloc_t *const nv_alloc_sleep = &nv_alloc_sleep_def;
nv_alloc_t *const nv_alloc_pushpage = &nv_alloc_pushpage_def;
nv_alloc_t *const nv_alloc_nosleep = &nv_alloc_nosleep_def;
