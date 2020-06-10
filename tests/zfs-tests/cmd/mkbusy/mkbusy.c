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

static void
usage(char *progname)
{
	(void) fprintf(stderr, "Usage: %s <dirname|filename>\n", progname);
	exit(1);
}

static void
fail(char *err, int rval)
{
	perror(err);
	exit(rval);
}

static void
daemonize(void)
{
	pid_t	pid;

	if ((pid = fork()) < 0) {
		fail("fork", 1);
	} else if (pid != 0) {
		(void) fprintf(stdout, "%ld\n", (long)pid);
		exit(0);
	}

	(void) setsid();
	(void) close(0);
	(void) close(1);
	(void) close(2);
}

int
main(int argc, char *argv[])
{
	int		ret, c;
	boolean_t	isdir = B_FALSE;
	boolean_t	fflag = B_FALSE;
	boolean_t	rflag = B_FALSE;
	struct stat	sbuf;
	char		*fpath = NULL;
	char		*prog = argv[0];

	while ((c = getopt(argc, argv, "fr")) != -1) {
		switch (c) {
		/* Open the file or directory read only */
		case 'r':
			rflag = B_TRUE;
			break;
		/* Run in the foreground */
		case 'f':
			fflag = B_TRUE;
			break;
		default:
			usage(prog);
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1)
		usage(prog);

	if ((ret = stat(argv[0], &sbuf)) != 0) {
		char	*arg, *dname, *fname;
		int	arglen;
		char	*slash;
		int	rc;

		/*
		 * The argument supplied doesn't exist. Copy the path, and
		 * remove the trailing slash if present.
		 */
		if ((arg = strdup(argv[0])) == NULL)
			fail("strdup", 1);
		arglen = strlen(arg);
		if (arg[arglen - 1] == '/')
			arg[arglen - 1] = '\0';

		/*
		 * Get the directory and file names, using the current directory
		 * if the provided path doesn't specify a directory at all.
		 */
		if ((slash = strrchr(arg, '/')) == NULL) {
			dname = strdup(".");
			fname = strdup(arg);
		} else {
			*slash = '\0';
			dname = strdup(arg);
			fname = strdup(slash + 1);
		}
		free(arg);
		if (dname == NULL || fname == NULL)
			fail("strdup", 1);

		/* The directory portion of the path must exist */
		if ((ret = stat(dname, &sbuf)) != 0 || !(sbuf.st_mode &
		    S_IFDIR))
			usage(prog);

		rc = asprintf(&fpath, "%s/%s", dname, fname);
		free(dname);
		free(fname);
		if (rc == -1 || fpath == NULL)
			fail("asprintf", 1);

	} else if ((sbuf.st_mode & S_IFMT) == S_IFREG ||
	    (sbuf.st_mode & S_IFMT) == S_IFLNK ||
	    (sbuf.st_mode & S_IFMT) == S_IFCHR ||
	    (sbuf.st_mode & S_IFMT) == S_IFBLK) {
		fpath = strdup(argv[0]);
	} else if ((sbuf.st_mode & S_IFMT) == S_IFDIR) {
		fpath = strdup(argv[0]);
		isdir = B_TRUE;
	} else {
		usage(prog);
	}

	if (fpath == NULL)
		fail("strdup", 1);

	if (isdir == B_FALSE) {
		int	fd, flags;
		mode_t	mode = S_IRUSR | S_IWUSR;

		flags = rflag == B_FALSE ? O_CREAT | O_RDWR : O_RDONLY;

		if ((fd = open(fpath, flags, mode)) < 0)
			fail("open", 1);
	} else {
		DIR	*dp;

		if ((dp = opendir(fpath)) == NULL)
			fail("opendir", 1);
	}
	free(fpath);

	if (fflag == B_FALSE)
		daemonize();
	(void) pause();

	/* NOTREACHED */
	return (0);
}
