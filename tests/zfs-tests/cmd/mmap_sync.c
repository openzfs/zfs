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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

static void
cleanup(char *file)
{
	(void) remove(file);
}

int
main(int argc, char *argv[])
{
	char *testdir = getenv("TESTDIR");
	if (!testdir) {
		fprintf(stderr, "environment variable TESTDIR not set\n");
		return (1);
	}

	struct stat st;
	umask(0);
	if (stat(testdir, &st) != 0 &&
	    mkdir(testdir, 0777) != 0) {
		perror("mkdir");
		return (1);
	}

	if (argc > 3) {
		fprintf(stderr, "usage: %s "
		    "[run time in mins] "
		    "[max msync time in ms]\n", argv[0]);
		return (1);
	}

	int run_time_mins = 1;
	if (argc >= 2) {
		run_time_mins = atoi(argv[1]);
	}

	int max_msync_time_ms = 2000;
	if (argc >= 3) {
		max_msync_time_ms = atoi(argv[2]);
	}

	char filepath[512];
	filepath[0] = '\0';
	char *file = &filepath[0];

	(void) snprintf(file, 512, "%s/msync_file", testdir);

	const int LEN = 8;
	cleanup(file);

	int fd = open(file, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR |
	    S_IRGRP | S_IROTH);

	if (fd == -1) {
		(void) fprintf(stderr, "%s: %s: ", argv[0], file);
		perror("open");
		return (1);
	}

	if (ftruncate(fd, LEN) != 0) {
		perror("ftruncate");
		cleanup(file);
		return (1);
	}

	void *ptr = mmap(NULL, LEN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

	if (ptr == MAP_FAILED) {
		perror("mmap");
		cleanup(file);
		return (1);
	}

	struct timeval tstart;
	gettimeofday(&tstart, NULL);

	long long x = 0LL;

	for (;;) {
		*((long long *)ptr) = x;
		x++;

		struct timeval t1, t2;
		gettimeofday(&t1, NULL);
		if (msync(ptr, LEN, MS_SYNC|MS_INVALIDATE) != 0) {
			perror("msync");
			cleanup(file);
			return (1);
		}

		gettimeofday(&t2, NULL);

		double elapsed = (t2.tv_sec - t1.tv_sec) * 1000.0;
		elapsed += ((t2.tv_usec - t1.tv_usec) / 1000.0);
		if (elapsed > max_msync_time_ms) {
			fprintf(stderr, "slow msync: %f ms\n", elapsed);
			if (munmap(ptr, LEN) != 0)
				perror("munmap");
			cleanup(file);
			return (1);
		}

		double elapsed_start = (t2.tv_sec - tstart.tv_sec) * 1000.0;
		elapsed_start += ((t2.tv_usec - tstart.tv_usec) / 1000.0);
		if (elapsed_start > run_time_mins * 60 * 1000) {
			break;
		}
	}

	if (munmap(ptr, LEN) != 0) {
		perror("munmap");
		cleanup(file);
		return (1);
	}

	if (close(fd) != 0) {
		perror("close");
	}

	cleanup(file);
	return (0);
}
