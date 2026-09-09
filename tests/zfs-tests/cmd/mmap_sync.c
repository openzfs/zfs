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
 * Dirty a mapped page, let the kernel pick it up for background writeback,
 * msync() it, and fail if an msync() takes longer than the given bound.
 * An msync() must not wait for the transaction group that a concurrent
 * background writeback of the page went into (see zpl_writepages()).
 *
 * The bound is an absolute time, and an msync() ends with a ZIL commit,
 * so an IO stall of the underlying device would exceed it just as well.
 * To tell the two apart a second thread keeps fsync()ing a plain write to
 * another file of the same dataset: that takes the same ZIL and the same
 * disk, but has no page writeback to wait for.  An msync() over the bound
 * is reported only if no fsync() that overlapped it was at least half as
 * slow; otherwise the device was stalled and the case is noted and
 * ignored.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>

#define	NSAMPLES	1024

/*
 * How long a dirtied page is left alone before it is msync()ed, on every
 * other iteration.  The test sets dirty_writeback_centisecs and
 * dirty_expire_centisecs to 1, so the kernel picks the page up for
 * background writeback within some 10-20 ms; this makes sure that has
 * happened by the time msync() looks at it.  The iterations in between
 * msync() right away, so that it is msync() that writes the page out.
 */
#define	WRITEBACK_DELAY_US	50000

/* Pause between the fsync()s that sample the device latency. */
#define	FSYNC_INTERVAL_US	10000

typedef struct sample {
	struct timespec start;
	struct timespec end;
} sample_t;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static sample_t samples[NSAMPLES];
static unsigned long nsamples;
static struct timespec inflight;	/* start of the fsync in progress */
static int inflight_valid;
static int fsync_fd = -1;
static volatile int stop;
static int fsync_err;

static double
ms_since(const struct timespec *from, const struct timespec *to)
{
	return ((to->tv_sec - from->tv_sec) * 1000.0 +
	    (to->tv_nsec - from->tv_nsec) / 1000000.0);
}

static int
ts_before(const struct timespec *a, const struct timespec *b)
{
	return (a->tv_sec < b->tv_sec ||
	    (a->tv_sec == b->tv_sec && a->tv_nsec < b->tv_nsec));
}

static void *
fsync_loop(void *arg)
{
	(void) arg;
	long long y = 0;

	while (!stop) {
		sample_t s;

		clock_gettime(CLOCK_MONOTONIC, &s.start);
		pthread_mutex_lock(&lock);
		inflight = s.start;
		inflight_valid = 1;
		pthread_mutex_unlock(&lock);

		if (pwrite(fsync_fd, &y, sizeof (y), 0) != sizeof (y) ||
		    fsync(fsync_fd) != 0) {
			fsync_err = 1;
			perror("fsync");
			stop = 1;
			return (NULL);
		}
		y++;

		clock_gettime(CLOCK_MONOTONIC, &s.end);
		pthread_mutex_lock(&lock);
		samples[nsamples++ % NSAMPLES] = s;
		inflight_valid = 0;
		pthread_mutex_unlock(&lock);
		usleep(FSYNC_INTERVAL_US);
	}
	return (NULL);
}

/*
 * The longest fsync() that overlapped [start, end], in ms.  An fsync()
 * that started inside that window but has not finished yet is waited for,
 * since it is the one that saw the same stall.
 */
static double
slowest_overlapping_fsync(const struct timespec *start,
    const struct timespec *end)
{
	double slowest = 0;

	for (;;) {
		pthread_mutex_lock(&lock);
		if (!inflight_valid || !ts_before(&inflight, end) || stop) {
			pthread_mutex_unlock(&lock);
			break;
		}
		pthread_mutex_unlock(&lock);
		usleep(1000);
	}

	pthread_mutex_lock(&lock);
	unsigned long n = nsamples < NSAMPLES ? nsamples : NSAMPLES;
	for (unsigned long i = 0; i < n; i++) {
		sample_t *s = &samples[i];

		if (ts_before(&s->end, start) || ts_before(end, &s->start))
			continue;
		double ms = ms_since(&s->start, &s->end);
		if (ms > slowest)
			slowest = ms;
	}
	pthread_mutex_unlock(&lock);
	return (slowest);
}

static void
cleanup(char *file, char *fsync_file)
{
	(void) remove(file);
	(void) remove(fsync_file);
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
	char fsyncpath[512];
	char *file = &filepath[0];
	char *fsync_file = &fsyncpath[0];

	(void) snprintf(file, sizeof (filepath), "%s/msync_file", testdir);
	(void) snprintf(fsync_file, sizeof (fsyncpath), "%s/fsync_file",
	    testdir);

	const int LEN = 8;
	cleanup(file, fsync_file);

	int fd = open(file, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR |
	    S_IRGRP | S_IROTH);

	if (fd == -1) {
		(void) fprintf(stderr, "%s: %s: ", argv[0], file);
		perror("open");
		return (1);
	}

	fsync_fd = open(fsync_file, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR |
	    S_IRGRP | S_IROTH);

	if (fsync_fd == -1) {
		(void) fprintf(stderr, "%s: %s: ", argv[0], fsync_file);
		perror("open");
		cleanup(file, fsync_file);
		return (1);
	}

	if (ftruncate(fd, LEN) != 0) {
		perror("ftruncate");
		cleanup(file, fsync_file);
		return (1);
	}

	void *ptr = mmap(NULL, LEN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

	if (ptr == MAP_FAILED) {
		perror("mmap");
		cleanup(file, fsync_file);
		return (1);
	}

	pthread_t tid;
	if (pthread_create(&tid, NULL, fsync_loop, NULL) != 0) {
		perror("pthread_create");
		cleanup(file, fsync_file);
		return (1);
	}

	struct timespec tstart;
	clock_gettime(CLOCK_MONOTONIC, &tstart);

	long long x = 0LL;
	int ret = 0;
	int stalls = 0;

	while (!stop) {
		*((long long *)ptr) = x;
		x++;
		if (x % 2 == 0)
			usleep(WRITEBACK_DELAY_US);

		struct timespec t1, t2;
		clock_gettime(CLOCK_MONOTONIC, &t1);
		if (msync(ptr, LEN, MS_SYNC|MS_INVALIDATE) != 0) {
			perror("msync");
			ret = 1;
			break;
		}
		clock_gettime(CLOCK_MONOTONIC, &t2);

		double elapsed = ms_since(&t1, &t2);
		if (elapsed > max_msync_time_ms) {
			double fsync_ms = slowest_overlapping_fsync(&t1, &t2);

			if (fsync_ms < elapsed / 2) {
				fprintf(stderr, "slow msync: %f ms, while a "
				    "concurrent fsync took at most %f ms\n",
				    elapsed, fsync_ms);
				ret = 1;
				break;
			}
			fprintf(stderr, "slow msync: %f ms, ignored: a "
			    "concurrent fsync took %f ms as well\n",
			    elapsed, fsync_ms);
			stalls++;
		}

		if (ms_since(&tstart, &t2) > run_time_mins * 60 * 1000.0)
			break;
	}

	stop = 1;
	(void) pthread_join(tid, NULL);
	if (fsync_err)
		ret = 1;
	if (stalls > 0)
		fprintf(stderr, "%d msync(s) ignored due to IO stalls\n",
		    stalls);

	if (munmap(ptr, LEN) != 0) {
		perror("munmap");
		ret = 1;
	}

	if (close(fd) != 0 || close(fsync_fd) != 0) {
		perror("close");
	}

	cleanup(file, fsync_file);
	return (ret);
}
