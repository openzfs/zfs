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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include "file_common.h"

static unsigned char bigbuffer[BIGBUFFERSIZE];

/*
 * Given a filename, check that the file consists entirely
 * of a particular pattern. If the pattern is not specified a
 * default will be used. For default values see file_common.h
 */
int
main(int argc, char **argv)
{
	int		bigfd;
	long		i, n;
	unsigned char	fillchar = DATA;
	int		bigbuffersize = BIGBUFFERSIZE;

	/*
	 * Validate arguments
	 */
	if (argc < 2) {
		(void) printf("Usage: %s filename [pattern]\n",
		    argv[0]);
		exit(1);
	}

	if (argv[2]) {
		fillchar = atoi(argv[2]);
	}

	/*
	 * Read the file contents and check every character
	 * against the supplied pattern. Abort if the
	 * pattern check fails.
	 */
	if ((bigfd = open(argv[1], O_RDONLY)) == -1) {
		(void) printf("open %s failed %d\n", argv[1], errno);
		exit(1);
	}

	do {
		if ((n = read(bigfd, &bigbuffer, bigbuffersize)) == -1) {
			(void) printf("read failed (%ld), %d\n", n, errno);
			exit(errno);
		}

		for (i = 0; i < n; i++) {
			if (bigbuffer[i] != fillchar) {
				(void) printf("error %s: 0x%x != 0x%x)\n",
				    argv[1], bigbuffer[i], fillchar);
				exit(1);
			}
		}
	} while (n == bigbuffersize);

	return (0);
}
