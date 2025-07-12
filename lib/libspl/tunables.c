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
 * Copyright (c) 2025, Rob Norris <robn@despairlabs.com>
 */

#include <stddef.h>
#include <sys/tunables.h>

/*
 * Userspace tunables.
 *
 * Tunables are external pointers to global variables that are wired up to the
 * host environment in some way that allows the operator to directly change
 * their values "under the hood".
 *
 * In userspace, the "host environment" is the program using libzpool.so. So
 * that it can manipulate tunables if it wants, we provide an API to access
 * them.
 *
 * Tunables are declared through the ZFS_MODULE_PARAM* macros, which associate
 * a global variable with some metadata we can use to describe and access the
 * tunable. This is done by creating a uniquely-named zfs_tunable_t.
 *
 * At runtime, we need a way to discover these zfs_tunable_t items. Since they
 * are declared globally, all over the codebase, there's no central place to
 * record or list them. So, we take advantage of the compiler's "linker set"
 * feature.
 *
 * In the ZFS_MODULE_PARAM macro, after we create the zfs_tunable_t, we also
 * create a zfs_tunable_t* pointing to it. That pointer is forced into the
 * "zfs_tunables" ELF section in compiled object. At link time, the linker will
 * collect all these pointers into one single big "zfs_tunable" section, and
 * will generate two new symbols in the final object: __start_zfs_tunable and
 * __stop_zfs_tunable. These point to the first and last item in that section,
 * which allows us to access the pointers in that section like an array, and
 * through those pointers access the tunable metadata, and from there the
 * actual C variable that the tunable describes.
 */

extern const zfs_tunable_t *__start_zfs_tunables;
extern const zfs_tunable_t *__stop_zfs_tunables;

/*
 * Because there are no tunables in libspl itself, the above symbols will not
 * be generated, which will stop libspl being linked at all. To work around
 * that, we force a symbol into that section, and then when iterating, skip
 * any NULL pointers.
 */
static void *__zfs_tunable__placeholder
	__attribute__((__section__("zfs_tunables")))
	__attribute__((__used__)) = NULL;
