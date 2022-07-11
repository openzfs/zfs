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
 * Copyright (c) 2020 by Delphix. All rights reserved.
 */

#ifndef _ZFS_PERCPU_H
#define	_ZFS_PERCPU_H

#include <linux/percpu_counter.h>

/*
 * 3.18 API change,
 * percpu_counter_init() now must be passed a gfp mask which will be
 * used for the dynamic allocation of the actual counter.
 */
#ifdef HAVE_PERCPU_COUNTER_INIT_WITH_GFP
#define	percpu_counter_init_common(counter, n, gfp) \
	percpu_counter_init(counter, n, gfp)
#else
#define	percpu_counter_init_common(counter, n, gfp) \
	percpu_counter_init(counter, n)
#endif

#endif /* _ZFS_PERCPU_H */
