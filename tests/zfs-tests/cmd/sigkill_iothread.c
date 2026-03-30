// SPDX-License-Identifier: CDDL-1.0
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
 * Multi-threaded I/O program for testing SIGKILL safety during ZFS I/O.
 *
 * Creates multiple threads that continuously read from or write to a file.
 * Designed to be killed with SIGKILL while threads are actively performing
 * I/O, to verify that the kernel does not panic in usercopy checks when
 * the address space is torn down while ZFS I/O is in flight.
 *
 * See openzfs/zfs#15918.
 *
 * Usage: sigkill_iothread <file> <r|w|rw> [nthreads]
 */

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <err.h>

#define	IO_BUF_SIZE	(128 * 1024)	/* 128K */

struct thread_arg {
	const char *file;
	int do_write;
};

static void *
io_thread(void *arg)
{
	struct thread_arg *ta = arg;
	char buf[IO_BUF_SIZE];
	int flags, fd;

	(void) memset(buf, 'A', sizeof (buf));

	flags = ta->do_write ? (O_RDWR | O_CREAT) : O_RDONLY;
	fd = open(ta->file, flags, 0666);
	if (fd == -1)
		err(1, "open(%s)", ta->file);

	for (;;) {
		if (lseek(fd, 0, SEEK_SET) == -1)
			break;
		if (ta->do_write)
			(void) write(fd, buf, sizeof (buf));
		else
			(void) read(fd, buf, sizeof (buf));
	}

	(void) close(fd);
	return (NULL);
}

int
main(int argc, char **argv)
{
	int nthreads = 4;
	int do_read = 0, do_write = 0;

	if (argc < 3) {
		(void) fprintf(stderr,
		    "usage: %s <file> <r|w|rw> [nthreads]\n", argv[0]);
		exit(1);
	}

	if (strcmp(argv[2], "r") == 0) {
		do_read = 1;
	} else if (strcmp(argv[2], "w") == 0) {
		do_write = 1;
	} else if (strcmp(argv[2], "rw") == 0) {
		do_read = 1;
		do_write = 1;
	} else {
		(void) fprintf(stderr, "mode must be 'r', 'w', or 'rw'\n");
		exit(1);
	}

	if (argc >= 4)
		nthreads = atoi(argv[3]);

	if (nthreads < 1 || nthreads > 64) {
		(void) fprintf(stderr,
		    "nthreads must be between 1 and 64\n");
		exit(1);
	}

	pthread_t *tids = calloc(nthreads, sizeof (pthread_t));
	struct thread_arg *args = calloc(nthreads, sizeof (struct thread_arg));

	if (tids == NULL || args == NULL)
		err(1, "calloc");

	for (int i = 0; i < nthreads; i++) {
		args[i].file = argv[1];
		if (do_read && do_write)
			args[i].do_write = (i % 2);
		else
			args[i].do_write = do_write;

		if (pthread_create(&tids[i], NULL, io_thread, &args[i]))
			err(1, "pthread_create");
	}

	for (int i = 0; i < nthreads; i++)
		(void) pthread_join(tids[i], NULL);

	free(tids);
	free(args);
	return (0);
}
