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
 * Copyright (c) 2018 by Delphix. All rights reserved.
 */

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

static void
usage(const char *msg, int exit_value)
{
	(void) fprintf(stderr, "usage: get_diff file redacted_file\n%s\n", msg);
	exit(exit_value);
}

/*
 * This utility compares two files, an original and its redacted counterpart
 * (in that order). It compares the files 512 bytes at a time, printing out
 * any ranges (as offset and length) where the redacted file does not match
 * the original. This output is used to verify that the expected ranges of
 * a redacted file do not contain the original data.
 */
int
main(int argc, char *argv[])
{
	off_t		diff_off = 0, diff_len = 0, off = 0;
	int		fd1, fd2;
	char		*fname1, *fname2;
	char		buf1[DEV_BSIZE], buf2[DEV_BSIZE];
	ssize_t		bytes;

	if (argc != 3)
		usage("Incorrect number of arguments.", 1);

	if ((fname1 = argv[1]) == NULL)
		usage("Filename missing.", 1);
	if ((fd1 = open(fname1, O_LARGEFILE | O_RDONLY)) < 0) {
		perror("open1 failed");
		exit(1);
	}

	if ((fname2 = argv[2]) == NULL)
		usage("Redacted filename missing.", 1);
	if ((fd2 = open(fname2, O_LARGEFILE | O_RDONLY)) < 0) {
		perror("open2 failed");
		exit(1);
	}

	while ((bytes = pread(fd1, buf1, DEV_BSIZE, off)) > 0) {
		if (pread(fd2, buf2, DEV_BSIZE, off) < 0) {
			if (errno == EIO) {
				/*
				 * A read in a redacted section of a file will
				 * fail with EIO. If we get EIO, continue on
				 * but ensure that a comparison of buf1 and
				 * buf2 will fail, indicating a redacted block.
				 */
				buf2[0] = ~buf1[0];
			} else {
				perror("pread failed");
				exit(1);
			}
		}
		if (memcmp(buf1, buf2, bytes) == 0) {
			if (diff_len != 0) {
				(void) fprintf(stdout, "%lld,%lld\n",
				    (long long)diff_off, (long long)diff_len);
				assert(off == diff_off + diff_len);
				diff_len = 0;
			}
			diff_off = 0;
		} else {
			if (diff_len == 0)
				diff_off = off;
			assert(off == diff_off + diff_len);
			diff_len += bytes;
		}
		off += bytes;
	}

	if (diff_len != 0) {
		(void) fprintf(stdout, "%lld,%lld\n", (long long)diff_off,
		    (long long)diff_len);
	}

	(void) close(fd1);
	(void) close(fd2);

	return (0);
}
