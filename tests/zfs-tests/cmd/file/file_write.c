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
#include <libgen.h>
#include <string.h>
#include <inttypes.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>

static unsigned char bigbuffer[BIGBUFFERSIZE];

/*
 * Writes (or appends) a given value to a file repeatedly.
 * See header file for defaults.
 */

static void usage(char *);

/*
 * pseudo-randomize the buffer
 */
static void randomize_buffer(int block_size) {
	int i;
	char rnd = rand() & 0xff;
	for (i = 0; i < block_size; i++)
		bigbuffer[i] ^= rnd;
}

int
main(int argc, char **argv)
{
	int		bigfd;
	int		c;
	int		oflag = 0;
	int		err = 0;
	int		k;
	long		i;
	int64_t		good_writes = 0;
	uchar_t		nxtfillchar;
	char		*prog = argv[0];
	/*
	 * Default Parameters
	 */
	int		write_count = BIGFILESIZE;
	uchar_t		fillchar = DATA;
	int		block_size = BLOCKSZ;
	char		*filename = NULL;
	char		*operation = NULL;
	offset_t	noffset, offset = 0;
	int		verbose = 0;
	int		rsync = 0;
	int		wsync = 0;

	/*
	 * Process Arguments
	 */
	while ((c = getopt(argc, argv, "b:c:d:s:f:o:vwr")) != -1) {
		switch (c) {
			case 'b':
				block_size = atoi(optarg);
				break;
			case 'c':
				write_count = atoi(optarg);
				break;
			case 'd':
				if (optarg[0] == 'R')
					fillchar = 'R'; /* R = random data */
				else
					fillchar = atoi(optarg);
				break;
			case 's':
				offset = atoll(optarg);
				break;
			case 'f':
				filename = optarg;
				break;
			case 'o':
				operation = optarg;
				break;
			case 'v':
				verbose = 1;
				break;
			case 'w':
				wsync = 1;
				break;
			case 'r':
				rsync = 1;
				break;
			case '?':
				(void) printf("unknown arg %c\n", optopt);
				usage(prog);
				break;
		}
	}

	/*
	 * Validate Parameters
	 */
	if (!filename) {
		(void) printf("Filename not specified (-f <file>)\n");
		err++;
	}

	if (!operation) {
		(void) printf("Operation not specified (-o <operation>).\n");
		err++;
	}

	if (block_size > BIGBUFFERSIZE) {
		(void) printf("block_size is too large max==%d.\n",
		    BIGBUFFERSIZE);
		err++;
	}

	if (err) {
		usage(prog); /* no return */
		return (1);
	}

	/*
	 * Prepare the buffer and determine the requested operation
	 */
	nxtfillchar = fillchar;
	k = 0;

	if (fillchar == 'R')
		srand(time(NULL));

	for (i = 0; i < block_size; i++) {
		bigbuffer[i] = nxtfillchar;

		if (fillchar == 0) {
			if ((k % DATA_RANGE) == 0) {
				k = 0;
			}
			nxtfillchar = k++;
		} else if (fillchar == 'R') {
			nxtfillchar = rand() & 0xff;
		}
	}

	/*
	 * using the strncmp of operation will make the operation match the
	 * first shortest match - as the operations are unique from the first
	 * character this means that we match single character operations
	 */
	if ((strncmp(operation, "create", strlen(operation) + 1)) == 0 ||
	    (strncmp(operation, "overwrite", strlen(operation) + 1)) == 0) {
		oflag = (O_RDWR|O_CREAT);
	} else if ((strncmp(operation, "append", strlen(operation) + 1)) == 0) {
		oflag = (O_RDWR|O_APPEND);
	} else {
		(void) printf("valid operations are <create|append> not '%s'\n",
		    operation);
		usage(prog);
	}

	if (rsync) {
		oflag = oflag | O_RSYNC;
	}

	if (wsync) {
		oflag = oflag | O_SYNC;
	}

	/*
	 * Given an operation (create/overwrite/append), open the file
	 * accordingly and perform a write of the appropriate type.
	 */
	if ((bigfd = open(filename, oflag, 0666)) == -1) {
		(void) printf("open %s: failed [%s]%d. Aborting!\n", filename,
		    strerror(errno), errno);
		exit(errno);
	}
	noffset = lseek64(bigfd, offset, SEEK_SET);
	if (noffset != offset) {
		(void) printf("llseek %s (%lld/%lld) failed [%s]%d.Aborting!\n",
		    filename, offset, noffset, strerror(errno), errno);
		exit(errno);
	}

	if (verbose) {
		(void) printf("%s: block_size = %d, write_count = %d, "
		    "offset = %lld, ", filename, block_size,
		    write_count, offset);
		if (fillchar == 'R') {
			(void) printf("data = [random]\n");
		} else {
			(void) printf("data = %s%d\n",
			    (fillchar == 0) ? "0->" : "",
			    (fillchar == 0) ? DATA_RANGE : fillchar);
		}
	}

	for (i = 0; i < write_count; i++) {
		ssize_t n;
		if (fillchar == 'R')
			randomize_buffer(block_size);

		if ((n = write(bigfd, &bigbuffer, block_size)) == -1) {
			(void) printf("write failed (%ld), good_writes = %"
			    PRId64 ", " "error: %s[%d]\n",
			    (long)n, good_writes,
			    strerror(errno),
			    errno);
			exit(errno);
		}
		good_writes++;
	}

	if (verbose) {
		(void) printf("Success: good_writes = %" PRId64 "(%"
		    PRId64 ")\n", good_writes, (good_writes * block_size));
	}

	return (0);
}

static void
usage(char *prog)
{
	(void) printf("Usage: %s [-v] -o {create,overwrite,append} -f file_name"
	    " [-b block_size]\n"
	    "\t[-s offset] [-c write_count] [-d data]\n\n"
	    "Where [data] equal to zero causes chars "
	    "0->%d to be repeated throughout, or [data]\n"
	    "equal to 'R' for pseudorandom data.\n",
	    prog, DATA_RANGE);

	exit(1);
}
