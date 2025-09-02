// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * Copyright (c) 2024 The FreeBSD Foundation
 *
 * This software was developed by Pawel Dawidek <pawel@dawidek.net>
 * under sponsorship from the FreeBSD Foundation.
 */

#include <sys/mman.h>
#include <sys/stat.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef __unused
#define	__unused	__attribute__((__unused__))
#endif

static const char *progname;

static void
usage(void)
{
	(void) fprintf(stderr, "usage: %s <cnt> <syscall> <args>\n", progname);
	(void) fprintf(stderr, "       chmod <path>\n");
	(void) fprintf(stderr, "       chown <path>\n");
	(void) fprintf(stderr, "       create <path>\n");
	(void) fprintf(stderr, "       link <path>\n");
	(void) fprintf(stderr, "       mkdir <path>\n");
	(void) fprintf(stderr, "       readlink <symlink>\n");
	(void) fprintf(stderr, "       rmdir <path>\n");
	(void) fprintf(stderr, "       stat <path>\n");
	(void) fprintf(stderr, "       symlink <path>\n");
	(void) fprintf(stderr, "       unlink <path>\n");
	exit(3);
}

static bool
fsop_chmod(int i __unused, const char *path)
{
	return (chmod(path, 0600) == 0);
}

static bool
fsop_chown(int i __unused, const char *path)
{
	return (chown(path, 0, 0) == 0);
}

static bool
fsop_create(int i, const char *base)
{
	char path[MAXPATHLEN];
	int fd;

	snprintf(path, sizeof (path), "%s.%d", base, i);

	fd = open(path, O_CREAT | O_EXCL, 0600);
	if (fd < 0) {
		return (false);
	}
	close(fd);
	return (true);
}

static bool
fsop_link(int i, const char *base)
{
	char path[MAXPATHLEN];

	snprintf(path, sizeof (path), "%s.%d", base, i);

	return (link(base, path) == 0);
}

static bool
fsop_stat(int i __unused, const char *path)
{
	struct stat sb;

	return (stat(path, &sb) == 0);
}

static bool
fsop_mkdir(int i, const char *base)
{
	char path[MAXPATHLEN];

	snprintf(path, sizeof (path), "%s.%d", base, i);

	return (mkdir(path, 0700) == 0);
}

static bool
fsop_readlink(int i __unused, const char *symlink)
{
	char path[MAXPATHLEN];

	return (readlink(symlink, path, sizeof (path)) >= 0);
}

static bool
fsop_rename(int i, const char *base)
{
	char path[MAXPATHLEN];
	const char *src, *dst;

	snprintf(path, sizeof (path), "%s.renamed", base);

	if ((i & 1) == 0) {
		src = base;
		dst = path;
	} else {
		src = path;
		dst = base;
	}

	return (rename(src, dst) == 0);
}

static bool
fsop_rmdir(int i, const char *base)
{
	char path[MAXPATHLEN];

	snprintf(path, sizeof (path), "%s.%d", base, i);

	return (rmdir(path) == 0);
}

static bool
fsop_symlink(int i, const char *base)
{
	char path[MAXPATHLEN];

	snprintf(path, sizeof (path), "%s.%d", base, i);

	return (symlink(base, path) == 0);
}

static bool
fsop_unlink(int i, const char *base)
{
	char path[MAXPATHLEN];

	snprintf(path, sizeof (path), "%s.%d", base, i);

	return (unlink(path) == 0);
}

static struct fsop {
	const char	*fo_syscall;
	bool		(*fo_handler)(int, const char *);
} fsops[] = {
	{ "chmod", fsop_chmod },
	{ "chown", fsop_chown },
	{ "create", fsop_create },
	{ "link", fsop_link },
	{ "mkdir", fsop_mkdir },
	{ "readlink", fsop_readlink },
	{ "rename", fsop_rename },
	{ "rmdir", fsop_rmdir },
	{ "stat", fsop_stat },
	{ "symlink", fsop_symlink },
	{ "unlink", fsop_unlink }
};

int
main(int argc, char *argv[])
{
	struct fsop *fsop;
	const char *syscall;
	int count;

	progname = argv[0];

	if (argc < 3) {
		usage();
	}

	count = atoi(argv[1]);
	if (count <= 0) {
		(void) fprintf(stderr, "invalid count\n");
		exit(2);
	}
	syscall = argv[2];
	argc -= 3;
	argv += 3;
	if (argc != 1) {
		usage();
	}

	fsop = NULL;
	for (unsigned int i = 0; i < sizeof (fsops) / sizeof (fsops[0]); i++) {
		if (strcmp(fsops[i].fo_syscall, syscall) == 0) {
			fsop = &fsops[i];
			break;
		}
	}
	if (fsop == NULL) {
		fprintf(stderr, "Unknown syscall: %s\n", syscall);
		exit(2);
	}

	for (int i = 0; i < count; i++) {
		if (!fsop->fo_handler(i, argv[0])) {
			fprintf(stderr, "%s() failed: %s\n", syscall,
			    strerror(errno));
			exit(1);
		}
	}

	exit(0);
}
