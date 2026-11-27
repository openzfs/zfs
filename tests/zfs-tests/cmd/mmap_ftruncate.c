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
 * Copyright (c) 2025, Klara, Inc.
 */

/*
 * Tests async writeback behaviour. Creates a file, maps it into memory, and
 * dirties every page within it. Then, starts async writeback of all the
 * dirty pages (sync_file_range(SYNC_FILE_RANGE_WRITE) on Linux,
 * msync(MS_ASYNC) elsewhere) and calls ftruncate() to collapse the file back
 * down to 0. ftruncate() must wait for any page still under writeback before
 * it can free it. None of these are sync operations, so they should avoid
 * the various "force flush" codepaths, and the writeback should complete as
 * soon as the DMU has the data.
 *
 * Prints the time the final ftruncate() took, in milliseconds. Only that
 * call is timed: dirtying the pages and starting their writeback push the
 * whole file through the DMU, so how long that takes depends on the
 * machine, not on whether async writeback completes immediately.
 */

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#define	_pdfail(f, l, s)	\
	do { perror("[" f "#" #l "] " s); exit(2); } while (0)
#define	pdfail(str) _pdfail(__FILE__, __LINE__, str)

int
main(int argc, char **argv)
{
	if (argc != 3) {
		printf("usage: mmap_ftruncate <file> <size>\n");
		exit(2);
	}

	const char *file = argv[1];

	char *end;
	off_t sz = strtoull(argv[2], &end, 0);
	if (end == argv[2] || *end != '\0' || sz == 0) {
		fprintf(stderr, "E: invalid size");
		exit(2);
	}

	int fd = open(file, O_CREAT|O_TRUNC|O_RDWR, S_IRUSR|S_IWUSR);
	if (fd < 0)
		pdfail("open");

	if (ftruncate(fd, sz) < 0)
		pdfail("ftruncate");

	char *p = mmap(NULL, sz, PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED)
		pdfail("mmap");

	for (off_t off = 0; off < sz; off += 4096)
		p[off] = 1;

#ifdef __linux__
	if (sync_file_range(fd, 0, sz, SYNC_FILE_RANGE_WRITE) < 0)
		pdfail("sync_file_range");
#else
	if (msync(p, sz, MS_ASYNC) < 0)
		pdfail("msync");
#endif

	struct timespec t0, t1;
	if (clock_gettime(CLOCK_MONOTONIC, &t0) < 0)
		pdfail("clock_gettime");

	if (ftruncate(fd, 0) < 0)
		pdfail("ftruncate");

	if (clock_gettime(CLOCK_MONOTONIC, &t1) < 0)
		pdfail("clock_gettime");

	printf("%lld\n", (long long)(t1.tv_sec - t0.tv_sec) * 1000 +
	    (t1.tv_nsec - t0.tv_nsec) / 1000000);

	if (munmap(p, sz) < 0)
		pdfail("munmap");

	close(fd);
	return (0);
}
