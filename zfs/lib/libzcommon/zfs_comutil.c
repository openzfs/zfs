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
 * This file is intended for functions that ought to be common between user
 * land (libzfs) and the kernel. When many common routines need to be shared
 * then a separate file should to be created.
 */

#if defined(_KERNEL)
#include <sys/systm.h>
#endif

#include <sys/types.h>
#include <sys/fs/zfs.h>
#include <sys/nvpair.h>

/*
 * Are there allocatable vdevs?
 */
boolean_t
zfs_allocatable_devs(nvlist_t *nv)
{
	uint64_t is_log;
	uint_t c;
	nvlist_t **child;
	uint_t children;

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0) {
		return (B_FALSE);
	}
	for (c = 0; c < children; c++) {
		is_log = 0;
		(void) nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_LOG,
		    &is_log);
		if (!is_log)
			return (B_TRUE);
	}
	return (B_FALSE);
}
