// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright (c) 2026, TrueNAS.
 */

#ifndef _ZFS_WORKQUEUE_COMPAT_H
#define	_ZFS_WORKQUEUE_COMPAT_H

/*
 * Forcibly flush the kernel's "delayed work" workqueue.
 */
#ifdef HAVE_FLUSH_DELAY_WORKQUEUE_INTERNAL
/*
 * Linux 5.19: flush_workqueue() on system workqueues is a compile error, use
 *              __flush_workqueue() instead.
 */
#ifdef HAVE_FLUSH_DELAY_WORKQUEUE_PERCPU
/*
 * Linux 6.17: system_percpu_wq introduced as direct replacement for system_wq,
 *             and takes over as the delay workqueue.
 */
#define	zpl_flush_delay_workqueue()	__flush_workqueue(system_percpu_wq)
#else
#define	zpl_flush_delay_workqueue()	__flush_workqueue(system_wq)
#endif
#else
#define	zpl_flush_delay_workqueue()	flush_workqueue(system_wq)
#endif

#endif
