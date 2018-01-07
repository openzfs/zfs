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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Copyright (c) 2012 by Delphix. All rights reserved.
 */

#include "../file_common.h"
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <linux/falloc.h>

/*
 * Create a file with assigned size and then free the specified
 * section of the file
 */

static void usage(char *progname);

static void
usage(char *progname)
{
	(void) fprintf(stderr,
	    "usage: %s [-l filesize] [-s start-offset]"
	    "[-n section-len] filename\n", progname);
	exit(1);
}

int
main(int argc, char *argv[])
{
	char *filename = NULL;
	char *buf = NULL;
	size_t filesize = 0;
	off_t start_off = 0;
	off_t off_len = 0;
	int  fd, ch;
	mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

	while ((ch = getopt(argc, argv, "l:s:n:")) != EOF) {
		switch (ch) {
		case 'l':
			filesize = atoll(optarg);
			break;
		case 's':
			start_off = atoll(optarg);
			break;
		case 'n':
			off_len = atoll(optarg);
			break;
		default:
			usage(argv[0]);
			break;
		}
	}

	if (optind == argc - 1)
		filename = argv[optind];
	else
		usage(argv[0]);

	if ((fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, mode)) < 0) {
		perror("open");
		return (1);
	}

	buf = (char *)calloc(1, filesize);
	if (buf == NULL) {
		perror("write");
		close(fd);
		return (1);
	}
	memset(buf, 'c', filesize);

	if (write(fd, buf, filesize) < filesize) {
		free(buf);
		perror("write");
		close(fd);
		return (1);
	}

	free(buf);

#if defined(FALLOC_FL_PUNCH_HOLE) && defined(FALLOC_FL_KEEP_SIZE)
	if (fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
	    start_off, off_len) < 0) {
		perror("fallocate");
		close(fd);
		return (1);
	}
#else /* !(defined(FALLOC_FL_PUNCH_HOLE) && defined(FALLOC_FL_KEEP_SIZE)) */
	{
		perror("FALLOC_FL_PUNCH_HOLE unsupported");
		close(fd);
		return (1);
	}
#endif /* defined(FALLOC_FL_PUNCH_HOLE) && defined(FALLOC_FL_KEEP_SIZE) */
	close(fd);
	return (0);
}
