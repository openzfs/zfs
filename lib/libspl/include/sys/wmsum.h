/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */

/*
 * wmsum counters are a reduced version of aggsum counters, optimized for
 * write-mostly scenarios.  They do not provide optimized read functions,
 * but instead allow much cheaper add function.  The primary usage is
 * infrequently read statistic counters, not requiring exact precision.
 *
 * In user-space due to lack of better implementation mapped to aggsum.
 */

#ifndef	_SYS_WMSUM_H
#define	_SYS_WMSUM_H

#include <sys/aggsum.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	wmsum_t	aggsum_t

static inline void
wmsum_init(wmsum_t *ws, uint64_t value)
{

	aggsum_init(ws, value);
}

static inline void
wmsum_fini(wmsum_t *ws)
{

	aggsum_fini(ws);
}

static inline uint64_t
wmsum_value(wmsum_t *ws)
{

	return (aggsum_value(ws));
}

static inline void
wmsum_add(wmsum_t *ws, int64_t delta)
{

	aggsum_add(ws, delta);
}

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_WMSUM_H */
