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
