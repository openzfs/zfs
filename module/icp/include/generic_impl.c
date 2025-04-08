// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * Copyright (c) 2003, 2010 Oracle and/or its affiliates.
 * Copyright (c) 2022 Tino Reichardt <milky-zfs@mcmilk.de>
 */

/*
 * This file gets included by c files for implementing the full set
 * of zfs_impl.h defines.
 *
 * It's ment for easier maintaining multiple implementations of
 * algorithms. Look into blake3_impl.c, sha256_impl.c or sha512_impl.c
 * for reference.
 */

#include <sys/zfs_context.h>
#include <sys/zio_checksum.h>
#include <sys/zfs_impl.h>

/* Two default implementations */
#define	IMPL_FASTEST	(UINT32_MAX)
#define	IMPL_CYCLE	(UINT32_MAX - 1)

#define	IMPL_READ(i)	(*(volatile uint32_t *) &(i))

/* Implementation that contains the fastest method */
static IMPL_OPS_T generic_fastest_impl = {
	.name = "fastest"
};

/* Hold all supported implementations */
static const IMPL_OPS_T *generic_supp_impls[ARRAY_SIZE(IMPL_ARRAY)];
static uint32_t generic_supp_impls_cnt = 0;

/* Currently selected implementation */
static uint32_t generic_impl_chosen = IMPL_FASTEST;

static struct generic_impl_selector {
	const char *name;
	uint32_t sel;
} generic_impl_selectors[] = {
	{ "cycle",	IMPL_CYCLE },
	{ "fastest",	IMPL_FASTEST }
};

/* check the supported implementations */
static void
generic_impl_init(void)
{
	int i, c;

	/* init only once */
	if (likely(generic_supp_impls_cnt != 0))
		return;

	/* Move supported implementations into generic_supp_impls */
	for (i = 0, c = 0; i < ARRAY_SIZE(IMPL_ARRAY); i++) {
		const IMPL_OPS_T *impl = IMPL_ARRAY[i];

		if (impl->is_supported && impl->is_supported())
			generic_supp_impls[c++] = impl;
	}
	generic_supp_impls_cnt = c;

	/* first init generic impl, may be changed via set_fastest() */
	memcpy(&generic_fastest_impl, generic_supp_impls[0],
	    sizeof (generic_fastest_impl));
}

/* get number of supported implementations */
static uint32_t
generic_impl_getcnt(void)
{
	generic_impl_init();
	return (generic_supp_impls_cnt);
}

/* get id of selected implementation */
static uint32_t
generic_impl_getid(void)
{
	generic_impl_init();
	return (IMPL_READ(generic_impl_chosen));
}

/* get name of selected implementation */
static const char *
generic_impl_getname(void)
{
	uint32_t impl = IMPL_READ(generic_impl_chosen);

	generic_impl_init();
	switch (impl) {
	case IMPL_FASTEST:
		return ("fastest");
	case IMPL_CYCLE:
		return ("cycle");
	default:
		return (generic_supp_impls[impl]->name);
	}
}

/* set implementation by id */
static void
generic_impl_setid(uint32_t id)
{
	generic_impl_init();
	switch (id) {
	case IMPL_FASTEST:
		atomic_swap_32(&generic_impl_chosen, IMPL_FASTEST);
		break;
	case IMPL_CYCLE:
		atomic_swap_32(&generic_impl_chosen, IMPL_CYCLE);
		break;
	default:
		ASSERT3U(id, <, generic_supp_impls_cnt);
		atomic_swap_32(&generic_impl_chosen, id);
		break;
	}
}

/* set implementation by name */
static int
generic_impl_setname(const char *val)
{
	uint32_t impl = IMPL_READ(generic_impl_chosen);
	size_t val_len;
	int i, err = -EINVAL;

	generic_impl_init();
	val_len = strlen(val);
	while ((val_len > 0) && !!isspace(val[val_len-1])) /* trim '\n' */
		val_len--;

	/* check mandatory implementations */
	for (i = 0; i < ARRAY_SIZE(generic_impl_selectors); i++) {
		const char *name = generic_impl_selectors[i].name;

		if (val_len == strlen(name) &&
		    strncmp(val, name, val_len) == 0) {
			impl = generic_impl_selectors[i].sel;
			err = 0;
			break;
		}
	}

	/* check all supported implementations */
	if (err != 0) {
		for (i = 0; i < generic_supp_impls_cnt; i++) {
			const char *name = generic_supp_impls[i]->name;

			if (val_len == strlen(name) &&
			    strncmp(val, name, val_len) == 0) {
				impl = i;
				err = 0;
				break;
			}
		}
	}

	if (err == 0) {
		atomic_swap_32(&generic_impl_chosen, impl);
	}

	return (err);
}

/* setup id as fastest implementation */
static void
generic_impl_set_fastest(uint32_t id)
{
	generic_impl_init();
	memcpy(&generic_fastest_impl, generic_supp_impls[id],
	    sizeof (generic_fastest_impl));
}

/* return impl iterating functions */
const zfs_impl_t ZFS_IMPL_OPS = {
	.name = IMPL_NAME,
	.getcnt = generic_impl_getcnt,
	.getid = generic_impl_getid,
	.getname = generic_impl_getname,
	.set_fastest = generic_impl_set_fastest,
	.setid = generic_impl_setid,
	.setname = generic_impl_setname
};

/* get impl ops_t of selected implementation */
const IMPL_OPS_T *
IMPL_GET_OPS(void)
{
	const IMPL_OPS_T *ops = NULL;
	uint32_t idx, impl = IMPL_READ(generic_impl_chosen);
	static uint32_t cycle_count = 0;

	generic_impl_init();
	switch (impl) {
	case IMPL_FASTEST:
		ops = &generic_fastest_impl;
		break;
	case IMPL_CYCLE:
		idx = (++cycle_count) % generic_supp_impls_cnt;
		ops = generic_supp_impls[idx];
		break;
	default:
		ASSERT3U(impl, <, generic_supp_impls_cnt);
		ops = generic_supp_impls[impl];
		break;
	}

	ASSERT3P(ops, !=, NULL);
	return (ops);
}
