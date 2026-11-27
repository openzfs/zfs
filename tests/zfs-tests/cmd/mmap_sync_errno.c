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
 * Store into every page of a shared mapping of a file, and write the pages
 * back with fsync(), fdatasync() and msync(MS_SYNC) in turn, storing into
 * them again before each call.  Each call must return 0 when the expected
 * error is 0, and -1 with errno set to the expected error otherwise.  A
 * failed writeback must not be reported as a positive errno with errno
 * left alone, which callers take for success (openzfs #19915).
 *
 * usage: mmap_sync_errno <file> 0|ENOSPC|EDQUOT
 *
 * Prints what each call returned.  Exits 0 if every call returned what was
 * expected, 1 if not, and 2 on errors.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static __attribute__((noreturn)) void
fail(const char *what)
{
	perror(what);
	exit(2);
}

static __attribute__((noreturn)) void
usage(void)
{
	(void) fprintf(stderr,
	    "usage: mmap_sync_errno <file> 0|ENOSPC|EDQUOT\n");
	exit(2);
}

int
main(int argc, char *argv[])
{
	const char *calls[] = { "fsync", "fdatasync", "msync" };
	long psz = sysconf(_SC_PAGESIZE);
	struct stat st;
	int expected;
	int bad = 0;

	if (argc != 3)
		usage();
	if (strcmp(argv[2], "0") == 0)
		expected = 0;
	else if (strcmp(argv[2], "ENOSPC") == 0)
		expected = ENOSPC;
	else if (strcmp(argv[2], "EDQUOT") == 0)
		expected = EDQUOT;
	else
		usage();

	int fd = open(argv[1], O_RDWR);
	if (fd < 0)
		fail("open");
	if (fstat(fd, &st) != 0)
		fail("fstat");
	if (psz <= 0 || st.st_size < psz) {
		(void) fprintf(stderr, "%s: shorter than a page\n", argv[1]);
		exit(2);
	}
	size_t len = st.st_size;

	char *p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED)
		fail("mmap");

	for (int c = 0; c < 3; c++) {
		int ret;

		for (size_t off = 0; off < len; off += psz)
			p[off]++;

		errno = 0;
		if (c == 0)
			ret = fsync(fd);
		else if (c == 1)
			ret = fdatasync(fd);
		else
			ret = msync(p, len, MS_SYNC);
		int err = errno;

		(void) printf("%s: returned %d, errno %d (%s)\n", calls[c],
		    ret, err, strerror(err));
		if (expected == 0 ? ret != 0 : (ret != -1 || err != expected))
			bad = 1;
	}

	(void) munmap(p, len);
	(void) close(fd);

	return (bad);
}
