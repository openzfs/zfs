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
 * Check that POSIX_FADV_WILLNEED readahead on a mapped file and pwrite()
 * to the same file keep making progress side by side.
 *
 * usage: mmap_fadvise_readahead_writer <file> <seconds> <stall>
 *
 * Writers rewrite WRITE_SIZE bytes at random page aligned offsets of a
 * FILE_SIZE file.  Readers drop a SPAN_SIZE span from the page cache, fault
 * one page of it back in through a MADV_RANDOM mapping, so that the file
 * keeps cached pages and zpl_fadvise() goes on to generic_fadvise(), and
 * then ask for the span with POSIX_FADV_WILLNEED.  The kernel hands
 * ->readahead() the pages of the span locked; a readahead that waits for a
 * writer's range lock while that writer waits in update_pages() for one of
 * those pages never moves again.
 *
 * Every second the counts of both sides are compared with the previous
 * second.  If neither moved for <stall> seconds, print them and exit 2.
 * Threads stuck in the kernel cannot be killed, so the test also bounds
 * the wait for this process.  After <seconds> of progress, exit 0.
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define	FILE_SIZE	(64UL << 20)
#define	WRITE_SIZE	(64UL << 10)
#define	SPAN_SIZE	(4UL << 20)
#define	NWRITERS	4
#define	NREADERS	4

static int fd;
static char *map;
static size_t pagesize;
static uint64_t writes;
static uint64_t readaheads;

static uint64_t
next_random(uint64_t *state)
{
	*state ^= *state << 13;
	*state ^= *state >> 7;
	*state ^= *state << 17;
	return (*state);
}

static off_t
random_offset(uint64_t *state, size_t len)
{
	return ((off_t)(next_random(state) % ((FILE_SIZE - len) / pagesize)) *
	    pagesize);
}

static void *
writer(void *arg)
{
	uint64_t state = (uintptr_t)arg * 0x9e3779b97f4a7c15ULL + 1;
	char *buf = malloc(WRITE_SIZE);

	if (buf == NULL) {
		perror("malloc");
		exit(1);
	}
	memset(buf, 'w', WRITE_SIZE);
	for (;;) {
		off_t off = random_offset(&state, WRITE_SIZE);

		if (pwrite(fd, buf, WRITE_SIZE, off) != (ssize_t)WRITE_SIZE) {
			perror("pwrite");
			exit(1);
		}
		__atomic_fetch_add(&writes, 1, __ATOMIC_RELAXED);
	}
	return (NULL);
}

static void *
reader(void *arg)
{
	uint64_t state = (uintptr_t)arg * 0xbf58476d1ce4e5b9ULL + 7;
	volatile char sink;

	for (;;) {
		off_t off = random_offset(&state, SPAN_SIZE);

		(void) madvise(map + off, SPAN_SIZE, MADV_DONTNEED);
		(void) posix_fadvise(fd, off, SPAN_SIZE, POSIX_FADV_DONTNEED);
		sink = map[off];
		(void) sink;
		(void) posix_fadvise(fd, off, SPAN_SIZE, POSIX_FADV_WILLNEED);
		__atomic_fetch_add(&readaheads, 1, __ATOMIC_RELAXED);
	}
	return (NULL);
}

int
main(int argc, char *argv[])
{
	pthread_t tid;
	char *buf;
	int seconds, stall, still = 0;
	uint64_t last_writes = 0, last_readaheads = 0;

	if (argc != 4) {
		(void) fprintf(stderr,
		    "usage: %s <file> <seconds> <stall>\n", argv[0]);
		return (1);
	}
	seconds = atoi(argv[2]);
	stall = atoi(argv[3]);
	pagesize = sysconf(_SC_PAGESIZE);

	if ((fd = open(argv[1], O_RDWR | O_CREAT | O_TRUNC, 0644)) < 0) {
		perror("open");
		return (1);
	}
	if ((buf = malloc(1 << 20)) == NULL) {
		perror("malloc");
		return (1);
	}
	memset(buf, 'f', 1 << 20);
	for (off_t off = 0; off < (off_t)FILE_SIZE; off += 1 << 20) {
		if (pwrite(fd, buf, 1 << 20, off) != 1 << 20) {
			perror("pwrite");
			return (1);
		}
	}
	free(buf);
	if (fsync(fd) != 0) {
		perror("fsync");
		return (1);
	}

	map = mmap(NULL, FILE_SIZE, PROT_READ, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED) {
		perror("mmap");
		return (1);
	}
	if (madvise(map, FILE_SIZE, MADV_RANDOM) != 0) {
		perror("madvise");
		return (1);
	}

	for (uintptr_t i = 1; i <= NWRITERS; i++) {
		if ((errno = pthread_create(&tid, NULL, writer,
		    (void *)i)) != 0) {
			perror("pthread_create");
			return (1);
		}
	}
	for (uintptr_t i = 1; i <= NREADERS; i++) {
		if ((errno = pthread_create(&tid, NULL, reader,
		    (void *)i)) != 0) {
			perror("pthread_create");
			return (1);
		}
	}

	for (int t = 1; t <= seconds; t++) {
		uint64_t w, r;

		(void) sleep(1);
		w = __atomic_load_n(&writes, __ATOMIC_RELAXED);
		r = __atomic_load_n(&readaheads, __ATOMIC_RELAXED);
		still = (w == last_writes && r == last_readaheads) ?
		    still + 1 : 0;
		if (still >= stall) {
			(void) printf("no progress for %ds after %ds: "
			    "%llu writes, %llu readaheads\n", still, t,
			    (unsigned long long)w, (unsigned long long)r);
			(void) fflush(stdout);
			_exit(2);
		}
		last_writes = w;
		last_readaheads = r;
	}

	(void) printf("%ds: %llu writes, %llu readaheads\n", seconds,
	    (unsigned long long)last_writes,
	    (unsigned long long)last_readaheads);
	return (0);
}
