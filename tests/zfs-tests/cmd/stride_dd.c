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

#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

static int bsize = 0;
static int count = 0;
static char *ifile = NULL;
static char *ofile = NULL;
static off_t stride = 0;
static off_t seek = 0;
static const char *execname = "stride_dd";

static void usage(void);
static void parse_options(int argc, char *argv[]);

static void
usage(void)
{
	(void) fprintf(stderr,
	    "usage: %s -i inputfile -o outputfile -b blocksize -c count \n"
	    "           -s stride [ -k seekblocks]\n"
	    "\n"
	    "Simplified version of dd that supports the stride option.\n"
	    "A stride of n means that for each block written, n - 1 blocks\n"
	    "are skipped in both the input and output file. A stride of 1\n"
	    "means that blocks are read and written consecutively.\n"
	    "All numeric parameters must be integers.\n"
	    "\n"
	    "    inputfile:  File to read from\n"
	    "    outputfile: File to write to\n"
	    "    blocksize:  Size of each block to read/write\n"
	    "    count:      Number of blocks to read/write\n"
	    "    stride:     Read/write a block then skip (stride - 1) blocks\n"
	    "    seekblocks: Number of blocks to skip at start of output\n",
	    execname);
	(void) exit(1);
}

static void
parse_options(int argc, char *argv[])
{
	int c;
	int errflag = 0;

	execname = argv[0];

	extern char *optarg;
	extern int optind, optopt;

	while ((c = getopt(argc, argv, ":b:c:i:o:s:k:")) != -1) {
		switch (c) {
			case 'b':
				bsize = atoi(optarg);
				break;

			case 'c':
				count = atoi(optarg);
				break;

			case 'i':
				ifile = optarg;
				break;

			case 'o':
				ofile = optarg;
				break;

			case 's':
				stride = atoi(optarg);
				break;

			case 'k':
				seek = atoi(optarg);
				break;

			case ':':
				(void) fprintf(stderr,
				    "Option -%c requires an operand\n", optopt);
				errflag++;
				break;

			case '?':
			default:
				(void) fprintf(stderr,
				    "Unrecognized option: -%c\n", optopt);
				errflag++;
				break;
		}

		if (errflag) {
			(void) usage();
		}
	}

	if (bsize <= 0 || count <= 0 || stride <= 0 || ifile == NULL ||
	    ofile == NULL || seek < 0) {
		(void) fprintf(stderr,
		    "Required parameter(s) missing or invalid.\n");
		(void) usage();
	}
}

int
main(int argc, char *argv[])
{
	int i;
	int ifd;
	int ofd;
	void *buf;
	int c;

	parse_options(argc, argv);

	ifd = open(ifile, O_RDONLY);
	if (ifd == -1) {
		(void) fprintf(stderr, "%s: %s: ", execname, ifile);
		perror("open");
		exit(2);
	}

	ofd = open(ofile, O_WRONLY | O_CREAT, 0666);
	if (ofd == -1) {
		(void) fprintf(stderr, "%s: %s: ", execname, ofile);
		perror("open");
		exit(2);
	}

	/*
	 * We use valloc because some character block devices expect a
	 * page-aligned buffer.
	 */
	int err = posix_memalign(&buf, 4096, bsize);
	if (err != 0) {
		(void) fprintf(stderr,
		    "%s: %s\n", execname, strerror(err));
		exit(2);
	}

	if (seek > 0) {
		if (lseek(ofd, seek * bsize, SEEK_CUR) == -1) {
			perror("output lseek");
			exit(2);
		}
	}

	for (i = 0; i < count; i++) {
		c = read(ifd, buf, bsize);
		if (c != bsize) {

			perror("read");
			exit(2);
		}
		if (c != bsize) {
			if (c < 0) {
				perror("read");
			} else {
				(void) fprintf(stderr,
				    "%s: unexpected short read, read %d "
				    "bytes, expected %d\n", execname,
				    c, bsize);
			}
			exit(2);
		}

		c = write(ofd, buf, bsize);
		if (c != bsize) {
			if (c < 0) {
				perror("write");
			} else {
				(void) fprintf(stderr,
				    "%s: unexpected short write, wrote %d "
				    "bytes, expected %d\n", execname,
				    c, bsize);
			}
			exit(2);
		}

		if (stride > 1) {
			if (lseek(ifd, (stride - 1) * bsize, SEEK_CUR) == -1) {
				perror("input lseek");
				exit(2);
			}
			if (lseek(ofd, (stride - 1) * bsize, SEEK_CUR) == -1) {
				perror("output lseek");
				exit(2);
			}
		}
	}
	free(buf);

	(void) close(ofd);
	(void) close(ifd);

	return (0);
}
