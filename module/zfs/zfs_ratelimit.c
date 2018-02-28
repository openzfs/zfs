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
