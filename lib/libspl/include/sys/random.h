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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2011 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2012, Joyent, Inc. All rights reserved.
 */

#ifndef _SYS_RANDOM_H
#define	_SYS_RANDOM_H

extern int random_get_bytes(uint8_t *ptr, size_t len);
extern int random_get_pseudo_bytes(uint8_t *ptr, size_t len);

extern void random_force_pseudo(boolean_t onoff);

static __inline__ uint32_t
random_in_range(uint32_t range)
{
	uint32_t r;

	ASSERT(range != 0);

	if (range == 1)
		return (0);

	(void) random_get_pseudo_bytes((uint8_t *)&r, sizeof (r));

	return (r % range);
}

#endif	/* _SYS_RANDOM_H */
