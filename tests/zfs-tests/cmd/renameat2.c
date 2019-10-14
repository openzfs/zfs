/* SPDX-License-Identifier: CDDL-1.0 OR MPL-2.0 */
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
 * Copyright (C) 2019 Aleksa Sarai <cyphar@cyphar.com>
 * Copyright (C) 2019 SUSE LLC
 */

/*
 * mv(1) doesn't currently support RENAME_{EXCHANGE,WHITEOUT} so this is a very
 * simple renameat2(2) wrapper for the OpenZFS self-tests.
 */

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>

#ifndef SYS_renameat2
#ifdef __NR_renameat2
#define	SYS_renameat2 __NR_renameat2
#elif defined(__x86_64__)
#define	SYS_renameat2 316
#elif defined(__i386__)
#define	SYS_renameat2 353
#elif defined(__arm__) || defined(__aarch64__)
#define	SYS_renameat2 382
#else
#error "SYS_renameat2 not known for this architecture."
#endif
#endif

#ifndef RENAME_NOREPLACE
#define	RENAME_NOREPLACE	(1 << 0) /* Don't overwrite target */
#endif
#ifndef RENAME_EXCHANGE
#define	RENAME_EXCHANGE		(1 << 1) /* Exchange source and dest */
#endif
#ifndef RENAME_WHITEOUT
#define	RENAME_WHITEOUT		(1 << 2) /* Whiteout source */
#endif

/* glibc doesn't provide renameat2 wrapper, let's use our own */
static int
sys_renameat2(int olddirfd, const char *oldpath,
    int newdirfd, const char *newpath, unsigned int flags)
{
	int ret = syscall(SYS_renameat2, olddirfd, oldpath, newdirfd, newpath,
	    flags);
	return ((ret < 0) ? -errno : ret);
}

static void
usage(void)
{
	fprintf(stderr, "usage: renameat2 [-Cnwx] src dst\n");
	exit(1);
}

static void
check(void)
{
	int err = sys_renameat2(AT_FDCWD, ".", AT_FDCWD, ".", RENAME_EXCHANGE);
	exit(err == -ENOSYS);
}

int
main(int argc, char **argv)
{
	char *src, *dst;
	int ch, err;
	unsigned int flags = 0;

	while ((ch = getopt(argc, argv, "Cnwx")) >= 0) {
		switch (ch) {
			case 'C':
				check();
				break;
			case 'n':
				flags |= RENAME_NOREPLACE;
				break;
			case 'w':
				flags |= RENAME_WHITEOUT;
				break;
			case 'x':
				flags |= RENAME_EXCHANGE;
				break;
			default:
				usage();
				break;
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 2)
		usage();
	src = argv[0];
	dst = argv[1];

	err = sys_renameat2(AT_FDCWD, src, AT_FDCWD, dst, flags);
	if (err < 0)
		fprintf(stderr, "renameat2: %s", strerror(-err));
	return (err != 0);
}
