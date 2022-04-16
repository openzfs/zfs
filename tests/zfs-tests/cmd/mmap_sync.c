/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://opensource.org/licenses/CDDL-1.0.
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
	remove(file);
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

	int run_time_mins = 5;
	if (argc >= 2) {
		run_time_mins = atoi(argv[1]);
	}

	int max_msync_time_ms = 1000;
	if (argc >= 3) {
		max_msync_time_ms = atoi(argv[2]);
	}

	char filepath[512];
	filepath[0] = '\0';
	char *file = &filepath[0];

	strcat(file, testdir);
	strcat(file, "/msync_file");

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
			munmap(ptr, LEN);
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
