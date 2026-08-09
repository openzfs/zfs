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
 * Copyright (c) 2026, TrueNAS.
 */

/*
 * This is a sanity check test for the F_SETLEASE and F_GETLEASE fcntl() calls.
 * We use the generic kernel implementation, but we want to be alerted if it
 * ever breaks.
 *
 * This is not a comprehensive test. It would be nice if it could be!
 */

#ifndef _GNU_SOURCE
#define	_GNU_SOURCE
#endif

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

static int
get_lease(int fd) {
	int r = fcntl(fd, F_GETLEASE);
	if (r < 0) {
		perror("fcntl(GETLEASE)");
		exit(2);
	}
	return (r);
}

static int
set_lease(int fd, int lease) {
	return (fcntl(fd, F_SETLEASE, lease) < 0 ? errno : 0);
}

static const char *lease_str[] = {
	[F_RDLCK] = "RDLCK",
	[F_WRLCK] = "WRLCK",
	[F_UNLCK] = "UNLCK",
};

static void
assert_lease(int fd, int expect) {
	int got = get_lease(fd);
	if (got != expect) {
		fprintf(stderr, "ASSERT_LEASE: expected %s [%d], got %s [%d]\n",
		    lease_str[expect], expect, lease_str[got], got);
		abort();
	}
	printf("ok: lease is %s\n", lease_str[got]);
}

static void
assert_set_lease(int fd, int lease) {
	int err = set_lease(fd, lease);
	if (err != 0) {
		fprintf(stderr, "ASSERT_SET_LEASE: tried %s [%d], error: %s\n",
		    lease_str[lease], lease, strerror(err));
		abort();
	}
	printf("ok: set lease to %s\n", lease_str[lease]);
}

int
main(int argc, char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "usage: %s <filename>\n", argv[0]);
		exit(1);
	}

	/* create and open file, read+write */
	int fd = open(argv[1], O_CREAT|O_RDONLY, S_IRWXU|S_IRWXG|S_IRWXO);
	if (fd < 0) {
		perror("open");
		exit(2);
	}
	printf("ok: opened file RDONLY\n");

	/* fd starts with no lease */
	assert_lease(fd, F_UNLCK);

	/* fd is readonly, so can take read lease */
	assert_set_lease(fd, F_RDLCK);
	/* confirm read lease */
	assert_lease(fd, F_RDLCK);

	/* no other openers, so can take write lease */
	assert_set_lease(fd, F_WRLCK);
	/* confirm write lease */
	assert_lease(fd, F_WRLCK);

	/* release lease */
	assert_set_lease(fd, F_UNLCK);
	/* confirm lease released */
	assert_lease(fd, F_UNLCK);

	close(fd);

	return (0);
}
