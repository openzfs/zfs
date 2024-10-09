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
 * Copyright (c) 2024 by Triad National Security, LLC.
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

static char *filename = NULL;
static int blocksize = 131072; /* 128K */
static int err_expected = 0;
static int read_op = 0;
static int write_op = 0;
static int numblocks = 100;
static char *execname = NULL;
static int print_usage = 0;
static int randompattern = 0;
static int fd;
char *buf = NULL;

typedef struct {
	int entire_file_completed;
} pthread_args_t;

static void
usage(void)
{
	(void) fprintf(stderr,
	    "usage %s -f filename [-b blocksize] [-e wr_error_expected]\n"
	    "         [-n numblocks] [-p randompattern] -r read_op \n"
	    "         -w write_op [-h help]\n"
	    "\n"
	    "Testing whether checksum verify works correctly for O_DIRECT.\n"
	    "when manipulating the contents of a userspace buffer.\n"
	    "\n"
	    "    filename:       File to read or write to.\n"
	    "    blocksize:      Size of each block to write (must be at \n"
	    "                    least >= 512).\n"
	    "    err_expected:   Whether write() is expected to return EIO\n"
	    "                    while manipulating the contents of the\n"
	    "                    buffer.\n"
	    "    numblocks:      Total number of blocksized blocks to\n"
	    "                    write.\n"
	    "    read_op:        Perform reads to the filename file while\n"
	    "                    while manipulating the buffer contents\n"
	    "    write_op:       Perform writes to the filename file while\n"
	    "                    manipulating the buffer contents\n"
	    "    randompattern:  Fill data buffer with random data for \n"
	    "                    writes. Default behavior is to fill the \n"
	    "                    buffer with known data pattern (0xdeadbeef)\n"
	    "    help:           Print usage information and exit.\n"
	    "\n"
	    "    Required parameters:\n"
	    "    filename\n"
	    "    read_op or write_op\n"
	    "\n"
	    "    Default Values:\n"
	    "    blocksize       -> 131072\n"
	    "    wr_err_expexted -> false\n"
	    "    numblocks       -> 100\n"
	    "    randompattern   -> false\n",
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

	while ((c = getopt(argc, argv, "b:ef:hn:rw")) != -1) {
		switch (c) {
			case 'b':
				blocksize = atoi(optarg);
				break;

			case 'e':
				err_expected = 1;
				break;

			case 'f':
				filename = optarg;
				break;


			case 'h':
				print_usage = 1;
				break;

			case 'n':
				numblocks = atoi(optarg);
				break;

			case 'r':
				read_op = 1;
				break;

			case 'w':
				write_op = 1;
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

	if (blocksize < 512 || filename == NULL || numblocks <= 0 ||
	    (read_op == 0 && write_op == 0)) {
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

	while (!args->entire_file_completed) {
		wrote = pwrite(fd, buf, blocksize, offset);
		if (wrote != blocksize) {
			if (err_expected)
				assert(errno == EIO);
			else
				exit(2);
		}

		offset = ((offset + blocksize) % total_data);
		left -= blocksize;

		if (left == 0)
			args->entire_file_completed = 1;
	}

	pthread_exit(NULL);
}

/*
 * Read blocksize * numblocks to the file using O_DIRECT.
 */
static void *
read_thread(void *arg)
{
	size_t offset = 0;
	int total_data = blocksize * numblocks;
	int left = total_data;
	ssize_t read = 0;
	pthread_args_t *args = (pthread_args_t *)arg;

	while (!args->entire_file_completed) {
		read = pread(fd, buf, blocksize, offset);
		if (read != blocksize) {
			exit(2);
		}

		offset = ((offset + blocksize) % total_data);
		left -= blocksize;

		if (left == 0)
			args->entire_file_completed = 1;
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

	while (!args->entire_file_completed) {
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
	int fd_flags = O_DIRECT;
	mode_t mode = S_IRUSR | S_IWUSR;
	pthread_t io_thr;
	pthread_t manipul_thr;
	int left = blocksize;
	int offset = 0;
	int rc;
	pthread_args_t args = { 0 };

	parse_options(argc, argv);

	if (write_op) {
		fd_flags |= (O_WRONLY | O_CREAT);
	} else {
		fd_flags |= O_RDONLY;
	}

	fd = open(filename, fd_flags, mode);
	if (fd == -1) {
		(void) fprintf(stderr, "%s, %s\n", execname, filename);
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

	if (write_op) {
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
	}

	if ((rc = pthread_create(&manipul_thr, NULL, manipulate_buf_thread,
	    &args))) {
		fprintf(stderr, "error: pthreads_create, manipul_thr, "
		    "rc: %d\n", rc);
		exit(2);
	}

	if (write_op) {
		/*
		 * Writing using O_DIRECT while manipulating the buffer contents
		 * until the entire file is written.
		 */
		if ((rc = pthread_create(&io_thr, NULL, write_thread, &args))) {
			fprintf(stderr, "error: pthreads_create, io_thr, "
			    "rc: %d\n", rc);
			exit(2);
		}
	} else {
		/*
		 * Reading using O_DIRECT while manipulating the buffer contents
		 * until the entire file is read.
		 */
		if ((rc = pthread_create(&io_thr, NULL, read_thread, &args))) {
			fprintf(stderr, "error: pthreads_create, io_thr, "
			    "rc: %d\n", rc);
			exit(2);
		}
	}

	pthread_join(io_thr, NULL);
	pthread_join(manipul_thr, NULL);

	assert(args.entire_file_completed == 1);

	(void) close(fd);

	free(buf);

	return (0);
}
