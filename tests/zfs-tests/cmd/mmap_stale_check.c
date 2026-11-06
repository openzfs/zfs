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
 * Helper for the tests that check that pages cached through mmap() do not
 * outlive a rollback or a forced receive (openzfs #10931).
 *
 *   mmap_stale_check touch <file>
 *       fault in every page through a read-only mapping
 *   mmap_stale_check csum <file>
 *       print a checksum of the contents, using pread()
 *   mmap_stale_check msum <file>
 *       print the same checksum, using a read-only mapping
 *   mmap_stale_check fault <file>
 *       keep faulting random pages in through read-only mappings, evicting
 *       them again with MADV_PAGEOUT, until killed
 *   mmap_stale_check dirty <file>
 *       keep dirtying random pages through a writable mapping, until killed
 *   mmap_stale_check poke <file>
 *       dirty page 3 through a writable mapping and keep the mapping until
 *       killed, without writing the page back
 *
 * The mapping created by "touch" is gone when the program exits, but the
 * pages it faulted in stay resident in the page cache.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifndef	MADV_PAGEOUT
#define	MADV_PAGEOUT	21
#endif

#define	FNV_OFFSET	0xcbf29ce484222325ULL
#define	FNV_PRIME	0x100000001b3ULL
#define	CHUNK		(1024 * 1024)

static uint64_t
fnv1a(uint64_t h, const unsigned char *p, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		h ^= p[i];
		h *= FNV_PRIME;
	}
	return (h);
}

static void *
map_file(int fd, size_t size)
{
	void *map = mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0);

	if (map == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}
	return (map);
}

static void
do_touch(int fd, size_t size)
{
	long pagesize = sysconf(_SC_PAGESIZE);
	const volatile unsigned char *map = map_file(fd, size);
	unsigned char sink = 0;

	for (size_t off = 0; off < size; off += pagesize)
		sink ^= map[off];

	(void) munmap((void *)map, size);
	(void) sink;
}

static void
do_fault(int fd, size_t size)
{
	long pagesize = sysconf(_SC_PAGESIZE);
	size_t npages = size / pagesize;
	unsigned int seed = getpid();
	volatile unsigned char sink = 0;

	if (npages == 0) {
		(void) fprintf(stderr, "file smaller than a page\n");
		exit(1);
	}

	for (;;) {
		const volatile unsigned char *map = map_file(fd, size);

		for (int i = 0; i < 256; i++)
			sink ^= map[(rand_r(&seed) % npages) * pagesize];

		/*
		 * Evict the pages again, so that the next pass faults.  This
		 * is best effort: it fails before Linux 5.4, and skips pages
		 * which are mapped by several processes.
		 */
		(void) madvise((void *)map, size, MADV_PAGEOUT);
		(void) munmap((void *)map, size);
		(void) sink;
	}
}

/*
 * Dirty pages through a shared writable mapping.  With "poke" a single page
 * is dirtied, and the mapping is then kept (and the page left dirty) until
 * the program is killed.  Otherwise random pages are dirtied over and over,
 * for something else to write them back.
 */
static void
do_dirty(const char *path, size_t size, int poke)
{
	long pagesize = sysconf(_SC_PAGESIZE);
	size_t npages = size / pagesize;
	unsigned int seed = getpid();
	int fd = open(path, O_RDWR);
	volatile unsigned char *map;

	if (fd == -1) {
		perror("open");
		exit(1);
	}
	map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (map == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}

	if (poke) {
		map[3 * pagesize] ^= 0xff;
		for (;;)
			(void) pause();
	}

	for (unsigned int i = 0; ; i++) {
		map[(rand_r(&seed) % npages) * pagesize] = (unsigned char)i;
		if (i % 64 == 0)
			(void) msync((void *)map, size, MS_ASYNC);
	}
}

static uint64_t
do_csum(int fd, size_t size)
{
	unsigned char *buf = malloc(CHUNK);
	uint64_t h = FNV_OFFSET;
	size_t off = 0;

	if (buf == NULL) {
		perror("malloc");
		exit(1);
	}
	while (off < size) {
		size_t len = size - off < CHUNK ? size - off : CHUNK;
		ssize_t n = pread(fd, buf, len, off);

		if (n <= 0) {
			perror("pread");
			exit(1);
		}
		h = fnv1a(h, buf, n);
		off += n;
	}
	free(buf);
	return (h);
}

static uint64_t
do_msum(int fd, size_t size)
{
	void *map = map_file(fd, size);
	uint64_t h = fnv1a(FNV_OFFSET, map, size);

	(void) munmap(map, size);
	return (h);
}

int
main(int argc, char **argv)
{
	struct stat st;
	int fd;

	if (argc != 3 || (strcmp(argv[1], "touch") != 0 &&
	    strcmp(argv[1], "csum") != 0 && strcmp(argv[1], "msum") != 0 &&
	    strcmp(argv[1], "fault") != 0 && strcmp(argv[1], "dirty") != 0 &&
	    strcmp(argv[1], "poke") != 0)) {
		(void) fprintf(stderr,
		    "usage: %s touch|csum|msum|fault|dirty|poke <file>\n",
		    argv[0]);
		exit(2);
	}

	if ((fd = open(argv[2], O_RDONLY)) == -1) {
		perror("open");
		exit(1);
	}
	if (fstat(fd, &st) == -1 || st.st_size == 0) {
		(void) fprintf(stderr, "%s: cannot stat or empty file\n",
		    argv[2]);
		exit(1);
	}

	if (strcmp(argv[1], "touch") == 0) {
		do_touch(fd, st.st_size);
	} else if (strcmp(argv[1], "fault") == 0) {
		do_fault(fd, st.st_size);
	} else if (strcmp(argv[1], "dirty") == 0 ||
	    strcmp(argv[1], "poke") == 0) {
		if (st.st_size < 4 * sysconf(_SC_PAGESIZE)) {
			(void) fprintf(stderr, "%s: file too small\n", argv[2]);
			exit(1);
		}
		(void) close(fd);
		do_dirty(argv[2], st.st_size, strcmp(argv[1], "poke") == 0);
	} else {
		uint64_t h = strcmp(argv[1], "csum") == 0 ?
		    do_csum(fd, st.st_size) : do_msum(fd, st.st_size);

		(void) printf("%016llx\n", (unsigned long long)h);
	}

	(void) close(fd);
	return (0);
}
