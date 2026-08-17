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
 * Copyright 2026, tiehexue <tiehexue@hotmail.com>. All rights reserved.
 */

/*
 * Submit vectored asynchronous Direct I/O writes and verify what comes back.
 *
 * A request built from two or more iovec segments is described to the kernel
 * by an iterator with a backing array that belongs to the submitting aio
 * call: io_submit() frees the array (or unwinds the stack frame that holds
 * it) when it returns, long before a deferred completion runs.  ZFS's async
 * Direct I/O path must therefore copy the array into the queued request; a
 * reader of the original array on the worker taskq thread touches released
 * memory.  Only the vectored shape carries such an array, so the segment
 * count is the point of this program and is left to the caller.  One
 * segment is the control.
 *
 * In the "fsync" (or "syncfs") mode the program calls fsync() (or syncfs())
 * immediately after io_submit() and before waiting for the completion,
 * exercising the barrier race with a write that is still queued or still
 * executing on the taskq.  A barrier that does not wait for queued writes
 * before committing the ZIL can return before the write is durable, and a
 * broken barrier counter can make fsync() wait forever; the alarm turns
 * that hang into a distinct exit code.
 *
 * Each trial checks both the length the kernel reports and, by reading the
 * file back with O_DIRECT, every byte it claims to have written.  Trials
 * repeat because a request that reads released memory can come out either
 * way depending on what has since reused it, so a single observation of
 * success proves little.
 *
 * usage: aio_dio_writev <file> <segments> <trials> [verify|fsync|syncfs]
 * exit:  0 = every trial delivered the whole request correctly
 *        1 = at least one trial did not
 *        2 = usage or setup error
 *        3 = fsync()/syncfs() did not return in time (barrier hang)
 */

#include <errno.h>
#include <fcntl.h>
#include <libaio.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

/* Wait this long for a completion before calling the trial failed. */
#define	WAIT_SECONDS	60

static unsigned char
pattern(size_t off)
{
	return ((unsigned char)((off * 31) + (off >> 8) * 7 + 1));
}

/*
 * A hung fsync()/syncfs() barrier is the failure mode under test; report it
 * with a distinct exit code instead of blocking the whole test run.
 */
static void
alarm_handler(int sig)
{
	(void) sig;
	_exit(3);
}

/*
 * One vectored async Direct I/O write of the whole file, then a read-back.
 *
 * Returns 0 when the kernel reported every byte and the file matches, 1 when
 * it did not, and 2 when the request may still be in flight.  The caller has
 * to stop after a 2: the write buffer is still spoken for, so reusing it for
 * another trial would be writing it from under the kernel.
 */
static int
write_trial(io_context_t ctx, int fd, struct iovec *iov, int nseg,
    unsigned char *rbuf, size_t len, int trial, const char *mode)
{
	struct timespec ts = { WAIT_SECONDS, 0 };
	struct io_event ev;
	struct iocb cb;
	struct iocb *cbp = &cb;
	size_t firstbad = len;
	ssize_t got_read;
	long res;
	int got;

	io_prep_pwritev(&cb, fd, iov, nseg, 0);

	got = io_submit(ctx, 1, &cbp);
	if (got != 1) {
		(void) fprintf(stderr, "trial %d: io_submit: %s\n", trial,
		    strerror(got < 0 ? -got : EIO));
		return (1);
	}

	if (strcmp(mode, "fsync") == 0) {
		(void) alarm(WAIT_SECONDS);
		if (fsync(fd) != 0) {
			(void) fprintf(stderr, "trial %d: fsync: %s\n", trial,
			    strerror(errno));
			(void) alarm(0);
			return (1);
		}
		(void) alarm(0);
	} else if (strcmp(mode, "syncfs") == 0) {
		(void) alarm(WAIT_SECONDS);
		if (syncfs(fd) != 0) {
			(void) fprintf(stderr, "trial %d: syncfs: %s\n",
			    trial, strerror(errno));
			(void) alarm(0);
			return (1);
		}
		(void) alarm(0);
	}

	got = io_getevents(ctx, 1, 1, &ev, &ts);
	if (got < 0) {
		(void) fprintf(stderr, "trial %d: io_getevents: %s\n", trial,
		    strerror(-got));
		return (2);
	}
	if (got != 1) {
		(void) fprintf(stderr,
		    "trial %d: no completion within %d seconds\n", trial,
		    WAIT_SECONDS);
		return (2);
	}

	res = (long)ev.res;
	if (res < 0) {
		(void) fprintf(stderr, "trial %d: write failed: %s\n", trial,
		    strerror((int)-res));
		return (1);
	}
	if ((size_t)res != len) {
		(void) fprintf(stderr,
		    "trial %d: short write: %ld of %zu bytes\n", trial, res,
		    len);
		return (1);
	}

	/*
	 * Read the file back with O_DIRECT and check every byte.  A read
	 * that the kernel called successful must match the pattern exactly.
	 */
	got_read = pread(fd, rbuf, len, 0);
	if (got_read < 0) {
		(void) fprintf(stderr, "trial %d: read back: %s\n", trial,
		    strerror(errno));
		return (1);
	}
	if ((size_t)got_read != len) {
		(void) fprintf(stderr,
		    "trial %d: read back was short: %zd of %zu bytes\n",
		    trial, got_read, len);
		return (1);
	}
	for (size_t i = 0; i < len; i++) {
		if (rbuf[i] != pattern(i)) {
			firstbad = i;
			break;
		}
	}
	if (firstbad != len) {
		(void) fprintf(stderr,
		    "trial %d: write reported %zu bytes but byte %zu is "
		    "0x%02x, expected 0x%02x\n", trial, len, firstbad,
		    rbuf[firstbad], pattern(firstbad));
		return (1);
	}

	return (0);
}

int
main(int argc, char **argv)
{
	io_context_t ctx = 0;
	struct iovec *iov;
	unsigned char *buf = NULL;
	unsigned char *rbuf = NULL;
	const char *path;
	const char *mode = "verify";
	size_t len, pagesize;
	int nseg, trials, failed = 0, fd, ret;

	if (argc != 4 && argc != 5) {
		(void) fprintf(stderr,
		    "usage: %s <file> <segments> <trials> "
		    "[verify|fsync|syncfs]\n", argv[0]);
		return (2);
	}

	path = argv[1];
	nseg = atoi(argv[2]);
	trials = atoi(argv[3]);
	if (argc == 5)
		mode = argv[4];
	if (nseg < 1 || trials < 1) {
		(void) fprintf(stderr, "segments and trials must be >= 1\n");
		return (2);
	}
	if (strcmp(mode, "verify") != 0 && strcmp(mode, "fsync") != 0 &&
	    strcmp(mode, "syncfs") != 0) {
		(void) fprintf(stderr, "unknown mode: %s\n", mode);
		return (2);
	}

	(void) signal(SIGALRM, alarm_handler);

	pagesize = (size_t)getpagesize();
	len = (size_t)nseg * pagesize;

	if (posix_memalign((void **)&buf, pagesize, len) != 0) {
		perror("posix_memalign");
		return (2);
	}
	if (posix_memalign((void **)&rbuf, pagesize, len) != 0) {
		perror("posix_memalign");
		return (2);
	}
	for (size_t i = 0; i < len; i++)
		buf[i] = pattern(i);

	iov = calloc(nseg, sizeof (*iov));
	if (iov == NULL) {
		perror("calloc");
		return (2);
	}
	for (int i = 0; i < nseg; i++) {
		iov[i].iov_base = buf + (size_t)i * pagesize;
		iov[i].iov_len = pagesize;
	}

	fd = open(path, O_RDWR | O_CREAT | O_TRUNC | O_DIRECT, 0644);
	if (fd < 0) {
		perror("open for write");
		return (2);
	}

	/* io_setup() returns a negative errno rather than setting errno. */
	ret = io_setup(1, &ctx);
	if (ret != 0) {
		(void) fprintf(stderr, "io_setup: %s\n", strerror(-ret));
		(void) close(fd);
		return (2);
	}

	for (int i = 1; i <= trials; i++) {
		ret = write_trial(ctx, fd, iov, nseg, rbuf, len, i, mode);
		if (ret != 0)
			failed++;
		if (ret == 2) {
			(void) fprintf(stderr,
			    "trial %d left a request in flight; stopping\n",
			    i);
			break;
		}
	}

	(void) io_destroy(ctx);
	(void) close(fd);
	free(iov);
	free(rbuf);
	free(buf);

	if (failed != 0) {
		(void) fprintf(stderr,
		    "%d of %d vectored Direct I/O writes did not deliver the "
		    "request (%d segments, %zu bytes, mode %s)\n", failed,
		    trials, nseg, len, mode);
		return (1);
	}

	(void) printf("%d of %d trials delivered and verified %zu bytes over "
	    "%d segments (%s)\n", trials, trials, len, nseg, mode);

	return (0);
}
