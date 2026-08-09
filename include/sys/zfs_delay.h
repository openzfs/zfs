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

#ifndef	_SYS_FS_ZFS_DELAY_H
#define	_SYS_FS_ZFS_DELAY_H

#include <sys/timer.h>

/*
 * Generic wrapper to sleep until a given time.
 */
#define	zfs_sleep_until(wakeup)						\
	do {								\
		hrtime_t delta = wakeup - gethrtime();			\
									\
		if (delta > 0) {					\
			unsigned long delta_us;				\
			delta_us = delta / (NANOSEC / MICROSEC);	\
			usleep_range(delta_us, delta_us + 100);		\
		}							\
	} while (0)

#endif	/* _SYS_FS_ZFS_DELAY_H */
