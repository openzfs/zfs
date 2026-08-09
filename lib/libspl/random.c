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
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2016 Actifio, Inc. All rights reserved.
 * Copyright (c) 2025, Klara, Inc.
 */

#include <stdint.h>
#include <fcntl.h>
#include <assert.h>
#include <sys/random.h>
#include "libspl_impl.h"

#define	RANDOM_PATH	"/dev/random"
#define	URANDOM_PATH	"/dev/urandom"

static int random_fd = -1, urandom_fd = -1;

static boolean_t force_pseudo = B_FALSE;

void
random_init(void)
{
	/* Handle multiple calls. */
	if (random_fd != -1) {
		ASSERT3U(urandom_fd, !=, -1);
		return;
	}

	VERIFY((random_fd = open(RANDOM_PATH, O_RDONLY | O_CLOEXEC)) != -1);
	VERIFY((urandom_fd = open(URANDOM_PATH, O_RDONLY | O_CLOEXEC)) != -1);
}

void
random_fini(void)
{
	close(random_fd);
	close(urandom_fd);

	random_fd = -1;
	urandom_fd = -1;
}

void
random_force_pseudo(boolean_t onoff)
{
	force_pseudo = onoff;
}

static int
random_get_bytes_common(uint8_t *ptr, size_t len, int fd)
{
	size_t resid = len;
	ssize_t bytes;

	ASSERT(fd != -1);

	while (resid != 0) {
		bytes = read(fd, ptr, resid);
		ASSERT3S(bytes, >=, 0);
		ptr += bytes;
		resid -= bytes;
	}

	return (0);
}

int
random_get_bytes(uint8_t *ptr, size_t len)
{
	if (force_pseudo)
		return (random_get_pseudo_bytes(ptr, len));
	return (random_get_bytes_common(ptr, len, random_fd));
}

int
random_get_pseudo_bytes(uint8_t *ptr, size_t len)
{
	return (random_get_bytes_common(ptr, len, urandom_fd));
}
