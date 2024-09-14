/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * Copyright (c) 2022 by Triad National Security, LLC.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <assert.h>

#ifndef MIN
#define	MIN(a, b)	((a) < (b)) ? (a) : (b)
#endif

static char *outputfile = NULL;
static int blocksize = 131072; /* 128K */
static int wr_err_expected = 0;
static int numblocks = 100;
static char *execname = NULL;
static int print_usage = 0;
static int randompattern = 0;
static int ofd;
char *buf = NULL;

typedef struct {
	int entire_file_written;
} pthread_args_t;

static void
usage(void)
{
	(void) fprintf(stderr,
	    "usage %s -o outputfile [-b blocksize] [-e wr_error_expected]\n"
	    "         [-n numblocks] [-p randpattern] [-h help]\n"
	    "\n"
	    "Testing whether checksum verify works correctly for O_DIRECT.\n"
	    "when manipulating the contents of a userspace buffer.\n"
	    "\n"
	    "    outputfile:      File to write to.\n"
	    "    blocksize:       Size of each block to write (must be at \n"
	    "                     least >= 512).\n"
	    "    wr_err_expected: Whether pwrite() is expected to return EIO\n"
	    "                     while manipulating the contents of the\n"
	    "                     buffer.\n"
	    "    numblocks:       Total number of blocksized blocks to\n"
	    "                     write.\n"
	    "    randpattern:     Fill data buffer with random data. Default\n"
	    "                     behavior is to fill the buffer with the \n"
	    "                     known data pattern (0xdeadbeef).\n"
	    "    help:           Print usage information and exit.\n"
	    "\n"
	    "    Required parameters:\n"
	    "    outputfile\n"
	    "\n"
	    "    Default Values:\n"
	    "    blocksize       -> 131072\n"
	    "    wr_err_expexted -> false\n"
	    "    numblocks       -> 100\n"
	    "    randpattern     -> false\n",
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
	execname = argv[0];

	while ((c = getopt(argc, argv, "b:ehn:o:p")) != -1) {
		switch (c) {
			case 'b':
				blocksize = atoi(optarg);
				break;

			case 'e':
				wr_err_expected = 1;
				break;

			case 'h':
				print_usage = 1;
				break;

			case 'n':
				numblocks = atoi(optarg);
				break;

			case 'o':
				outputfile = optarg;
				break;

			case 'p':
				randompattern = 1;
				break;

			case ':':
				(void) fprintf(stderr,
				    "Option -%c requires an opertand\n",
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

	if (errflag || print_usage == 1)
		(void) usage();

	if (blocksize < 512 || outputfile == NULL || numblocks <= 0) {
		(void) fprintf(stderr,
		    "Required paramater(s) missing or invalid.\n");
		(void) usage();
	}
}

/*
 * Write blocksize * numblocks to the file using O_DIRECT.
 */
static void *
write_thread(void *arg)
{
	size_t offset = 0;
	int total_data = blocksize * numblocks;
	int left = total_data;
	ssize_t wrote = 0;
	pthread_args_t *args = (pthread_args_t *)arg;

	while (!args->entire_file_written) {
		wrote = pwrite(ofd, buf, blocksize, offset);
		if (wrote != blocksize) {
			if (wr_err_expected)
				assert(errno == EIO);
			else
				exit(2);
		}

		offset = ((offset + blocksize) % total_data);
		left -= blocksize;

		if (left == 0)
			args->entire_file_written = 1;
	}

	pthread_exit(NULL);
}

/*
 * Update the buffers contents with random data.
 */
static void *
manipulate_buf_thread(void *arg)
{
	size_t rand_offset;
	char rand_char;
	pthread_args_t *args = (pthread_args_t *)arg;

	while (!args->entire_file_written) {
		rand_offset = (rand() % blocksize);
		rand_char = (rand() % (126 - 33) + 33);
		buf[rand_offset] = rand_char;
	}

	pthread_exit(NULL);
}

int
main(int argc, char *argv[])
{
	const char *datapattern = "0xdeadbeef";
	int ofd_flags = O_WRONLY | O_CREAT | O_DIRECT;
	mode_t mode = S_IRUSR | S_IWUSR;
	pthread_t write_thr;
	pthread_t manipul_thr;
	int left = blocksize;
	int offset = 0;
	int rc;
	pthread_args_t args = { 0 };

	parse_options(argc, argv);

	ofd = open(outputfile, ofd_flags, mode);
	if (ofd == -1) {
		(void) fprintf(stderr, "%s, %s\n", execname, outputfile);
		perror("open");
		exit(2);
	}

	int err = posix_memalign((void **)&buf, sysconf(_SC_PAGE_SIZE),
	    blocksize);
	if (err != 0) {
		(void) fprintf(stderr,
		    "%s: %s\n", execname, strerror(err));
		exit(2);
	}

	if (!randompattern) {
		/* Putting known data pattern in buffer */
		while (left) {
			size_t amt = MIN(strlen(datapattern), left);
			memcpy(&buf[offset], datapattern, amt);
			offset += amt;
			left -= amt;
		}
	} else {
		/* Putting random data in buffer */
		for (int i = 0; i < blocksize; i++)
			buf[i] = rand();
	}

	/*
	 * Writing using O_DIRECT while manipulating the buffer contents until
	 * the entire file is written.
	 */
	if ((rc = pthread_create(&manipul_thr, NULL, manipulate_buf_thread,
	    &args))) {
		fprintf(stderr, "error: pthreads_create, manipul_thr, "
		    "rc: %d\n", rc);
		exit(2);
	}

	if ((rc = pthread_create(&write_thr, NULL, write_thread, &args))) {
		fprintf(stderr, "error: pthreads_create, write_thr, "
		    "rc: %d\n", rc);
		exit(2);
	}

	pthread_join(write_thr, NULL);
	pthread_join(manipul_thr, NULL);

	assert(args.entire_file_written == 1);

	(void) close(ofd);

	free(buf);

	return (0);
}
