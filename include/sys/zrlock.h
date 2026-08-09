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
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2015 by Delphix. All rights reserved.
 */

#ifndef	_SYS_ZRLOCK_H
#define	_SYS_ZRLOCK_H

#include <sys/zfs_context.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct zrlock {
	kmutex_t zr_mtx;
	kcondvar_t zr_cv;
	volatile int32_t zr_refcount;
#ifdef	ZFS_DEBUG
	kthread_t *zr_owner;
	const char *zr_caller;
#endif
} zrlock_t;

extern void zrl_init(zrlock_t *);
extern void zrl_destroy(zrlock_t *);
#define	zrl_add(_z)	zrl_add_impl((_z), __func__)
extern void zrl_add_impl(zrlock_t *, const char *);
extern void zrl_remove(zrlock_t *);
extern int zrl_tryenter(zrlock_t *);
extern void zrl_exit(zrlock_t *);
extern int zrl_is_zero(zrlock_t *);
extern int zrl_is_locked(zrlock_t *);
#ifdef	ZFS_DEBUG
extern kthread_t *zrl_owner(zrlock_t *);
#endif

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_ZRLOCK_H */
