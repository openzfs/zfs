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
 * Check that zeroing part of a page that readahead left unfilled does not
 * make that page up to date with the rest of it stale.
 *
 * usage: mmap_fadvise_readahead_zero_partial create <file> <seed>
 *        mmap_fadvise_readahead_zero_partial test|cold extend|punch <file>
 *            <seed>
 *
 * On Linux before 6.14, read_pages() leaves the pages that ->readahead()
 * did not take in the page cache, unlocked and not up to date.  When part
 * of such a page is zeroed (zfs_zero_partial_page()), the page must not be
 * marked up to date: the rest of it was never read.
 *
 * The file is two pages and TAIL bytes long (8292 bytes with 4K pages).
 * Its data comes from <seed> and is never zero, so that zeroes read in its
 * place are all counted.
 *
 * create	Write the file, which must not exist, and fsync it.  The caller
 *		then drops its pages from the page cache.
 * test		Touch the first page through a mapping, so that zpl_fadvise()
 *		goes on to generic_fadvise(), and fadvise(WILLNEED) the whole
 *		file.  Then zero part of the last page.  extend writes WBYTE
 *		past the last page, at five pages less 480 bytes (20000), so
 *		that the last page is zeroed from the old end of the file on;
 *		punch punches a hole of PUNCH_LEN bytes at PUNCH_OFF in the
 *		last page ([8200, 8250)).  Read the last page with pread() and
 *		compare it with what the file now holds.
 * cold		Read the whole file and compare it with what it holds after
 *		test.  The caller drops the file's pages first.
 *
 * Exit 0 if the bytes read are right, 1 if any is wrong, after printing
 * the wrong ranges, and 2 on any other error.
 */

#ifndef _GNU_SOURCE
#define	_GNU_SOURCE
#endif
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define	NAME		"mmap_fadvise_readahead_zero_partial"
#define	TAIL		100
#define	PUNCH_OFF	8
#define	PUNCH_LEN	50
#define	WBYTE		0x58

static int
usage(void)
{
	(void) fprintf(stderr, "usage: " NAME " create <file> <seed>\n"
	    "       " NAME " test|cold extend|punch <file> <seed>\n");
	return (2);
}

static int
fail(const char *what)
{
	(void) fprintf(stderr, NAME ": %s: %s\n", what, strerror(errno));
	return (2);
}

/* Fill buf with len bytes from seed (splitmix64), none of them zero. */
static void
fill(unsigned char *buf, size_t len, uint64_t seed)
{
	uint64_t x = seed;

	for (size_t i = 0; i < len; i++) {
		uint64_t z = (x += 0x9e3779b97f4a7c15ULL);

		z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
		z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
		z ^= z >> 31;
		buf[i] = 1 + z % 255;
	}
}

/* Print the ranges where got differs; return the number of wrong bytes. */
static long
compare(const char *how, off_t off, const unsigned char *got,
    const unsigned char *exp, long len)
{
	long nwrong = 0;

	for (long i = 0; i < len; ) {
		if (got[i] == exp[i]) {
			i++;
			continue;
		}
		long j = i;
		while (j < len && got[j] != exp[j])
			j++;
		(void) printf("  wrong at [%lld, %lld) (%ld bytes): got %02x "
		    "%02x .., expected %02x %02x ..\n", (long long)(off + i),
		    (long long)(off + j), j - i, got[i],
		    i + 1 < j ? got[i + 1] : 0, exp[i],
		    i + 1 < j ? exp[i + 1] : 0);
		nwrong += j - i;
		i = j;
	}
	(void) printf("%s: %ld of %ld bytes wrong\n", how, nwrong, len);
	return (nwrong);
}

int
main(int argc, char **argv)
{
	if (argc < 2)
		return (usage());

	const char *mode = argv[1];
	int create = strcmp(mode, "create") == 0;
	int test = strcmp(mode, "test") == 0;
	int cold = strcmp(mode, "cold") == 0;
	int extend = 0;
	int punch = 0;

	if (create) {
		if (argc != 4)
			return (usage());
	} else if (test || cold) {
		if (argc != 5)
			return (usage());
		extend = strcmp(argv[2], "extend") == 0;
		punch = strcmp(argv[2], "punch") == 0;
		if (!extend && !punch)
			return (usage());
	} else {
		return (usage());
	}

	const char *file = argv[argc - 2];
	char *end;
	errno = 0;
	uint64_t seed = strtoull(argv[argc - 1], &end, 0);
	if (errno != 0 || *end != '\0' || end == argv[argc - 1])
		return (usage());

	long pg = sysconf(_SC_PAGESIZE);
	if (pg < 4096) {
		(void) fprintf(stderr, NAME ": bad page size %ld\n", pg);
		return (2);
	}
	off_t last = 2 * pg;
	off_t size = last + TAIL;
	off_t woff = 5 * pg - 480;
	off_t hoff = last + PUNCH_OFF;

	/* What the file holds after create, and then after test. */
	off_t fsize = extend ? woff + 1 : size;
	unsigned char *exp = calloc(fsize, 1);
	unsigned char *got = malloc(fsize);
	if (exp == NULL || got == NULL)
		return (fail("malloc"));
	fill(exp, size, seed);

	if (create) {
		int fd = open(file, O_WRONLY | O_CREAT | O_EXCL, 0644);
		if (fd < 0)
			return (fail("open"));
		if (pwrite(fd, exp, size, 0) != size)
			return (fail("pwrite"));
		if (fsync(fd) != 0)
			return (fail("fsync"));
		struct stat st;
		if (fstat(fd, &st) != 0)
			return (fail("fstat"));
		(void) printf("created %s: %lld bytes, st_blksize %ld\n",
		    file, (long long)st.st_size, (long)st.st_blksize);
		(void) close(fd);
		return (0);
	}

	if (extend)
		exp[woff] = WBYTE;
	else
		(void) memset(exp + hoff, 0, PUNCH_LEN);

	int fd = open(file, test ? O_RDWR : O_RDONLY);
	if (fd < 0)
		return (fail("open"));
	struct stat st;
	if (fstat(fd, &st) != 0)
		return (fail("fstat"));

	if (cold) {
		if (st.st_size != fsize) {
			(void) printf("cold %s: file is %lld bytes, not %lld\n",
			    argv[2], (long long)st.st_size, (long long)fsize);
			return (1);
		}
		if (pread(fd, got, fsize, 0) != fsize)
			return (fail("pread"));
		char how[32];
		(void) snprintf(how, sizeof (how), "cold %s", argv[2]);
		return (compare(how, 0, got, exp, fsize) == 0 ? 0 : 1);
	}

	if (st.st_size != size) {
		(void) fprintf(stderr, NAME ": %s is %lld bytes, not %lld\n",
		    file, (long long)st.st_size, (long long)size);
		return (2);
	}

	unsigned char *p = mmap(NULL, pg, PROT_READ, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED)
		return (fail("mmap"));
	if (madvise(p, pg, MADV_RANDOM) != 0)
		return (fail("madvise"));
	volatile unsigned char c = p[0];
	(void) c;

	int err = posix_fadvise(fd, 0, 0, POSIX_FADV_WILLNEED);
	if (err != 0) {
		errno = err;
		return (fail("posix_fadvise"));
	}

	if (extend) {
		unsigned char b = WBYTE;
		if (pwrite(fd, &b, 1, woff) != 1)
			return (fail("pwrite"));
	} else if (fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE,
	    hoff, PUNCH_LEN) != 0) {
		return (fail("fallocate"));
	}

	long len = fsize - last < pg ? fsize - last : pg;
	ssize_t n = pread(fd, got, pg, last);
	if (n < 0)
		return (fail("pread"));
	if (n != len) {
		(void) printf("test %s: read %zd bytes at %lld, not %ld\n",
		    argv[2], n, (long long)last, len);
		return (1);
	}
	char how[32];
	(void) snprintf(how, sizeof (how), "test %s", argv[2]);
	long nwrong = compare(how, last, got, exp + last, len);

	(void) munmap(p, pg);
	(void) close(fd);
	return (nwrong == 0 ? 0 : 1);
}
