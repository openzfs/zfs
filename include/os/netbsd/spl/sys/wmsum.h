// SPDX-License-Identifier: CDDL-1.0
#ifndef	_SYS_WMSUM_H
#define	_SYS_WMSUM_H

#include <sys/types.h>
#include <sys/atomic.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef uint64_t wmsum_t;

static inline void
wmsum_init(wmsum_t *ws, uint64_t value)
{
	*ws = value;
}

static inline void
wmsum_fini(wmsum_t *ws)
{
	(void) ws;
}

static inline uint64_t
wmsum_value(wmsum_t *ws)
{
	return (*ws);
}

static inline void
wmsum_add(wmsum_t *ws, int64_t delta)
{
	atomic_add_64(ws, delta);
}

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_WMSUM_H */
