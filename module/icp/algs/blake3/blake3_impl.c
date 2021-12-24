
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
 * Copyright (c) 2021 Tino Reichardt <milky-zfs@mcmilk.de>
 */

#include <sys/zfs_context.h>
#include <sys/zio_checksum.h>
#include "blake3_impl.h"

static const blake3_impl_ops_t *blake3_impls[] = {
	&blake3_generic_impl,
#if defined(__x86_64) && defined(HAVE_SSE2)
	&blake3_sse2_impl,
#endif
#if defined(__x86_64) && defined(HAVE_SSE4_1)
	&blake3_sse41_impl,
#endif
#if defined(__x86_64) && defined(HAVE_SSE4_1) && defined(HAVE_AVX2)
	&blake3_avx2_impl,
#endif
#if defined(__x86_64) && defined(HAVE_AVX512F) && defined(HAVE_AVX512VL)
	&blake3_avx512_impl,
#endif
#if defined(__aarch64__)
	&blake3_neon_impl,
#endif
};

/* this pointer will be used by the assembler impl */
static const blake3_impl_ops_t *blake3_selected_impls = 0;

/*
 * Returns the number of supported BLAKE3 implementations
 */
int
blake3_get_impl_count(void)
{
	int i, supp_impl = 0;

	for (i = 0; i < ARRAY_SIZE(blake3_impls); i++) {
		if (blake3_impls[i]->is_supported()) {
			supp_impl++;
		}
	}

	return (supp_impl);
}

/*
 * Returns the name of selected BLAKE3 implementation
 */
const char *
blake3_get_impl_name(void)
{
	return (blake3_selected_impls->name);
}

/*
 * Set BLAKE3 implementation by id
 * - id: 0..blake3_impl_count()
 */
void
blake3_set_impl_id(int id)
{
	int i, supp_impl = 0;

	for (i = 0; i < ARRAY_SIZE(blake3_impls); i++) {
		if (!blake3_impls[i]->is_supported()) continue;

		if (id == supp_impl) {
			blake3_selected_impls = blake3_impls[i];
			return;
		}
		supp_impl++;
	}
}

/*
 * Set BLAKE3 implementation by name
 */
void
blake3_set_impl_name(char *name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(blake3_impls); i++) {
		if (!blake3_impls[i]->is_supported()) continue;
		if (strcmp(name, blake3_impls[i]->name) == 0) {
			blake3_selected_impls = blake3_impls[i];
			return;
		}
	}
}

/*
 * Returns currently selected BLAKE3 implementation
 */
const blake3_impl_ops_t *
blake3_impl_get_ops(void)
{
	if (blake3_selected_impls)
		return (blake3_selected_impls);

	/* take the safe generic as default */
	blake3_selected_impls = blake3_impls[0];

	return (blake3_selected_impls);
}

#ifdef _KERNEL
EXPORT_SYMBOL(blake3_get_impl_count);
EXPORT_SYMBOL(blake3_get_impl_name);
EXPORT_SYMBOL(blake3_set_impl_id);
EXPORT_SYMBOL(blake3_set_impl_name);
#endif
