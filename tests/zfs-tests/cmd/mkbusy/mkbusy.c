/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright (c) 2012 by Delphix. All rights reserved.
 */

/*
 * Make a directory busy. If the argument is an existing file or directory,
 * simply open it directly and pause. If not, verify that the parent directory
 * exists, and create a new file in that directory.
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <strings.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>


static __attribute__((noreturn)) void
usage(char *progname)
{
	(void) fprintf(stderr, "Usage: %s <dirname|filename>\n", progname);
	exit(1);
}

static __attribute__((noreturn)) void
fail(char *err)
{
	perror(err);
	exit(1);
}

static void
daemonize(void)
{
	pid_t	pid;

	if ((pid = fork()) < 0) {
		fail("fork");
	} else if (pid != 0) {
		(void) fprintf(stdout, "%ld\n", (long)pid);
		exit(0);
	}

	(void) setsid();
	(void) close(0);
	(void) close(1);
	(void) close(2);
}


static const char *
get_basename(const char *path)
{
	const char *bn = strrchr(path, '/');
	return (bn ? bn + 1 : path);
}

static ssize_t
get_dirnamelen(const char *path)
{
	const char *end = strrchr(path, '/');
	return (end ? end - path : -1);
}

int
main(int argc, char *argv[])
{
	int		c;
	boolean_t	isdir = B_FALSE;
	struct stat	sbuf;
	char		*fpath = NULL;
	char		*prog = argv[0];

	while ((c = getopt(argc, argv, "")) != -1) {
		switch (c) {
		default:
			usage(prog);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1)
		usage(prog);

	if (stat(argv[0], &sbuf) != 0) {
		char	*arg;
		const char	*dname, *fname;
		size_t	arglen;
		ssize_t	dnamelen;

		/*
		 * The argument supplied doesn't exist. Copy the path, and
		 * remove the trailing slash if present.
		 */
		if ((arg = strdup(argv[0])) == NULL)
			fail("strdup");
		arglen = strlen(arg);
		if (arg[arglen - 1] == '/')
			arg[arglen - 1] = '\0';

		/* Get the directory and file names. */
		fname = get_basename(arg);
		dname = arg;
		if ((dnamelen = get_dirnamelen(arg)) != -1)
			arg[dnamelen] = '\0';
		else
			dname = ".";

		/* The directory portion of the path must exist */
		if (stat(dname, &sbuf) != 0 || !(sbuf.st_mode & S_IFDIR))
			usage(prog);

		if (asprintf(&fpath, "%s/%s", dname, fname) == -1)
			fail("asprintf");

		free(arg);
	} else
		switch (sbuf.st_mode & S_IFMT) {
			case S_IFDIR:
				isdir = B_TRUE;
				fallthrough;
			case S_IFLNK:
			case S_IFCHR:
			case S_IFBLK:
				if ((fpath = strdup(argv[0])) == NULL)
					fail("strdup");
				break;
			default:
				usage(prog);
		}

	if (!isdir) {
		int	fd;

		if ((fd = open(fpath, O_CREAT | O_RDWR, 0600)) < 0)
			fail("open");
	} else {
		DIR	*dp;

		if ((dp = opendir(fpath)) == NULL)
			fail("opendir");
	}
	free(fpath);

	daemonize();
	(void) pause();

	return (0);
}
