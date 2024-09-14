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

static int alignment = 0;
static int bsize = 0;
static int count = 0;
static char *ifile = NULL;
static char *ofile = NULL;
static off_t stride = 1;
static off_t seek = 0;
static int seekbytes = 0;
static int if_o_direct = 0;
static int of_o_direct = 0;
static int skip = 0;
static int skipbytes = 0;
static int entire_file = 0;
static const char *execname = "stride_dd";

static void usage(void);
static void parse_options(int argc, char *argv[]);

static void
usage(void)
{
	(void) fprintf(stderr,
	    "usage: %s -i inputfile -o outputfile -b blocksize [-c count]\n"
	    "           [-s stride] [-k seekblocks] [-K seekbytes]\n"
	    "           [-a alignment] [-d if_o_direct] [-D of_o_direct]\n"
	    "           [-p skipblocks] [-P skipbytes] [-e entire_file]\n"
	    "\n"
	    "Simplified version of dd that supports the stride option.\n"
	    "A stride of n means that for each block written, n - 1 blocks\n"
	    "are skipped in both the input and output file. A stride of 1\n"
	    "means that blocks are read and written consecutively.\n"
	    "All numeric parameters must be integers.\n"
	    "\n"
	    "    inputfile:   File to read from\n"
	    "    outputfile:  File to write to\n"
	    "    blocksize:   Size of each block to read/write\n"
	    "    count:       Number of blocks to read/write (Required"
	    " unless -e is used)\n"
	    "    stride:      Read/write a block then skip (stride - 1) blocks"
	    "\n"
	    "    seekblocks:  Number of blocks to skip at start of output\n"
	    "    seekbytes:   Treat seekblocks as byte count\n"
	    "    alignment:   Alignment passed to posix_memalign() (default"
	    " PAGE_SIZE)\n"
	    "    if_o_direct: Use O_DIRECT with inputfile (default no O_DIRECT)"
	    "\n"
	    "    of_o_direct: Use O_DIRECT with outputfile (default no "
	    " O_DIRECT)\n"
	    "    skipblocks:  Number of blocks to skip at start of input "
	    " (default 0)\n"
	    "    skipbytes:   Treat skipblocks as byte count\n"
	    "    entire_file: When used the entire inputfile will be read and"
	    " count will be ignored\n",
	    execname);
	(void) exit(1);
}

/*
 * posix_memalign() only allows for alignments which are postive, powers of two
 * and a multiple of sizeof (void *).
 */
static int
invalid_alignment(int alignment)
{
	if ((alignment < 0) || (alignment & (alignment - 1)) ||
	    ((alignment % sizeof (void *)))) {
		(void) fprintf(stderr,
		    "Alignment must be a postive, power of two, and multiple "
		    "of sizeof (void *).\n");
		return (1);
	}
	return (0);
}

static void
parse_options(int argc, char *argv[])
{
	int c;
	int errflag = 0;

	execname = argv[0];
	alignment = sysconf(_SC_PAGE_SIZE);

	extern char *optarg;
	extern int optind, optopt;

	while ((c = getopt(argc, argv, "a:b:c:deDi:o:s:k:Kp:P")) != -1) {
		switch (c) {
			case 'a':
				alignment = atoi(optarg);
				break;

			case 'b':
				bsize = atoi(optarg);
				break;

			case 'c':
				count = atoi(optarg);
				break;

			case 'd':
				if_o_direct = 1;
				break;

			case 'e':
				entire_file = 1;
				break;

			case 'D':
				of_o_direct = 1;
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

			case 'K':
				seekbytes = 1;
				break;

			case 'p':
				skip = atoi(optarg);
				break;

			case 'P':
				skipbytes = 1;
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

	if (bsize <= 0 || stride <= 0 || ifile == NULL || ofile == NULL ||
	    seek < 0 || invalid_alignment(alignment) || skip < 0) {
		(void) fprintf(stderr,
		    "Required parameter(s) missing or invalid.\n");
		(void) usage();
	}

	if (count <= 0 && entire_file == 0) {
		(void) fprintf(stderr,
		    "Required parameter(s) missing or invalid.\n");
		(void) usage();
	}
}

static void
read_entire_file(int ifd, int ofd, void *buf)
{
	int c;

	do {
		c = read(ifd, buf, bsize);
		if (c < 0) {
			perror("read");
			exit(2);
		} else if (c != 0) {
			c = write(ofd, buf, bsize);
			if (c < 0) {
				perror("write");
				exit(2);
			}

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
	} while (c != 0);
}

static void
read_on_count(int ifd, int ofd, void *buf)
{
	int i;
	int c;

	for (i = 0; i < count; i++) {
		c = read(ifd, buf, bsize);
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
}

int
main(int argc, char *argv[])
{
	int ifd;
	int ofd;
	int ifd_flags = O_RDONLY;
	int ofd_flags = O_WRONLY | O_CREAT;
	void *buf;

	parse_options(argc, argv);

	if (if_o_direct)
		ifd_flags |= O_DIRECT;

	if (of_o_direct)
		ofd_flags |= O_DIRECT;

	ifd = open(ifile, ifd_flags);
	if (ifd == -1) {
		(void) fprintf(stderr, "%s: %s: ", execname, ifile);
		perror("open");
		exit(2);
	}

	ofd = open(ofile, ofd_flags, 0666);
	if (ofd == -1) {
		(void) fprintf(stderr, "%s: %s: ", execname, ofile);
		perror("open");
		exit(2);
	}

	/*
	 * We use valloc because some character block devices expect a
	 * page-aligned buffer.
	 */
	int err = posix_memalign(&buf, alignment, bsize);
	if (err != 0) {
		(void) fprintf(stderr,
		    "%s: %s\n", execname, strerror(err));
		exit(2);
	}

	if (skip > 0) {
		int skipamt = skipbytes == 1 ? skip : skip * bsize;
		if (lseek(ifd, skipamt, SEEK_CUR) == -1) {
			perror("input lseek");
			exit(2);
		}
	}

	if (seek > 0) {
		int seekamt = seekbytes == 1 ? seek : seek * bsize;
		if (lseek(ofd, seekamt, SEEK_CUR) == -1) {
			perror("output lseek");
			exit(2);
		}
	}

	if (entire_file == 1)
		read_entire_file(ifd, ofd, buf);
	else
		read_on_count(ifd, ofd, buf);

	free(buf);

	(void) close(ofd);
	(void) close(ifd);

	return (0);
}
