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
 * Copyright (c) 2022 by Triad National Security, LLC
 */

#include "file_common.h"
#include <unistd.h>
#include <sys/sysmacros.h>

static char *filename = NULL;
static int expected_offset = -1;
static int blocksize = 131072; /* 128KiB */
static int numblocks = 8;
static const char *execname = "file_append";
static int use_odirect = 0;

static void
usage(void)
{
	(void) fprintf(stderr,
	    "usage %s -f filename -e expected_offset [-b blocksize] \n"
	    "         [-n numblocks] [-d use_odirect] [-h help]\n"
	    "\n"
	    "Opens a file using O_APPEND and writes numblocks blocksize\n"
	    "blocks to filename.\n"
	    "Checks if expected_offst == lseek(fd, 0, SEEK_CUR)).\n"
	    "\n"
	    "    filename:         File to open with O_APPEND and write to.\n"
	    "    expected_offset:  Expected file offset after writing\n"
	    "                      blocksize numblocks to filename\n"
	    "    blocksize:        Size of each block to writei (must be at\n"
	    "                      least >= 512). If using use_odirect (-d)\n"
	    "                      must be a mutltiple of _SC_PAGE_SIZE\n"
	    "    numblocks:        Total number of blocksized blocks to\n"
	    "                      write.\n"
	    "    use_odirect:      Open file using O_DIRECT.\n"
	    "    help:             Print usage information and exit.\n"
	    "\n"
	    "    Required parameters:\n"
	    "    filename\n"
	    "    expected_offset\n"
	    "\n"
	    "    Default values:\n"
	    "    blocksize   -> 131072 (128 KiB)\n"
	    "    numblocks   -> 8\n"
	    "    use_odirect -> False\n",
	    execname);
	(void) exit(1);
}

static void
parse_options(int argc, char *argv[])
{
	int c;
	int errflag = 0;
	extern char *optarg;
	extern int optind, optopt;

	while ((c = getopt(argc, argv, "b:de:f:hn:")) != -1) {
		switch (c) {
			case 'b':
				blocksize = atoi(optarg);
				break;
			case 'd':
				use_odirect = 1;
				break;
			case 'e':
				expected_offset = atoi(optarg);
				break;
			case 'f':
				filename = optarg;
				break;
			case 'h':
				(void) usage();
				break;
			case 'n':
				numblocks = atoi(optarg);
				break;
			case ':':
				(void) fprintf(stderr,
				    "Option -%c requires an operand\n",
				    optopt);
				errflag++;
				break;
			case '?':
			default:
				(void) fprintf(stderr,
				    "Unrecognized option: -%c\n", optopt);
				errflag++;
				break;
		}
	}

	if (errflag)
		(void) usage();

	if (use_odirect && ((blocksize % sysconf(_SC_PAGE_SIZE)) != 0)) {
		(void) fprintf(stderr,
		    "blocksize parameter invalid when using O_DIRECT.\n");
		(void) usage();
	}

	if (blocksize < 512 || expected_offset < 0 || filename == NULL ||
	    numblocks <= 0) {
		(void) fprintf(stderr,
		    "Required parameters(s) missing or invalid value for "
		    "parameter.\n");
		(void) usage();
	}
}

int
main(int argc, char *argv[])
{
	int		err;
	const char	*datapattern = "0xf00ba3";
	int		fd = -1;
	int		fd_flags = O_WRONLY | O_CREAT | O_APPEND;
	int		buf_offset = 0;
	char		*buf;

	parse_options(argc, argv);

	if (use_odirect)
		fd_flags |= O_DIRECT;

	fd = open(filename, fd_flags, 0666);
	if (fd == -1) {
		(void) fprintf(stderr, "%s: %s: ", execname, filename);
		perror("open");
		(void) exit(2);
	}

	err = posix_memalign((void **)&buf, sysconf(_SC_PAGE_SIZE),
	    blocksize);

	if (err != 0) {
		(void) fprintf(stderr,
		    "%s: %s\n", execname, strerror(err));
		(void) exit(2);
	}

	/* Putting known data pattern in buffer */
	int left = blocksize;
	while (left) {
		size_t amt = MIN(strlen(datapattern), left);
		memcpy(&buf[buf_offset], datapattern, amt);
		buf_offset += amt;
		left -= amt;
	}

	for (int i = 0; i < numblocks; i++) {
		int wrote = write(fd, buf, blocksize);

		if (wrote != blocksize) {
			if (wrote < 0) {
				perror("write");
			} else {
				(void) fprintf(stderr,
				    "%s: unexpected short write, wrote %d "
				    "byte, expected %d\n", execname, wrote,
				    blocksize);
			}
			(void) exit(2);
		}
	}

	/* Getting current file offset */
	off_t off = lseek(fd, 0, SEEK_CUR);

	if (off == -1) {
		perror("output seek");
		(void) exit(2);
	} else if (off != expected_offset) {
		(void) fprintf(stderr,
		    "%s: expected offset %d but current offset in %s is set "
		    "to %ld\n", execname, expected_offset, filename,
		    (long int)off);
		(void) exit(2);
	}

	(void) close(fd);
	free(buf);

	return (0);
}
