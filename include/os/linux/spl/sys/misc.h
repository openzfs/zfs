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

#ifndef _OS_LINUX_SPL_MISC_H
#define	_OS_LINUX_SPL_MISC_H

#include <linux/kobject.h>
#include <linux/swap.h>

extern void spl_signal_kobj_evt(struct block_device *bdev);

/*
 * Check if the current thread is a memory reclaim thread.
 */
extern int current_is_reclaim_thread(void);

#endif
