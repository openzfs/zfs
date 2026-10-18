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
 * Copyright (c) 2026 by George Melikov.
 */

/*
 * Repeatedly pwrite from a buffer whose second page is being discarded.
 * This can make the no-fault copy stop partway through a full DMU block.
 *
 * usage: mmap_write_source_race <file> <blocks>
 */

#ifdef __linux__

#ifndef _GNU_SOURCE
#define	_GNU_SOURCE
#endif

#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct madvise_thread_arg {
	void *page;
	size_t page_size;
	pthread_mutex_t lock;
	int stop;
	int error;
};

static void *
madvise_thread(void *arg)
{
	struct madvise_thread_arg *state = arg;

	for (;;) {
		if (madvise(state->page, state->page_size,
		    MADV_DONTNEED) != 0) {
			(void) pthread_mutex_lock(&state->lock);
			state->error = errno;
			state->stop = 1;
			(void) pthread_mutex_unlock(&state->lock);
			break;
		}

		(void) pthread_mutex_lock(&state->lock);
		int stop = state->stop;
		(void) pthread_mutex_unlock(&state->lock);
		if (stop)
			break;
	}

	return (NULL);
}

int
main(int argc, char *argv[])
{
	if (argc != 3) {
		(void) fprintf(stderr, "usage: %s <file> <blocks>\n", argv[0]);
		return (2);
	}

	char *end;
	unsigned long blocks = strtoul(argv[2], &end, 0);
	if (*argv[2] == '\0' || *end != '\0' || blocks == 0 ||
	    blocks > SIZE_MAX / 2) {
		(void) fprintf(stderr, "invalid block count: %s\n", argv[2]);
		return (2);
	}

	long page_size_long = sysconf(_SC_PAGESIZE);
	if (page_size_long <= 0 ||
	    (unsigned long)page_size_long > SIZE_MAX / 2) {
		(void) fprintf(stderr, "invalid page size\n");
		return (1);
	}
	size_t page_size = (size_t)page_size_long;
	size_t block_size = 2 * page_size;
	if (blocks > (unsigned long)(INT64_MAX / block_size)) {
		(void) fprintf(stderr, "file size is too large\n");
		return (2);
	}

	void *src = mmap(NULL, block_size, PROT_READ | PROT_WRITE,
	    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (src == MAP_FAILED) {
		perror("mmap");
		return (1);
	}
	(void) memset(src, 0x5a, block_size);

	struct madvise_thread_arg state = {
		.page = (char *)src + page_size,
		.page_size = page_size,
		.lock = PTHREAD_MUTEX_INITIALIZER,
		.stop = 0,
		.error = 0,
	};
	pthread_t thread;
	int error = pthread_create(&thread, NULL, madvise_thread, &state);
	if (error != 0) {
		(void) fprintf(stderr, "pthread_create: %s\n", strerror(error));
		(void) munmap(src, block_size);
		return (1);
	}

	int fd = open(argv[1], O_RDWR | O_CREAT | O_TRUNC, 0666);
	if (fd < 0 || ftruncate(fd, (off_t)(blocks * block_size)) != 0) {
		perror(argv[1]);
		error = 1;
		goto out;
	}

	unsigned long short_writes = 0;
	unsigned long nwrites = 0;
	for (unsigned long i = 0; i < blocks; i++) {
		off_t offset = (off_t)(i * block_size);
		ssize_t written = pwrite(fd, src, block_size, offset);
		if (written < 0) {
			(void) fprintf(stderr,
			    "pwrite block %lu failed: %s\n", i,
			    strerror(errno));
			error = 1;
			break;
		}
		nwrites++;
		if ((size_t)written != block_size)
			short_writes++;
	}

	(void) fprintf(stderr,
	    "completed %lu pwrite calls; short writes: %lu\n",
	    nwrites, short_writes);

out:
	(void) pthread_mutex_lock(&state.lock);
	state.stop = 1;
	(void) pthread_mutex_unlock(&state.lock);
	(void) pthread_join(thread, NULL);
	if (state.error != 0) {
		(void) fprintf(stderr, "madvise: %s\n", strerror(state.error));
		error = 1;
	}
	if (fd >= 0)
		(void) close(fd);
	(void) pthread_mutex_destroy(&state.lock);
	(void) munmap(src, block_size);

	return (error);
}

#else

#include <stdio.h>

int
main(void)
{
	(void) fprintf(stderr, "mmap_write_source_race requires Linux\n");
	return (77);
}

#endif
