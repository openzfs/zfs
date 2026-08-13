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
 * Submit vectored asynchronous Direct I/O reads and verify what comes back.
 *
 * A request built from two or more iovec segments is described to the kernel
 * by an iterator with a backing array, and that array belongs to the
 * submitting aio call rather than to the request.  On kernels that import a
 * single-segment read as ITER_UBUF the one-segment case holds the address in
 * the iterator itself and has no such array, which makes it a useful control.
 * Only the vectored shape can expose a reader of the array running after
 * submission has returned, so the segment count is the point of this program
 * and is left to the caller.
 *
 * The file is written with O_DIRECT so that no page cache remains for it.  A
 * cached range is a legitimate reason to decline Direct I/O, and leaving one
 * behind would make a decline ambiguous.
 *
 * Each trial checks both the length the kernel reports and every byte it
 * claims to have delivered; those are separate failures and it matters which
 * happened.  Trials repeat because a request that reads released memory can
 * come out either way depending on what has since reused it, so a single
 * observation of success proves little.
 *
 * usage: aio_dio_readv <file> <segments> <trials>
 * exit:  0 = every trial delivered the whole request correctly
 *        1 = at least one trial did not
 *        2 = usage or setup error
 */

#include <errno.h>
#include <fcntl.h>
#include <libaio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

/*
 * Pre-fill for the read buffer.  Any byte still holding this after a read the
 * kernel called successful was never written.
 */
#define	POISON	0xa5

/* Wait this long for a completion before calling the trial failed. */
#define	WAIT_SECONDS	60

static unsigned char
pattern(size_t off)
{
	return ((unsigned char)((off * 31) + (off >> 8) * 7 + 1));
}

/*
 * Write the test file with O_DIRECT, so the data reaches the pool without
 * leaving a page cache behind.
 */
static int
write_file(const char *path, unsigned char *buf, size_t len)
{
	ssize_t got;
	int fd;

	for (size_t i = 0; i < len; i++)
		buf[i] = pattern(i);

	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_DIRECT, 0644);
	if (fd < 0) {
		perror("open for write");
		return (-1);
	}

	got = write(fd, buf, len);
	if (got < 0) {
		perror("write");
		(void) close(fd);
		return (-1);
	}
	if ((size_t)got != len) {
		(void) fprintf(stderr, "short write: %zd of %zu\n", got, len);
		(void) close(fd);
		return (-1);
	}

	if (close(fd) != 0) {
		perror("close after write");
		return (-1);
	}

	return (0);
}

/*
 * One vectored async Direct I/O read of the whole file.
 *
 * Returns 0 when the kernel delivered every byte, 1 when it did not, and 2
 * when it did not and the request may still be in flight.  The caller has to
 * stop after a 2: the read buffer is still spoken for, so reusing it for
 * another trial would be reading and writing it from under the kernel.
 */
static int
read_trial(io_context_t ctx, int fd, struct iovec *iov, int nseg,
    unsigned char *buf, size_t len, int trial)
{
	struct timespec ts = { WAIT_SECONDS, 0 };
	struct io_event ev;
	struct iocb cb;
	struct iocb *cbp = &cb;
	size_t firstbad = len;
	long res;
	int got;

	memset(buf, POISON, len);

	io_prep_preadv(&cb, fd, iov, nseg, 0);

	got = io_submit(ctx, 1, &cbp);
	if (got != 1) {
		(void) fprintf(stderr, "trial %d: io_submit: %s\n", trial,
		    strerror(got < 0 ? -got : EIO));
		return (1);
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
		(void) fprintf(stderr, "trial %d: read failed: %s\n", trial,
		    strerror((int)-res));
		return (1);
	}
	if ((size_t)res != len) {
		(void) fprintf(stderr,
		    "trial %d: short read: %ld of %zu bytes\n", trial, res,
		    len);
		return (1);
	}

	for (size_t i = 0; i < len; i++) {
		if (buf[i] != pattern(i)) {
			firstbad = i;
			break;
		}
	}
	if (firstbad != len) {
		(void) fprintf(stderr,
		    "trial %d: read reported %zu bytes but byte %zu is "
		    "0x%02x, expected 0x%02x\n", trial, len, firstbad,
		    buf[firstbad], pattern(firstbad));
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
	const char *path;
	size_t len, pagesize;
	int nseg, trials, failed = 0, fd, ret;

	if (argc != 4) {
		(void) fprintf(stderr,
		    "usage: %s <file> <segments> <trials>\n", argv[0]);
		return (2);
	}

	path = argv[1];
	nseg = atoi(argv[2]);
	trials = atoi(argv[3]);
	if (nseg < 1 || trials < 1) {
		(void) fprintf(stderr, "segments and trials must be >= 1\n");
		return (2);
	}

	pagesize = (size_t)getpagesize();
	len = (size_t)nseg * pagesize;

	if (posix_memalign((void **)&buf, pagesize, len) != 0) {
		perror("posix_memalign");
		return (2);
	}

	iov = calloc(nseg, sizeof (*iov));
	if (iov == NULL) {
		perror("calloc");
		return (2);
	}
	for (int i = 0; i < nseg; i++) {
		iov[i].iov_base = buf + (size_t)i * pagesize;
		iov[i].iov_len = pagesize;
	}

	if (write_file(path, buf, len) != 0)
		return (2);

	fd = open(path, O_RDONLY | O_DIRECT);
	if (fd < 0) {
		perror("open for read");
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
		ret = read_trial(ctx, fd, iov, nseg, buf, len, i);
		if (ret != 0)
			failed++;
		if (ret == 2) {
			(void) fprintf(stderr,
			    "trial %d left a request in flight; stopping\n", i);
			break;
		}
	}

	(void) io_destroy(ctx);
	(void) close(fd);
	free(iov);
	free(buf);

	if (failed != 0) {
		(void) fprintf(stderr,
		    "%d of %d vectored Direct I/O reads did not deliver the "
		    "request (%d segments, %zu bytes)\n", failed, trials,
		    nseg, len);
		return (1);
	}

	(void) printf("%d of %d trials delivered %zu bytes over %d segments\n",
	    trials, trials, len, nseg);

	return (0);
}
