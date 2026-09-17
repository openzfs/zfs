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
 * Copyright (c) 2026, TrueNAS.
 */

#ifndef _ZFS_KTHREAD_COMPAT_H
#define	_ZFS_KTHREAD_COMPAT_H

#include <linux/fs_struct.h>

/*
 * Linux 7.3 change. Kernel-internal threads no longer have access to the
 * filesystem by default. Callers that need it should wrap the necessary code
 * in `scoped_with_init_fs()`, which will make the filesystem availble until
 * end of scope.
 *
 * For older kernels that do not provide this macro, we define it to be a
 * no-op.
 */
#ifndef scoped_with_init_fs
#define	scoped_with_init_fs()	if (1)
#endif

#endif
