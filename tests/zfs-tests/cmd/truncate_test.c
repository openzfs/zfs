// SPDX-License-Identifier: CDDL-1.0
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
 * Copyright (c) 2012, 2014 by Delphix. All rights reserved.
 * Copyright 2017, loli10K <ezomori.nozomu@gmail.com>. All rights reserved.
 */

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#define	FSIZE	256*1024*1024

static long	fsize = FSIZE;
static int	errflag = 0;
static char	*filename = NULL;
static int	ftruncflag = 0;

static void parse_options(int argc, char *argv[]);

static void
usage(char *execname)
{
	(void) fprintf(stderr,
	    "usage: %s [-s filesize] [-f] /path/to/file\n", execname);
	(void) exit(1);
}

int
main(int argc, char *argv[])
{
	int fd;

	parse_options(argc, argv);

	if (ftruncflag) {
		fd = open(filename, O_RDWR|O_CREAT, 0666);
		if (fd < 0) {
			perror("open");
			return (1);
		}
		if (ftruncate(fd, fsize) < 0) {
			perror("ftruncate");
			return (1);
		}
		if (close(fd)) {
			perror("close");
			return (1);
		}
	} else {
		if (truncate(filename, fsize) < 0) {
			perror("truncate");
			return (1);
		}
	}
	return (0);
}

static void
parse_options(int argc, char *argv[])
{
	int c;
	extern char *optarg;
	extern int optind, optopt;

	while ((c = getopt(argc, argv, "s:f")) != -1) {
		switch (c) {
			case 's':
				fsize = atoi(optarg);
				break;
			case 'f':
				ftruncflag++;
				break;
			case ':':
				(void) fprintf(stderr,
				    "Option -%c requires an operand\n", optopt);
				errflag++;
				break;
		}
		if (errflag) {
			(void) usage(argv[0]);
		}
	}

	if (argc <= optind) {
		(void) fprintf(stderr, "No filename specified\n");
		usage(argv[0]);
	}
	filename = argv[optind];
}
