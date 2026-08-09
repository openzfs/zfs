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
 * Copyright (c) 2017, Lawrence Livermore National Security, LLC.
 */

#include <sys/zfs_ratelimit.h>

/*
 * Initialize rate limit struct
 *
 * rl:		zfs_ratelimit_t struct
 * burst:	Number to allow in an interval before rate limiting
 * interval:	Interval time in seconds
 */
void
zfs_ratelimit_init(zfs_ratelimit_t *rl, unsigned int *burst,
    unsigned int interval)
{
	rl->count = 0;
	rl->start = 0;
	rl->interval = interval;
	rl->burst = burst;
	mutex_init(&rl->lock, NULL, MUTEX_DEFAULT, NULL);
}

/*
 * Finalize rate limit struct
 *
 * rl:		zfs_ratelimit_t struct
 */
void
zfs_ratelimit_fini(zfs_ratelimit_t *rl)
{
	mutex_destroy(&rl->lock);
}

/*
 * Re-implementation of the kernel's __ratelimit() function
 *
 * We had to write our own rate limiter because the kernel's __ratelimit()
 * function annoyingly prints out how many times it rate limited to the kernel
 * logs (and there's no way to turn it off):
 *
 *	__ratelimit: 59 callbacks suppressed
 *
 * If the kernel ever allows us to disable these prints, we should go back to
 * using __ratelimit() instead.
 *
 * Return values are the same as __ratelimit():
 *
 * 0: If we're rate limiting
 * 1: If we're not rate limiting.
 */
int
zfs_ratelimit(zfs_ratelimit_t *rl)
{
	hrtime_t now;

	hrtime_t elapsed;
	int error = 1;

	mutex_enter(&rl->lock);

	now = gethrtime();
	elapsed = now - rl->start;

	rl->count++;
	if (NSEC2SEC(elapsed) >= rl->interval) {
		rl->start = now;
		rl->count = 0;
	} else {
		if (rl->count >= *rl->burst) {
			error = 0; /* We're ratelimiting */
		}
	}
	mutex_exit(&rl->lock);

	return (error);
}
