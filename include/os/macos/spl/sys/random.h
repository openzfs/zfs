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
 *
 * Copyright (C) 2013 Jorgen Lundman <lundman@lundman.net>
 *
 */

#ifndef _SPL_RANDOM_H
#define	_SPL_RANDOM_H

#include_next <sys/random.h>


static inline int
random_get_bytes(uint8_t *ptr, size_t len)
{
	read_random(ptr, len);
	return (0);
}

static inline int
random_get_pseudo_bytes(uint8_t *ptr, size_t len)
{
	read_random(ptr, len);
	return (0);
}

static inline uint32_t
random_in_range(uint32_t range)
{
	uint32_t r;

	ASSERT(range != 0);

	if (range == 1)
		return (0);

	read_random((void *)&r, sizeof (r));

	return (r % range);
}

#endif	/* _SPL_RANDOM_H */
