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

#ifndef _SYS_ARC_GROWTH_H
#define	_SYS_ARC_GROWTH_H

#include <sys/zfs_context.h>

typedef struct arc_growth {
	/* Zero permits growth; an expired deadline still needs reaping. */
	uint64_t ag_resume_time;
} arc_growth_t;

static inline boolean_t
arc_growth_paused(arc_growth_t *growth)
{
	return (atomic_load_64(&growth->ag_resume_time) != 0);
}

static inline void
arc_growth_pause(arc_growth_t *growth, uint64_t deadline)
{
	ASSERT3U(deadline, >, 0);
	uint64_t old = atomic_load_64(&growth->ag_resume_time);

	while (old < deadline) {
		uint64_t seen = atomic_cas_64(&growth->ag_resume_time,
		    old, deadline);
		if (seen == old)
			break;
		old = seen;
	}
}

static inline void
arc_growth_resume(arc_growth_t *growth, uint64_t now)
{
	uint64_t deadline = atomic_load_64(&growth->ag_resume_time);

	/* A newer pressure request must survive this expiry check. */
	if (deadline != 0 && deadline <= now)
		(void) atomic_cas_64(&growth->ag_resume_time, deadline, 0);
}

#endif /* _SYS_ARC_GROWTH_H */
