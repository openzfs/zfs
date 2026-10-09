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
 * Verify that fsync()/syncfs() cannot be satisfied by a later write
 * completing early while an earlier write is still in flight.
 *
 * W1 is an asynchronous Direct I/O write to file1 at offset 0.  A reader
 * thread first submits an asynchronous Direct I/O read of the same range,
 * which the test holds in the data pipeline with an injected read delay (see
 * async_write_006_pos): the read holds the range lock, so W1's write task
 * blocks in zfs_write_impl() and its barrier sequence is not completed.
 * While W1 is blocked, the barrier thread calls fsync() (or syncfs()), and
 * the main thread submits a second write (W2) on an undelayed range which
 * completes quickly.  A barrier built on completed-counts would see "one
 * write completed" and return early; the sequence watermark barrier must
 * keep waiting until W1 itself has completed.
 *
 * In the fsync mode W2 is written to file1 at a second offset, so the
 * per-znode barrier is exercised.  In the syncfs mode W2 is written to
 * file2, exercising the filesystem-wide barrier.
 *
 * usage: aio_barrier_race <file1> <file2> <fsync|syncfs>
 * exit:  0 = the barrier stayed blocked until W1 completed
 *        1 = the barrier was satisfied by the later write, hung, or a
 *            completion reported the wrong length
 *        2 = usage or setup error
 */

#include <errno.h>
#include <fcntl.h>
#include <libaio.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#define	PAGESZ	4096
#define	W1_OFF	0
#define	W2_OFF	PAGESZ

/* The injected delay must outlast the pre-W2 stall below. */
#define	READER_HEADSTART_NS	(500ULL * 1000 * 1000)
#define	STALL_NS		(1500ULL * 1000 * 1000)
#define	FINISH_WAIT_NS		(20ULL * 1000 * 1000 * 1000)
#define	EVENT_WAIT_SECS		20

struct barrier_args {
	int		fd1;
	int		fd2;	/* -1 unless syncfs mode */
	int		mode;	/* 0 = fsync, 1 = syncfs */
	unsigned char	*buf;
	io_context_t	ctx1;	/* W1's completion context */
	volatile int	done;	/* set once the barrier returned */
};

/*
 * The reader holds the range lock of [0, 4K) while its Direct I/O read is
 * stalled in the data pipeline, which is what blocks W1's write task.
 */
static void *
reader_thread(void *arg)
{
	struct barrier_args *sh = arg;
	io_context_t ctx = 0;
	struct iocb cb;
	struct iocb *cbp = &cb;
	struct io_event ev;
	struct timespec ts = { EVENT_WAIT_SECS, 0 };

	(void) io_setup(1, &ctx);
	io_prep_pread(&cb, sh->fd1, sh->buf, PAGESZ, W1_OFF);
	if (io_submit(ctx, 1, &cbp) != 1) {
		(void) fprintf(stderr, "io_submit(read) failed\n");
		(void) io_destroy(ctx);
		_exit(2);
	}
	(void) io_getevents(ctx, 1, 1, &ev, &ts);
	(void) io_destroy(ctx);
	return (NULL);
}

static void *
barrier_thread(void *arg)
{
	struct barrier_args *sh = arg;
	struct iocb cb;
	struct iocb *cbp = &cb;

	/* W1: 4K at offset 0, blocked on the range lock held by the read. */
	io_prep_pwrite(&cb, sh->fd1, sh->buf, PAGESZ, W1_OFF);
	if (io_submit(sh->ctx1, 1, &cbp) != 1) {
		(void) fprintf(stderr, "io_submit(W1) failed\n");
		_exit(2);
	}

	/*
	 * The barrier must stay blocked until W1 executes.  Exit as soon as
	 * it returns so that an early return is observable while W1 is still
	 * blocked; W1's completion is collected by main.
	 */
	if (sh->mode == 0) {
		if (fsync(sh->fd1) != 0)
			(void) fprintf(stderr, "fsync: %s\n", strerror(errno));
	} else {
		if (syncfs(sh->fd1) != 0)
			(void) fprintf(stderr, "syncfs: %s\n", strerror(errno));
	}
	sh->done = 1;
	return (NULL);
}

static uint64_t
now_ns(void)
{
	struct timespec ts;

	(void) clock_gettime(CLOCK_MONOTONIC, &ts);
	return ((uint64_t)ts.tv_sec * 1000 * 1000 * 1000 + ts.tv_nsec);
}

int
main(int argc, char **argv)
{
	/*
	 * Zero-initialize: io_setup() requires *ctxp to be 0 on entry and
	 * fails with EINVAL if the caller's slot holds a nonzero value.  The
	 * stack contents of an uninitialized struct member are environment
	 * dependent, which made the failure show up only on some systems.
	 */
	struct barrier_args sh = { 0 };
	io_context_t ctx2 = 0;
	struct iocb cb2;
	struct iocb *cbp2 = &cb2;
	struct io_event ev;
	struct timespec ts = { EVENT_WAIT_SECS, 0 };
	pthread_t rtid, btid;
	uint64_t start;
	int ret, got, fd2 = -1;

	if (argc != 4) {
		(void) fprintf(stderr,
		    "usage: %s <file1> <file2> <fsync|syncfs>\n", argv[0]);
		return (2);
	}
	if (strcmp(argv[3], "fsync") == 0)
		sh.mode = 0;
	else if (strcmp(argv[3], "syncfs") == 0)
		sh.mode = 1;
	else {
		(void) fprintf(stderr, "unknown mode: %s\n", argv[3]);
		return (2);
	}

	if (posix_memalign((void **)&sh.buf, PAGESZ, PAGESZ) != 0) {
		perror("posix_memalign");
		return (2);
	}
	sh.done = 0;
	sh.fd1 = open(argv[1], O_RDWR | O_DIRECT);
	if (sh.fd1 < 0) {
		perror("open file1");
		return (2);
	}
	if (sh.mode == 1) {
		fd2 = open(argv[2], O_RDWR | O_DIRECT);
		if (fd2 < 0) {
			perror("open file2");
			return (2);
		}
	}
	sh.fd2 = fd2;

	/*
	 * io_setup() returns a negative errno rather than setting errno
	 * (libaio on some distributions restores the caller's errno), so
	 * perror() here would print a misleading "Success" on failure.
	 */
	ret = io_setup(1, &sh.ctx1);
	if (ret != 0) {
		(void) fprintf(stderr, "io_setup(ctx1): %s\n",
		    ret < 0 ? strerror(-ret) : "nonzero return");
		return (2);
	}
	ret = io_setup(1, &ctx2);
	if (ret != 0) {
		(void) fprintf(stderr, "io_setup(ctx2): %s\n",
		    ret < 0 ? strerror(-ret) : "nonzero return");
		return (2);
	}

	/*
	 * Start the delayed read first and give it time to take the range
	 * lock, so W1's write task is guaranteed to block behind it.
	 */
	(void) pthread_create(&rtid, NULL, reader_thread, &sh);
	(void) usleep(READER_HEADSTART_NS / 1000);
	(void) pthread_create(&btid, NULL, barrier_thread, &sh);

	(void) usleep(STALL_NS / 1000);

	if (sh.done != 0) {
		(void) fprintf(stderr,
		    "barrier returned before W2 was submitted\n");
		return (1);
	}

	/* W2: same file, second offset (fsync) or file2 at 0 (syncfs). */
	if (sh.mode == 0)
		io_prep_pwrite(&cb2, sh.fd1, sh.buf, PAGESZ, W2_OFF);
	else
		io_prep_pwrite(&cb2, fd2, sh.buf, PAGESZ, W1_OFF);
	got = io_submit(ctx2, 1, &cbp2);
	if (got != 1) {
		(void) fprintf(stderr, "io_submit(W2): %s\n",
		    strerror(got < 0 ? -got : EIO));
		return (2);
	}
	got = io_getevents(ctx2, 1, 1, &ev, &ts);
	if (got != 1) {
		(void) fprintf(stderr,
		    "W2 did not complete within %d seconds\n",
		    EVENT_WAIT_SECS);
		return (1);
	}
	if ((long)ev.res != PAGESZ) {
		(void) fprintf(stderr, "W2 reported %ld bytes\n", (long)ev.res);
		return (1);
	}

	/*
	 * A broken count-based barrier returns as soon as W2 completes.  Give
	 * the barrier thread a moment to observe that and set 'done', so the
	 * check below cannot race the flag write.
	 */
	(void) usleep(1000000);
	if (sh.done != 0) {
		(void) fprintf(stderr,
		    "barrier was satisfied by the later write while W1 was "
		    "still in flight\n");
		return (1);
	}

	/*
	 * W1 is still blocked, so the barrier must still be blocked.  Wait
	 * for the injected read delay to expire, W1 to complete, and the
	 * barrier to return.
	 */
	start = now_ns();
	while (sh.done == 0) {
		if (now_ns() - start > (uint64_t)FINISH_WAIT_NS) {
			(void) fprintf(stderr,
			    "barrier did not return after W1 completed\n");
			return (1);
		}
		(void) usleep(100000);
	}
	(void) pthread_join(btid, NULL);
	(void) pthread_join(rtid, NULL);

	/* W1 completed once the barrier returned; collect its event. */
	got = io_getevents(sh.ctx1, 1, 1, &ev, &ts);
	if (got != 1 || (long)ev.res != PAGESZ) {
		(void) fprintf(stderr, "W1 completion: got=%d res=%ld\n", got,
		    got == 1 ? (long)ev.res : -1L);
		return (1);
	}

	ret = 0;
	(void) io_destroy(sh.ctx1);
	(void) io_destroy(ctx2);
	(void) close(sh.fd1);
	if (fd2 >= 0)
		(void) close(fd2);
	free(sh.buf);

	(void) printf("%s barrier stayed blocked until W1 completed\n",
	    sh.mode == 0 ? "fsync" : "syncfs");
	return (ret);
}
