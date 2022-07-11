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
 * Copyright (c) 2021-2022 Tino Reichardt <milky-zfs@mcmilk.de>
 */

#include <sys/zfs_context.h>
#include <sys/zio_checksum.h>

#include "blake3_impl.h"

static const blake3_impl_ops_t *const blake3_impls[] = {
	&blake3_generic_impl,
#if defined(__aarch64__) || \
	(defined(__x86_64) && defined(HAVE_SSE2)) || \
	(defined(__PPC64__) && defined(__LITTLE_ENDIAN__))
	&blake3_sse2_impl,
#endif
#if defined(__aarch64__) || \
	(defined(__x86_64) && defined(HAVE_SSE4_1)) || \
	(defined(__PPC64__) && defined(__LITTLE_ENDIAN__))
	&blake3_sse41_impl,
#endif
#if defined(__x86_64) && defined(HAVE_SSE4_1) && defined(HAVE_AVX2)
	&blake3_avx2_impl,
#endif
#if defined(__x86_64) && defined(HAVE_AVX512F) && defined(HAVE_AVX512VL)
	&blake3_avx512_impl,
#endif
};

/* this pointer holds current ops for implementation */
static const blake3_impl_ops_t *blake3_selected_impl = &blake3_generic_impl;

/* special implementation selections */
#define	IMPL_FASTEST	(UINT32_MAX)
#define	IMPL_CYCLE	(UINT32_MAX-1)
#define	IMPL_USER	(UINT32_MAX-2)
#define	IMPL_PARAM	(UINT32_MAX-3)

#define	IMPL_READ(i) (*(volatile uint32_t *) &(i))
static uint32_t icp_blake3_impl = IMPL_FASTEST;

#define	BLAKE3_IMPL_NAME_MAX	16

/* id of fastest implementation */
static uint32_t blake3_fastest_id = 0;

/* currently used id */
static uint32_t blake3_current_id = 0;

/* id of module parameter (-1 == unused) */
static int blake3_param_id = -1;

/* return number of supported implementations */
int
blake3_get_impl_count(void)
{
	static int impls = 0;
	int i;

	if (impls)
		return (impls);

	for (i = 0; i < ARRAY_SIZE(blake3_impls); i++) {
		if (!blake3_impls[i]->is_supported()) continue;
		impls++;
	}

	return (impls);
}

/* return id of selected implementation */
int
blake3_get_impl_id(void)
{
	return (blake3_current_id);
}

/* return name of selected implementation */
const char *
blake3_get_impl_name(void)
{
	return (blake3_selected_impl->name);
}

/* setup id as fastest implementation */
void
blake3_set_impl_fastest(uint32_t id)
{
	blake3_fastest_id = id;
}

/* set implementation by id */
void
blake3_set_impl_id(uint32_t id)
{
	int i, cid;

	/* select fastest */
	if (id == IMPL_FASTEST)
		id = blake3_fastest_id;

	/* select next or first */
	if (id == IMPL_CYCLE)
		id = (++blake3_current_id) % blake3_get_impl_count();

	/* 0..N for the real impl */
	for (i = 0, cid = 0; i < ARRAY_SIZE(blake3_impls); i++) {
		if (!blake3_impls[i]->is_supported()) continue;
		if (cid == id) {
			blake3_current_id = cid;
			blake3_selected_impl = blake3_impls[i];
			return;
		}
		cid++;
	}
}

/* set implementation by name */
int
blake3_set_impl_name(const char *name)
{
	int i, cid;

	if (strcmp(name, "fastest") == 0) {
		atomic_swap_32(&icp_blake3_impl, IMPL_FASTEST);
		blake3_set_impl_id(IMPL_FASTEST);
		return (0);
	} else if (strcmp(name, "cycle") == 0) {
		atomic_swap_32(&icp_blake3_impl, IMPL_CYCLE);
		blake3_set_impl_id(IMPL_CYCLE);
		return (0);
	}

	for (i = 0, cid = 0; i < ARRAY_SIZE(blake3_impls); i++) {
		if (!blake3_impls[i]->is_supported()) continue;
		if (strcmp(name, blake3_impls[i]->name) == 0) {
			if (icp_blake3_impl == IMPL_PARAM) {
				blake3_param_id = cid;
				return (0);
			}
			blake3_selected_impl = blake3_impls[i];
			blake3_current_id = cid;
			return (0);
		}
		cid++;
	}

	return (-EINVAL);
}

/* setup implementation */
void
blake3_setup_impl(void)
{
	switch (IMPL_READ(icp_blake3_impl)) {
	case IMPL_PARAM:
		blake3_set_impl_id(blake3_param_id);
		atomic_swap_32(&icp_blake3_impl, IMPL_USER);
		break;
	case IMPL_FASTEST:
		blake3_set_impl_id(IMPL_FASTEST);
		break;
	case IMPL_CYCLE:
		blake3_set_impl_id(IMPL_CYCLE);
		break;
	default:
		blake3_set_impl_id(blake3_current_id);
		break;
	}
}

/* return selected implementation */
const blake3_impl_ops_t *
blake3_impl_get_ops(void)
{
	/* each call to ops will cycle */
	if (icp_blake3_impl == IMPL_CYCLE)
		blake3_set_impl_id(IMPL_CYCLE);

	return (blake3_selected_impl);
}

#if defined(_KERNEL)
void **blake3_per_cpu_ctx;

void
blake3_per_cpu_ctx_init(void)
{
	/*
	 * Create "The Godfather" ptr to hold all blake3 ctx
	 */
	blake3_per_cpu_ctx = kmem_alloc(max_ncpus * sizeof (void *), KM_SLEEP);
	for (int i = 0; i < max_ncpus; i++) {
		blake3_per_cpu_ctx[i] = kmem_alloc(sizeof (BLAKE3_CTX),
		    KM_SLEEP);
	}
}

void
blake3_per_cpu_ctx_fini(void)
{
	for (int i = 0; i < max_ncpus; i++) {
		memset(blake3_per_cpu_ctx[i], 0, sizeof (BLAKE3_CTX));
		kmem_free(blake3_per_cpu_ctx[i], sizeof (BLAKE3_CTX));
	}
	memset(blake3_per_cpu_ctx, 0, max_ncpus * sizeof (void *));
	kmem_free(blake3_per_cpu_ctx, max_ncpus * sizeof (void *));
}
#endif

#if defined(_KERNEL) && defined(__linux__)
static int
icp_blake3_impl_set(const char *name, zfs_kernel_param_t *kp)
{
	char req_name[BLAKE3_IMPL_NAME_MAX];
	size_t i;

	/* sanitize input */
	i = strnlen(name, BLAKE3_IMPL_NAME_MAX);
	if (i == 0 || i >= BLAKE3_IMPL_NAME_MAX)
		return (-EINVAL);

	strlcpy(req_name, name, BLAKE3_IMPL_NAME_MAX);
	while (i > 0 && isspace(req_name[i-1]))
		i--;
	req_name[i] = '\0';

	atomic_swap_32(&icp_blake3_impl, IMPL_PARAM);
	return (blake3_set_impl_name(req_name));
}

static int
icp_blake3_impl_get(char *buffer, zfs_kernel_param_t *kp)
{
	int i, cid, cnt = 0;
	char *fmt;

	/* cycling */
	fmt = (icp_blake3_impl == IMPL_CYCLE) ? "[cycle] " : "cycle ";
	cnt += sprintf(buffer + cnt, fmt);

	/* fastest one */
	fmt = (icp_blake3_impl == IMPL_FASTEST) ? "[fastest] " : "fastest ";
	cnt += sprintf(buffer + cnt, fmt);

	/* user selected */
	for (i = 0, cid = 0; i < ARRAY_SIZE(blake3_impls); i++) {
		if (!blake3_impls[i]->is_supported()) continue;
		fmt = (icp_blake3_impl == IMPL_USER &&
		    cid == blake3_current_id) ? "[%s] " : "%s ";
		cnt += sprintf(buffer + cnt, fmt, blake3_impls[i]->name);
		cid++;
	}

	buffer[cnt] = 0;

	return (cnt);
}

module_param_call(icp_blake3_impl, icp_blake3_impl_set, icp_blake3_impl_get,
    NULL, 0644);
MODULE_PARM_DESC(icp_blake3_impl, "Select BLAKE3 implementation.");
#endif
